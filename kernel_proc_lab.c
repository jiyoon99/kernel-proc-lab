#include <linux/atomic.h>
#include <linux/capability.h>
#include <linux/cdev.h>
#include <linux/cred.h>
#include <linux/device.h>
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include "kernel_proc_lab_ioctl.h"
#include "kernel_proc_lab_ring.h"
#include "kernel_proc_lab_version.h"

#define CREATE_TRACE_POINTS
#include "trace/events/kernel_proc_lab_trace.h"

#define PROC_NAME "kernel_proc_lab"
#define DEVICE_NAME "kernel_proc_lab"
#define CLASS_NAME "kernel_proc_lab"
#define MESSAGE_MAX KERNEL_PROC_LAB_MESSAGE_MAX
#define DEFAULT_LOG_CAPACITY KERNEL_PROC_LAB_LOG_CAPACITY
#define MIN_LOG_CAPACITY 8
#define MAX_LOG_CAPACITY 4096
#define DEFAULT_HEARTBEAT_INTERVAL_MS 5000
#define MIN_HEARTBEAT_INTERVAL_MS 250
#define MAX_HEARTBEAT_INTERVAL_MS 60000
#define LAB_VERSION KERNEL_PROC_LAB_VERSION

struct lab_file_state {
	u64 seen_sequence;
	u64 next_stream_sequence;
	size_t pending_offset;
	size_t pending_len;
	char pending_line[MESSAGE_MAX + 160];
	bool stream_enabled;
};

static struct proc_dir_entry *proc_entry;
static struct dentry *debugfs_dir;
static dev_t device_number;
static struct cdev lab_cdev;
static struct class *lab_class;
static struct device *lab_device;
static atomic64_t read_count = ATOMIC64_INIT(0);
static atomic64_t write_count = ATOMIC64_INIT(0);
static atomic64_t open_count = ATOMIC64_INIT(0);
static atomic64_t log_sequence = ATOMIC64_INIT(0);
static atomic64_t heartbeat_count = ATOMIC64_INIT(0);
static atomic64_t dropped_log_count = ATOMIC64_INIT(0);
static atomic_t heartbeat_enabled = ATOMIC_INIT(0);
static atomic_t heartbeat_interval_ms = ATOMIC_INIT(DEFAULT_HEARTBEAT_INTERVAL_MS);
static uint initial_heartbeat_interval_ms = DEFAULT_HEARTBEAT_INTERVAL_MS;
static uint log_capacity = DEFAULT_LOG_CAPACITY;
static bool start_heartbeat_on_load;
static char last_message[MESSAGE_MAX] = "hello from kernel space";
static struct kernel_proc_lab_log_entry *log_entries;
static u32 log_count;
static struct kernel_proc_lab_filter event_filter;
static pid_t last_writer_pid;
static char last_writer_comm[TASK_COMM_LEN] = "none";
static DEFINE_MUTEX(message_lock);
static DECLARE_WAIT_QUEUE_HEAD(message_wait);
static void heartbeat_work_fn(struct work_struct *work);
static DECLARE_DELAYED_WORK(heartbeat_work, heartbeat_work_fn);
static void fill_stats(struct kernel_proc_lab_stats *stats);
static void copy_log_snapshot_locked(struct kernel_proc_lab_log_snapshot *snapshot);
static u64 retained_log_start(void);
static u64 retained_log_start_locked(void);

module_param(initial_heartbeat_interval_ms, uint, 0444);
MODULE_PARM_DESC(initial_heartbeat_interval_ms,
		 "Initial heartbeat interval in milliseconds, 250..60000");
module_param(start_heartbeat_on_load, bool, 0444);
MODULE_PARM_DESC(start_heartbeat_on_load,
		 "Start heartbeat delayed work when the module loads");
module_param(log_capacity, uint, 0444);
MODULE_PARM_DESC(log_capacity, "Retained event ring capacity, 8..4096");

static char *lab_devnode(const struct device *dev, umode_t *mode)
{
	if (mode)
		*mode = 0660;
	return NULL;
}

static bool control_allowed(void)
{
	return capable(CAP_SYS_ADMIN);
}

static int require_control_permission(void)
{
	return control_allowed() ? 0 : -EPERM;
}

static bool event_allowed(u32 type, u32 pid, u32 uid, const char *comm)
{
	u32 type_bit;

	if (!event_filter.enabled)
		return true;

	if (event_filter.type_mask != 0) {
		type_bit = type < 32 ? (1U << type) : 0;
		if ((event_filter.type_mask & type_bit) == 0)
			return false;
	}
	if (event_filter.pid != 0 && event_filter.pid != pid)
		return false;
	if (event_filter.uid != 0 && event_filter.uid != uid)
		return false;
	if (event_filter.comm[0] != '\0' && strcmp(event_filter.comm, comm) != 0)
		return false;

	return true;
}

static u64 append_log_entry(u32 type, const char *message)
{
	u64 seq = (u64)atomic64_inc_return(&log_sequence);
	u32 pid = (u32)task_pid_nr(current);
	u32 uid = from_kuid(&init_user_ns, current_uid());

	if (!event_allowed(type, pid, uid, current->comm))
		return seq;

	kernel_proc_lab_ring_fill_entry(log_entries, log_capacity, seq,
					ktime_get_ns(), type, pid, uid,
					current->comm, message);
	if (log_count < log_capacity)
		log_count += 1;
	else
		atomic64_inc(&dropped_log_count);

	wake_up_interruptible(&message_wait);
	return seq;
}

static void record_writer_current(void)
{
	last_writer_pid = task_pid_nr(current);
	get_task_comm(last_writer_comm, current);
}

static int status_seq_show(struct seq_file *seq, void *v)
{
	struct kernel_proc_lab_stats stats;

	(void)v;
	fill_stats(&stats);

	seq_puts(seq, "kernel_proc_lab debug status\n");
	seq_printf(seq, "reads: %llu\n", stats.reads);
	seq_printf(seq, "writes: %llu\n", stats.writes);
	seq_printf(seq, "opens: %llu\n", stats.opens);
	seq_printf(seq, "log_events: %llu\n", stats.log_events);
	seq_printf(seq, "heartbeat_ticks: %llu\n", stats.heartbeat_ticks);
	seq_printf(seq, "dropped_log_events: %llu\n",
		   (unsigned long long)atomic64_read(&dropped_log_count));
	seq_printf(seq, "heartbeat_enabled: %u\n", stats.heartbeat_enabled);
	seq_printf(seq, "heartbeat_interval_ms: %u\n", stats.heartbeat_interval_ms);
	seq_printf(seq, "abi_version: %u\n", stats.abi_version);
	seq_printf(seq, "retained_log_start: %llu\n",
		   (unsigned long long)retained_log_start());
	seq_printf(seq, "log_capacity: %u\n", log_capacity);
	seq_printf(seq, "message_max: %u\n", MESSAGE_MAX);
	seq_printf(seq, "device_major: %u\n", MAJOR(device_number));
	seq_printf(seq, "device_minor: %u\n", MINOR(device_number));
	seq_printf(seq, "initial_heartbeat_interval_ms: %u\n",
		   initial_heartbeat_interval_ms);
	seq_printf(seq, "start_heartbeat_on_load: %u\n",
		   start_heartbeat_on_load ? 1 : 0);
	seq_printf(seq, "version: %s\n", LAB_VERSION);
	mutex_lock(&message_lock);
	seq_printf(seq, "last_writer_pid: %d\n", last_writer_pid);
	seq_printf(seq, "last_writer_comm: %s\n", last_writer_comm);
	seq_printf(seq, "last_message: %s\n", last_message);
	mutex_unlock(&message_lock);

	return 0;
}

static int log_seq_show(struct seq_file *seq, void *v)
{
	struct kernel_proc_lab_log_snapshot *snapshot;
	u32 index;

	(void)v;
	snapshot = kzalloc(sizeof(*snapshot), GFP_KERNEL);
	if (!snapshot)
		return -ENOMEM;

	mutex_lock(&message_lock);
	copy_log_snapshot_locked(snapshot);
	mutex_unlock(&message_lock);

	seq_puts(seq, "recent log dump\n");
	for (index = 0; index < snapshot->count; index += 1)
		seq_printf(seq, "#%llu %s\n", snapshot->entries[index].seq,
			   snapshot->entries[index].message);

	kfree(snapshot);
	return 0;
}

static int debugfs_status_open(struct inode *inode, struct file *file)
{
	return single_open(file, status_seq_show, inode->i_private);
}

static int debugfs_log_open(struct inode *inode, struct file *file)
{
	return single_open(file, log_seq_show, inode->i_private);
}

static const struct file_operations debugfs_status_fops = {
	.owner = THIS_MODULE,
	.open = debugfs_status_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations debugfs_log_fops = {
	.owner = THIS_MODULE,
	.open = debugfs_log_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static void fill_stats(struct kernel_proc_lab_stats *stats)
{
	stats->reads = atomic64_read(&read_count);
	stats->writes = atomic64_read(&write_count);
	stats->opens = atomic64_read(&open_count);
	stats->log_events = atomic64_read(&log_sequence);
	stats->heartbeat_ticks = atomic64_read(&heartbeat_count);
	stats->dropped_log_events = atomic64_read(&dropped_log_count);
	stats->heartbeat_enabled = atomic_read(&heartbeat_enabled);
	stats->heartbeat_interval_ms = atomic_read(&heartbeat_interval_ms);
	stats->abi_version = KERNEL_PROC_LAB_ABI_VERSION;
	stats->log_capacity = log_capacity;
	stats->message_max = MESSAGE_MAX;
	stats->reserved = 0;
}

static int set_heartbeat_interval(unsigned int interval_ms)
{
	unsigned int current_interval;

	if (interval_ms < MIN_HEARTBEAT_INTERVAL_MS ||
	    interval_ms > MAX_HEARTBEAT_INTERVAL_MS)
		return -EINVAL;

	atomic_set(&heartbeat_interval_ms, interval_ms);
	current_interval = atomic_read(&heartbeat_interval_ms);
	trace_kernel_proc_lab_config(current_interval, atomic_read(&heartbeat_enabled));
	if (atomic_read(&heartbeat_enabled))
		mod_delayed_work(system_wq, &heartbeat_work,
				 msecs_to_jiffies(current_interval));

	return 0;
}

static void heartbeat_work_fn(struct work_struct *work)
{
	char message[MESSAGE_MAX];
	u64 tick;

	(void)work;

	if (!atomic_read(&heartbeat_enabled))
		return;

	tick = (u64)atomic64_inc_return(&heartbeat_count);
	snprintf(message, sizeof(message), "heartbeat tick %llu",
		 (unsigned long long)tick);

	mutex_lock(&message_lock);
	strscpy(last_message, message, MESSAGE_MAX);
	tick = append_log_entry(KERNEL_PROC_LAB_EVENT_HEARTBEAT, message);
	mutex_unlock(&message_lock);
	trace_kernel_proc_lab_event("heartbeat", tick, message);

	if (atomic_read(&heartbeat_enabled))
		schedule_delayed_work(&heartbeat_work,
				      msecs_to_jiffies(atomic_read(&heartbeat_interval_ms)));
}

static void start_heartbeat(void)
{
	if (atomic_xchg(&heartbeat_enabled, 1) == 0)
		schedule_delayed_work(&heartbeat_work, 0);
}

static void stop_heartbeat(void)
{
	if (atomic_xchg(&heartbeat_enabled, 0) != 0)
		cancel_delayed_work_sync(&heartbeat_work);
}

static void copy_log_snapshot_locked(struct kernel_proc_lab_log_snapshot *snapshot)
{
	kernel_proc_lab_ring_snapshot(log_entries, log_capacity, log_count,
				      (u64)atomic64_read(&log_sequence), snapshot);
}

static bool copy_log_entry_locked(u64 sequence,
				  struct kernel_proc_lab_log_entry *entry)
{
	return kernel_proc_lab_ring_copy_entry(log_entries, log_capacity, log_count,
					       (u64)atomic64_read(&log_sequence),
					       sequence, entry);
}

static u64 retained_log_start_locked(void)
{
	return kernel_proc_lab_ring_retained_start(
		(u64)atomic64_read(&log_sequence), log_count);
}

static u64 retained_log_start(void)
{
	u64 start_seq;

	mutex_lock(&message_lock);
	start_seq = retained_log_start_locked();
	mutex_unlock(&message_lock);
	return start_seq;
}

static void reset_io_stats(void)
{
	atomic64_set(&read_count, 0);
	atomic64_set(&write_count, 0);
	atomic64_set(&open_count, 0);
}

static void reset_log_locked(void)
{
	memset(log_entries, 0, sizeof(*log_entries) * log_capacity);
	log_count = 0;
	atomic64_set(&log_sequence, 0);
	atomic64_set(&dropped_log_count, 0);
}

static ssize_t write_message_from_user(const char __user *buffer, size_t count)
{
	size_t copy_len = min(count, (size_t)MESSAGE_MAX - 1);
	char *tmp;
	u64 seq;

	if (count == 0)
		return 0;

	tmp = kzalloc(MESSAGE_MAX, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	if (copy_from_user(tmp, buffer, copy_len)) {
		kfree(tmp);
		return -EFAULT;
	}

	if (copy_len > 0 && tmp[copy_len - 1] == '\n')
		tmp[copy_len - 1] = '\0';

	mutex_lock(&message_lock);
	record_writer_current();
	strscpy(last_message, tmp, MESSAGE_MAX);
	seq = append_log_entry(KERNEL_PROC_LAB_EVENT_WRITE, tmp);
	mutex_unlock(&message_lock);
	trace_kernel_proc_lab_event("write", seq, tmp);
	atomic64_inc(&write_count);
	kfree(tmp);

	return count;
}

static int kernel_proc_lab_show(struct seq_file *seq, void *v)
{
	seq_printf(seq, "module: kernel_proc_lab\n");
	seq_printf(seq, "device: /dev/%s\n", DEVICE_NAME);
	seq_printf(seq, "reads: %lld\n", atomic64_read(&read_count));
	seq_printf(seq, "writes: %lld\n", atomic64_read(&write_count));
	seq_printf(seq, "opens: %lld\n", atomic64_read(&open_count));
	seq_printf(seq, "log_events: %lld\n", atomic64_read(&log_sequence));
	seq_printf(seq, "heartbeat_ticks: %lld\n", atomic64_read(&heartbeat_count));
	seq_printf(seq, "dropped_log_events: %lld\n",
		   atomic64_read(&dropped_log_count));
	seq_printf(seq, "heartbeat_enabled: %d\n", atomic_read(&heartbeat_enabled));
	seq_printf(seq, "heartbeat_interval_ms: %d\n",
		   atomic_read(&heartbeat_interval_ms));
	seq_printf(seq, "abi_version: %u\n", KERNEL_PROC_LAB_ABI_VERSION);
	seq_printf(seq, "retained_log_start: %llu\n",
		   (unsigned long long)retained_log_start());
	seq_printf(seq, "log_capacity: %u\n", log_capacity);
	seq_printf(seq, "message_max: %u\n", MESSAGE_MAX);
	seq_printf(seq, "device_major: %u\n", MAJOR(device_number));
	seq_printf(seq, "device_minor: %u\n", MINOR(device_number));
	seq_printf(seq, "initial_heartbeat_interval_ms: %u\n",
		   initial_heartbeat_interval_ms);
	seq_printf(seq, "start_heartbeat_on_load: %u\n",
		   start_heartbeat_on_load ? 1 : 0);
	seq_printf(seq, "version: %s\n", LAB_VERSION);
	mutex_lock(&message_lock);
	seq_printf(seq, "last_writer_pid: %d\n", last_writer_pid);
	seq_printf(seq, "last_writer_comm: %s\n", last_writer_comm);
	seq_printf(seq, "last_message: %s\n", last_message);
	if (log_count > 0) {
		struct kernel_proc_lab_log_snapshot *snapshot;
		u32 index;

		snapshot = kzalloc(sizeof(*snapshot), GFP_KERNEL);
		if (snapshot) {
			copy_log_snapshot_locked(snapshot);
			seq_puts(seq, "recent_log:\n");
			for (index = 0; index < snapshot->count; index += 1)
				seq_printf(seq,
					   "  #%llu type=%u ts=%llu pid=%u uid=%u comm=%s msg=%s\n",
					   snapshot->entries[index].seq,
					   snapshot->entries[index].type,
					   snapshot->entries[index].timestamp_ns,
					   snapshot->entries[index].pid,
					   snapshot->entries[index].uid,
					   snapshot->entries[index].comm,
					   snapshot->entries[index].message);
			kfree(snapshot);
		}
	}
	mutex_unlock(&message_lock);
	seq_printf(seq, "usage: echo \"message\" | sudo tee /dev/%s\n", DEVICE_NAME);

	return 0;
}

static int kernel_proc_lab_open(struct inode *inode, struct file *file)
{
	return single_open(file, kernel_proc_lab_show, NULL);
}

static ssize_t kernel_proc_lab_write(struct file *file, const char __user *buffer,
				     size_t count, loff_t *pos)
{
	return write_message_from_user(buffer, count);
}

static ssize_t stats_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct kernel_proc_lab_stats stats;

	(void)dev;
	(void)attr;
	fill_stats(&stats);

	return sysfs_emit(buf,
			  "reads: %llu\nwrites: %llu\nopens: %llu\nlog_events: %llu\nheartbeat_ticks: %llu\ndropped_log_events: %llu\nheartbeat_enabled: %u\nheartbeat_interval_ms: %u\nabi_version: %u\nlog_capacity: %u\nmessage_max: %u\n",
			  stats.reads, stats.writes, stats.opens, stats.log_events,
			  stats.heartbeat_ticks, stats.dropped_log_events,
			  stats.heartbeat_enabled, stats.heartbeat_interval_ms,
			  stats.abi_version, stats.log_capacity, stats.message_max);
}
static DEVICE_ATTR_RO(stats);

static ssize_t last_message_show(struct device *dev, struct device_attribute *attr,
				 char *buf)
{
	ssize_t ret;

	(void)dev;
	(void)attr;
	mutex_lock(&message_lock);
	ret = sysfs_emit(buf, "%s\n", last_message);
	mutex_unlock(&message_lock);
	return ret;
}
static DEVICE_ATTR_RO(last_message);

static ssize_t heartbeat_enabled_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	(void)dev;
	(void)attr;
	return sysfs_emit(buf, "%d\n", atomic_read(&heartbeat_enabled));
}

static ssize_t heartbeat_enabled_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	bool enabled;
	int ret;

	(void)dev;
	(void)attr;
	ret = require_control_permission();
	if (ret)
		return ret;

	ret = kstrtobool(buf, &enabled);
	if (ret)
		return ret;

	if (enabled)
		start_heartbeat();
	else
		stop_heartbeat();

	return count;
}
static DEVICE_ATTR_RW(heartbeat_enabled);

static ssize_t heartbeat_interval_ms_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	(void)dev;
	(void)attr;
	return sysfs_emit(buf, "%d\n", atomic_read(&heartbeat_interval_ms));
}

static ssize_t heartbeat_interval_ms_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	unsigned int interval_ms;
	int ret;

	(void)dev;
	(void)attr;
	ret = require_control_permission();
	if (ret)
		return ret;

	ret = kstrtouint(buf, 10, &interval_ms);
	if (ret)
		return ret;

	ret = set_heartbeat_interval(interval_ms);
	if (ret)
		return ret;

	return count;
}
static DEVICE_ATTR_RW(heartbeat_interval_ms);

static struct attribute *kernel_proc_lab_attrs[] = {
	&dev_attr_stats.attr,
	&dev_attr_last_message.attr,
	&dev_attr_heartbeat_enabled.attr,
	&dev_attr_heartbeat_interval_ms.attr,
	NULL,
};

static const struct attribute_group kernel_proc_lab_attr_group = {
	.attrs = kernel_proc_lab_attrs,
};

static int lab_chrdev_open(struct inode *inode, struct file *file)
{
	struct lab_file_state *state;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	state->seen_sequence = (u64)atomic64_read(&log_sequence);
	state->next_stream_sequence = retained_log_start();
	state->pending_offset = 0;
	state->pending_len = 0;
	state->stream_enabled = false;
	file->private_data = state;
	atomic64_inc(&open_count);
	return 0;
}

static int lab_chrdev_release(struct inode *inode, struct file *file)
{
	kfree(file->private_data);
	file->private_data = NULL;
	return 0;
}

static ssize_t lab_chrdev_read(struct file *file, char __user *buffer, size_t count,
			       loff_t *pos)
{
	struct lab_file_state *state = file->private_data;

	if (count == 0)
		return 0;

	if (state && state->stream_enabled) {
		struct kernel_proc_lab_log_entry entry;
		size_t len;

		for (;;) {
			u64 latest_seq;
			u64 start_seq;

			if (state->pending_offset < state->pending_len) {
				len = state->pending_len - state->pending_offset;
				if (count < len)
					len = count;
				if (copy_to_user(buffer,
						 state->pending_line + state->pending_offset,
						 len))
					return -EFAULT;
				state->pending_offset += len;
				if (state->pending_offset >= state->pending_len) {
					state->pending_offset = 0;
					state->pending_len = 0;
				}
				atomic64_inc(&read_count);
				return len;
			}

			mutex_lock(&message_lock);
			start_seq = retained_log_start_locked();
			latest_seq = (u64)atomic64_read(&log_sequence);
			if (state->next_stream_sequence < start_seq ||
			    state->next_stream_sequence > latest_seq + 1)
				state->next_stream_sequence = start_seq;
			if (copy_log_entry_locked(state->next_stream_sequence, &entry)) {
				state->next_stream_sequence += 1;
				mutex_unlock(&message_lock);
				state->pending_len =
					scnprintf(state->pending_line,
						  sizeof(state->pending_line),
						  "#%llu type=%u ts=%llu pid=%u uid=%u comm=%s msg=%s\n",
						  (unsigned long long)entry.seq,
						  entry.type,
						  (unsigned long long)entry.timestamp_ns,
						  entry.pid, entry.uid,
						  entry.comm,
						  entry.message);
				len = state->pending_len;
				if (count < len)
					len = count;
				if (copy_to_user(buffer, state->pending_line, len))
					return -EFAULT;
				state->pending_offset = len;
				if (state->pending_offset >= state->pending_len) {
					state->pending_offset = 0;
					state->pending_len = 0;
				}
				state->seen_sequence = entry.seq;
				atomic64_inc(&read_count);
				return len;
			}
			mutex_unlock(&message_lock);

			if (file->f_flags & O_NONBLOCK)
				return -EAGAIN;

			if (wait_event_interruptible(message_wait,
						     atomic64_read(&log_sequence) >=
							     state->next_stream_sequence ||
						     state->next_stream_sequence >
							     atomic64_read(&log_sequence) + 1))
				return -ERESTARTSYS;
		}
	}

	{
		char snapshot[MESSAGE_MAX];
		size_t len;

		if (*pos > 0)
			return 0;

		mutex_lock(&message_lock);
		strscpy(snapshot, last_message, sizeof(snapshot));
		mutex_unlock(&message_lock);

		len = strnlen(snapshot, sizeof(snapshot));
		if (len < sizeof(snapshot) - 1) {
			snapshot[len] = '\n';
			snapshot[len + 1] = '\0';
			len += 1;
		}

		if (count < len)
			len = count;

		if (copy_to_user(buffer, snapshot, len))
			return -EFAULT;

		*pos += len;
		if (file->private_data)
			((struct lab_file_state *)file->private_data)->seen_sequence =
				(u64)atomic64_read(&log_sequence);
		atomic64_inc(&read_count);
		return len;
	}
}

static ssize_t lab_chrdev_write(struct file *file, const char __user *buffer,
				size_t count, loff_t *pos)
{
	return write_message_from_user(buffer, count);
}

static long lab_chrdev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct kernel_proc_lab_stats stats;
	struct kernel_proc_lab_log_read log_read;
	struct kernel_proc_lab_log_entry *entries;
	struct kernel_proc_lab_config config;
	long ret;

	switch (cmd) {
	case KERNEL_PROC_LAB_IOC_GET_STATS:
		fill_stats(&stats);
		if (copy_to_user((void __user *)arg, &stats, sizeof(stats)))
			return -EFAULT;
		return 0;
	case KERNEL_PROC_LAB_IOC_RESET_STATS:
		ret = require_control_permission();
		if (ret)
			return ret;
		reset_io_stats();
		return 0;
	case KERNEL_PROC_LAB_IOC_CLEAR_MESSAGE:
	{
		u64 seq;

		ret = require_control_permission();
		if (ret)
			return ret;
		mutex_lock(&message_lock);
		record_writer_current();
		strscpy(last_message, "", MESSAGE_MAX);
		seq = append_log_entry(KERNEL_PROC_LAB_EVENT_CLEAR,
				       "[message cleared]");
		mutex_unlock(&message_lock);
		trace_kernel_proc_lab_event("clear", seq, "[message cleared]");
		return 0;
	}
	case KERNEL_PROC_LAB_IOC_GET_LOG_V2:
		if (copy_from_user(&log_read, (void __user *)arg, sizeof(log_read)))
			return -EFAULT;
		if (log_read.abi_version != KERNEL_PROC_LAB_ABI_VERSION ||
		    log_read.entry_size != sizeof(struct kernel_proc_lab_log_entry) ||
		    log_read.capacity > KERNEL_PROC_LAB_USER_LOG_CAPACITY ||
		    (log_read.capacity > 0 && log_read.entries == 0))
			return -EINVAL;

		if (log_read.capacity > 0) {
			entries = kcalloc(log_read.capacity, sizeof(*entries),
					  GFP_KERNEL);
			if (!entries)
				return -ENOMEM;
		} else {
			entries = NULL;
		}

		mutex_lock(&message_lock);
		log_read.count = min(log_count, log_read.capacity);
		log_read.start_seq = retained_log_start_locked();
		log_read.latest_seq = (u64)atomic64_read(&log_sequence);
		for (ret = 0; ret < log_read.count; ret += 1) {
			u64 seq = log_read.latest_seq - log_read.count + 1 + ret;
			u32 source = kernel_proc_lab_ring_index(seq, log_capacity);

			entries[ret] = log_entries[source];
		}
		mutex_unlock(&message_lock);

		ret = 0;
		if (log_read.count > 0)
			ret = copy_to_user((void __user *)(unsigned long)log_read.entries,
					   entries,
					   log_read.count * sizeof(*entries)) ?
			      -EFAULT : 0;
		kfree(entries);
		if (ret)
			return ret;
		if (copy_to_user((void __user *)arg, &log_read, sizeof(log_read)))
			return -EFAULT;
		if (file->private_data)
			((struct lab_file_state *)file->private_data)->seen_sequence =
				(u64)atomic64_read(&log_sequence);
		return 0;
	case KERNEL_PROC_LAB_IOC_START_HEARTBEAT:
		ret = require_control_permission();
		if (ret)
			return ret;
		start_heartbeat();
		trace_kernel_proc_lab_config(atomic_read(&heartbeat_interval_ms), 1);
		return 0;
	case KERNEL_PROC_LAB_IOC_STOP_HEARTBEAT:
		ret = require_control_permission();
		if (ret)
			return ret;
		stop_heartbeat();
		trace_kernel_proc_lab_config(atomic_read(&heartbeat_interval_ms), 0);
		return 0;
	case KERNEL_PROC_LAB_IOC_GET_CONFIG:
		config.heartbeat_interval_ms = atomic_read(&heartbeat_interval_ms);
		if (copy_to_user((void __user *)arg, &config, sizeof(config)))
			return -EFAULT;
		return 0;
	case KERNEL_PROC_LAB_IOC_SET_CONFIG:
		ret = require_control_permission();
		if (ret)
			return ret;
		if (copy_from_user(&config, (void __user *)arg, sizeof(config)))
			return -EFAULT;
		return set_heartbeat_interval(config.heartbeat_interval_ms);
	case KERNEL_PROC_LAB_IOC_START_STREAM:
		if (file->private_data) {
			struct lab_file_state *state = file->private_data;

			mutex_lock(&message_lock);
			state->next_stream_sequence = retained_log_start_locked();
			mutex_unlock(&message_lock);
			state->pending_offset = 0;
			state->pending_len = 0;
			state->stream_enabled = true;
			trace_kernel_proc_lab_event("stream_start",
						    state->next_stream_sequence,
						    "stream enabled");
		}
		return 0;
	case KERNEL_PROC_LAB_IOC_STOP_STREAM:
		if (file->private_data) {
			struct lab_file_state *state = file->private_data;

			state->stream_enabled = false;
			state->next_stream_sequence = (u64)atomic64_read(&log_sequence);
			state->pending_offset = 0;
			state->pending_len = 0;
			trace_kernel_proc_lab_event("stream_stop",
						    state->next_stream_sequence,
						    "stream disabled");
		}
		return 0;
	case KERNEL_PROC_LAB_IOC_RESET_LOG:
		ret = require_control_permission();
		if (ret)
			return ret;
		mutex_lock(&message_lock);
		reset_log_locked();
		mutex_unlock(&message_lock);
		wake_up_interruptible(&message_wait);
		return 0;
	case KERNEL_PROC_LAB_IOC_RESET_ALL:
		ret = require_control_permission();
		if (ret)
			return ret;
		reset_io_stats();
		atomic64_set(&heartbeat_count, 0);
		mutex_lock(&message_lock);
		reset_log_locked();
		strscpy(last_message, "", MESSAGE_MAX);
		last_writer_pid = 0;
		strscpy(last_writer_comm, "none", TASK_COMM_LEN);
		mutex_unlock(&message_lock);
		wake_up_interruptible(&message_wait);
		return 0;
	case KERNEL_PROC_LAB_IOC_GET_FILTER:
	{
		struct kernel_proc_lab_filter filter;

		mutex_lock(&message_lock);
		filter = event_filter;
		mutex_unlock(&message_lock);
		if (copy_to_user((void __user *)arg, &filter, sizeof(filter)))
			return -EFAULT;
		return 0;
	}
	case KERNEL_PROC_LAB_IOC_SET_FILTER:
	{
		struct kernel_proc_lab_filter filter;

		ret = require_control_permission();
		if (ret)
			return ret;
		if (copy_from_user(&filter, (void __user *)arg, sizeof(filter)))
			return -EFAULT;
		filter.comm[KERNEL_PROC_LAB_COMM_MAX - 1] = '\0';
		filter.enabled = filter.enabled ? 1 : 0;
		mutex_lock(&message_lock);
		event_filter = filter;
		mutex_unlock(&message_lock);
		trace_kernel_proc_lab_event("filter", 0, "filter updated");
		return 0;
	}
	case KERNEL_PROC_LAB_IOC_CLEAR_FILTER:
		ret = require_control_permission();
		if (ret)
			return ret;
		mutex_lock(&message_lock);
		memset(&event_filter, 0, sizeof(event_filter));
		mutex_unlock(&message_lock);
		trace_kernel_proc_lab_event("filter", 0, "filter cleared");
		return 0;
	default:
		return -ENOTTY;
	}
}

static __poll_t lab_chrdev_poll(struct file *file, poll_table *wait)
{
	struct lab_file_state *state = file->private_data;
	u64 latest_seq = (u64)atomic64_read(&log_sequence);

	poll_wait(file, &message_wait, wait);
	if (state && state->stream_enabled) {
		if (state->next_stream_sequence > latest_seq + 1 ||
		    latest_seq >= state->next_stream_sequence)
			return EPOLLIN | EPOLLRDNORM;
		return 0;
	}
	if (!state || state->seen_sequence > latest_seq + 1 ||
	    latest_seq > state->seen_sequence)
		return EPOLLIN | EPOLLRDNORM;

	return 0;
}

static const struct proc_ops kernel_proc_lab_ops = {
	.proc_open = kernel_proc_lab_open,
	.proc_read = seq_read,
	.proc_write = kernel_proc_lab_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static const struct file_operations lab_chrdev_fops = {
	.owner = THIS_MODULE,
	.open = lab_chrdev_open,
	.release = lab_chrdev_release,
	.read = lab_chrdev_read,
	.write = lab_chrdev_write,
	.unlocked_ioctl = lab_chrdev_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = lab_chrdev_ioctl,
#endif
	.poll = lab_chrdev_poll,
	.llseek = default_llseek,
};

static int __init kernel_proc_lab_init(void)
{
	int ret;

	if (initial_heartbeat_interval_ms < MIN_HEARTBEAT_INTERVAL_MS ||
	    initial_heartbeat_interval_ms > MAX_HEARTBEAT_INTERVAL_MS) {
		pr_err("kernel_proc_lab: invalid initial_heartbeat_interval_ms=%u\n",
		       initial_heartbeat_interval_ms);
		return -EINVAL;
	}
	if (log_capacity < MIN_LOG_CAPACITY || log_capacity > MAX_LOG_CAPACITY) {
		pr_err("kernel_proc_lab: invalid log_capacity=%u\n", log_capacity);
		return -EINVAL;
	}
	atomic_set(&heartbeat_interval_ms, initial_heartbeat_interval_ms);

	log_entries = kvcalloc(log_capacity, sizeof(*log_entries), GFP_KERNEL);
	if (!log_entries)
		return -ENOMEM;

	ret = alloc_chrdev_region(&device_number, 0, 1, DEVICE_NAME);
	if (ret)
		goto free_log_entries;

	cdev_init(&lab_cdev, &lab_chrdev_fops);
	lab_cdev.owner = THIS_MODULE;

	ret = cdev_add(&lab_cdev, device_number, 1);
	if (ret)
		goto unregister_chrdev;

	lab_class = class_create(CLASS_NAME);
	if (IS_ERR(lab_class)) {
		ret = PTR_ERR(lab_class);
		goto delete_cdev;
	}
	lab_class->devnode = lab_devnode;

	lab_device = device_create(lab_class, NULL, device_number, NULL, DEVICE_NAME);
	if (IS_ERR(lab_device)) {
		ret = PTR_ERR(lab_device);
		goto destroy_class;
	}

	ret = sysfs_create_group(&lab_device->kobj, &kernel_proc_lab_attr_group);
	if (ret)
		goto destroy_device;

	debugfs_dir = debugfs_create_dir(DEVICE_NAME, NULL);
	if (IS_ERR_OR_NULL(debugfs_dir))
		debugfs_dir = NULL;
	else {
		debugfs_create_file("status", 0444, debugfs_dir, NULL,
				    &debugfs_status_fops);
		debugfs_create_file("log", 0444, debugfs_dir, NULL,
				    &debugfs_log_fops);
	}

	proc_entry = proc_create(PROC_NAME, 0644, NULL, &kernel_proc_lab_ops);
	if (!proc_entry) {
		ret = -ENOMEM;
		goto remove_sysfs_group;
	}

	if (start_heartbeat_on_load)
		start_heartbeat();

	pr_info("kernel_proc_lab: loaded, /proc/%s and /dev/%s ready\n", PROC_NAME,
		DEVICE_NAME);
	return 0;

remove_sysfs_group:
	sysfs_remove_group(&lab_device->kobj, &kernel_proc_lab_attr_group);
destroy_device:
	stop_heartbeat();
	debugfs_remove_recursive(debugfs_dir);
	debugfs_dir = NULL;
	device_destroy(lab_class, device_number);
destroy_class:
	class_destroy(lab_class);
delete_cdev:
	cdev_del(&lab_cdev);
unregister_chrdev:
	unregister_chrdev_region(device_number, 1);
free_log_entries:
	kvfree(log_entries);
	log_entries = NULL;
	return ret;
}

static void __exit kernel_proc_lab_exit(void)
{
	stop_heartbeat();
	proc_remove(proc_entry);
	if (debugfs_dir)
		debugfs_remove_recursive(debugfs_dir);
	sysfs_remove_group(&lab_device->kobj, &kernel_proc_lab_attr_group);
	device_destroy(lab_class, device_number);
	class_destroy(lab_class);
	cdev_del(&lab_cdev);
	unregister_chrdev_region(device_number, 1);
	kvfree(log_entries);
	log_entries = NULL;
	pr_info("kernel_proc_lab: unloaded\n");
}

module_init(kernel_proc_lab_init);
module_exit(kernel_proc_lab_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("zion");
MODULE_DESCRIPTION("Learning kernel module with /proc and character device interfaces");
MODULE_VERSION(LAB_VERSION);
