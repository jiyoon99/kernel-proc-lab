#ifndef KERNEL_PROC_LAB_IOCTL_H
#define KERNEL_PROC_LAB_IOCTL_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define KERNEL_PROC_LAB_IOC_MAGIC 'k'
#define KERNEL_PROC_LAB_ABI_VERSION 4
#define KERNEL_PROC_LAB_MESSAGE_MAX 128
#define KERNEL_PROC_LAB_LOG_CAPACITY 64
#define KERNEL_PROC_LAB_USER_LOG_CAPACITY 64
#define KERNEL_PROC_LAB_COMM_MAX 16

enum kernel_proc_lab_event_type {
	KERNEL_PROC_LAB_EVENT_MESSAGE = 0,
	KERNEL_PROC_LAB_EVENT_WRITE = 1,
	KERNEL_PROC_LAB_EVENT_HEARTBEAT = 2,
	KERNEL_PROC_LAB_EVENT_CLEAR = 3,
	KERNEL_PROC_LAB_EVENT_RESET = 4,
	KERNEL_PROC_LAB_EVENT_STREAM = 5,
	KERNEL_PROC_LAB_EVENT_CONFIG = 6,
};

struct kernel_proc_lab_stats {
	__u64 reads;
	__u64 writes;
	__u64 opens;
	__u64 log_events;
	__u64 heartbeat_ticks;
	__u64 dropped_log_events;
	__u32 heartbeat_enabled;
	__u32 heartbeat_interval_ms;
	__u32 abi_version;
	__u32 log_capacity;
	__u32 message_max;
	__u32 reserved;
};

struct kernel_proc_lab_config {
	__u32 heartbeat_interval_ms;
};

struct kernel_proc_lab_filter {
	__u32 enabled;
	__u32 type_mask;
	__u32 pid;
	__u32 uid;
	char comm[KERNEL_PROC_LAB_COMM_MAX];
};

struct kernel_proc_lab_log_entry {
	__u64 seq;
	__u64 timestamp_ns;
	__u32 type;
	__u32 pid;
	__u32 uid;
	__u32 reserved;
	char comm[KERNEL_PROC_LAB_COMM_MAX];
	char message[KERNEL_PROC_LAB_MESSAGE_MAX];
};

struct kernel_proc_lab_log_snapshot {
	__u32 count;
	struct kernel_proc_lab_log_entry entries[KERNEL_PROC_LAB_USER_LOG_CAPACITY];
};

struct kernel_proc_lab_log_read {
	__u32 abi_version;
	__u32 entry_size;
	__u32 capacity;
	__u32 count;
	__u64 start_seq;
	__u64 latest_seq;
	__u64 entries;
};

#define KERNEL_PROC_LAB_IOC_GET_STATS \
	_IOR(KERNEL_PROC_LAB_IOC_MAGIC, 1, struct kernel_proc_lab_stats)
#define KERNEL_PROC_LAB_IOC_RESET_STATS _IO(KERNEL_PROC_LAB_IOC_MAGIC, 2)
#define KERNEL_PROC_LAB_IOC_CLEAR_MESSAGE _IO(KERNEL_PROC_LAB_IOC_MAGIC, 3)
#define KERNEL_PROC_LAB_IOC_GET_LOG_V2 \
	_IOWR(KERNEL_PROC_LAB_IOC_MAGIC, 4, struct kernel_proc_lab_log_read)
#define KERNEL_PROC_LAB_IOC_START_HEARTBEAT _IO(KERNEL_PROC_LAB_IOC_MAGIC, 5)
#define KERNEL_PROC_LAB_IOC_STOP_HEARTBEAT _IO(KERNEL_PROC_LAB_IOC_MAGIC, 6)
#define KERNEL_PROC_LAB_IOC_GET_CONFIG \
	_IOR(KERNEL_PROC_LAB_IOC_MAGIC, 7, struct kernel_proc_lab_config)
#define KERNEL_PROC_LAB_IOC_SET_CONFIG \
	_IOW(KERNEL_PROC_LAB_IOC_MAGIC, 8, struct kernel_proc_lab_config)
#define KERNEL_PROC_LAB_IOC_START_STREAM _IO(KERNEL_PROC_LAB_IOC_MAGIC, 9)
#define KERNEL_PROC_LAB_IOC_STOP_STREAM _IO(KERNEL_PROC_LAB_IOC_MAGIC, 10)
#define KERNEL_PROC_LAB_IOC_RESET_LOG _IO(KERNEL_PROC_LAB_IOC_MAGIC, 11)
#define KERNEL_PROC_LAB_IOC_RESET_ALL _IO(KERNEL_PROC_LAB_IOC_MAGIC, 12)
#define KERNEL_PROC_LAB_IOC_GET_FILTER \
	_IOR(KERNEL_PROC_LAB_IOC_MAGIC, 13, struct kernel_proc_lab_filter)
#define KERNEL_PROC_LAB_IOC_SET_FILTER \
	_IOW(KERNEL_PROC_LAB_IOC_MAGIC, 14, struct kernel_proc_lab_filter)
#define KERNEL_PROC_LAB_IOC_CLEAR_FILTER _IO(KERNEL_PROC_LAB_IOC_MAGIC, 15)

#endif
