#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/termios.h>
#include <time.h>
#include <unistd.h>

#include "kernel_proc_lab_ioctl.h"

#define DEVICE_PATH "/dev/kernel_proc_lab"
#define PROC_PATH "/proc/kernel_proc_lab"
#define COLLECTOR_LOG_PATH "/var/log/kernel-proc-lab/events.jsonl"
#define LOCAL_COLLECTOR_LOG_PATH "logs/events.jsonl"
#define MESSAGE_SIZE 256
#define MIN_PANEL_WIDTH 64
#define MAX_PANEL_WIDTH 112
#define LOG_DISPLAY_COUNT 6
#define SNAPSHOT_LINE_COUNT 96
#define SNAPSHOT_LINE_SIZE 160
#define SNAPSHOT_VISIBLE_LINES 18
#define MIN_HEARTBEAT_INTERVAL_MS 250
#define MAX_HEARTBEAT_INTERVAL_MS 60000
#define HEARTBEAT_STEP_MS 250
#define CAP_SYS_ADMIN_BIT 21
#define TAINT_PROPRIETARY_MODULE (1 << 0)
#define TAINT_OUT_OF_TREE_MODULE (1 << 12)
#define TAINT_UNSIGNED_MODULE (1 << 13)
#define EXPECTED_LAB_TAINT \
	(TAINT_OUT_OF_TREE_MODULE | TAINT_UNSIGNED_MODULE)
#define PROJECT_METRICS_TTL_SEC 30
#define COLLECTOR_METRICS_TTL_SEC 5
#define SNAPSHOT_REFRESH_TTL_SEC 5
#define COMMAND_TIMEOUT_SEC 15

enum labtop_view {
	VIEW_DASHBOARD = 0,
	VIEW_MODULE,
	VIEW_FILES,
	VIEW_TRACE,
	VIEW_DMESG,
	VIEW_DOCTOR,
	VIEW_SELFTEST,
	VIEW_PROJECT,
	VIEW_OPS,
	VIEW_FILTER,
	VIEW_COLLECTOR,
	VIEW_COUNT,
};

struct cpu_sample {
	unsigned long long idle;
	unsigned long long total;
};

struct system_metrics {
	int cpu_percent;
	int mem_percent;
	unsigned long long mem_used_mb;
	unsigned long long mem_total_mb;
	double load_1m;
	double load_5m;
	double load_15m;
	unsigned long long uptime_seconds;
};

struct module_metrics {
	unsigned long long retained_log_start;
	unsigned long long dropped_log_events;
	unsigned int abi_version;
	unsigned int log_capacity;
	unsigned int message_max;
	unsigned int device_major;
	unsigned int device_minor;
	unsigned int initial_heartbeat_interval_ms;
	unsigned int start_heartbeat_on_load;
	int last_writer_pid;
	char last_writer_comm[32];
	char version[64];
};

struct kernel_health {
	int tainted;
	char state[16];
	char detail[96];
};

struct project_metrics {
	int udev_rule_installed;
	int shellcheck_available;
	int kunit_file_present;
	int changelog_present;
	int ring_helper_present;
	int ring_host_test_present;
	int abi_host_test_present;
	int ci_check_target_present;
	int holders_target_present;
	int dkms_registered;
	int compat_ioctl_compiled;
};

struct collector_metrics {
	int service_active;
	int local_active;
	int log_exists;
	unsigned long long log_size;
	time_t log_mtime;
};

struct counter_rates {
	struct kernel_proc_lab_stats previous;
	struct timespec previous_time;
	double reads_per_sec;
	double writes_per_sec;
	double opens_per_sec;
	double log_per_sec;
	double heartbeat_per_sec;
	int initialized;
};

struct text_snapshot {
	char lines[SNAPSHOT_LINE_COUNT][SNAPSHOT_LINE_SIZE];
	int count;
};

struct cached_project_metrics {
	struct project_metrics metrics;
	struct timespec updated_at;
	int valid;
};

struct cached_collector_metrics {
	struct collector_metrics metrics;
	struct timespec updated_at;
	int valid;
};

static struct termios original_termios;
static volatile sig_atomic_t keep_running = 1;

static int terminal_height(void);
static double elapsed_seconds(const struct timespec *start,
			      const struct timespec *end);

static void restore_terminal(void)
{
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);
	printf("\033[r\033[?25h\033[0m\033[?1049l");
	fflush(stdout);
}

static void handle_signal(int signal_number)
{
	(void)signal_number;
	keep_running = 0;
}

static void setup_terminal(void)
{
	struct termios raw;

	if (tcgetattr(STDIN_FILENO, &original_termios) == 0) {
		raw = original_termios;
		raw.c_lflag &= (tcflag_t) ~(ICANON | ECHO);
		raw.c_cc[VMIN] = 0;
		raw.c_cc[VTIME] = 0;
		tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
		atexit(restore_terminal);
	}

	signal(SIGINT, handle_signal);
	signal(SIGTERM, handle_signal);
	printf("\033[?1049h\033[?25l\033[1;%dr\033[2J\033[H",
	       terminal_height());
}

static int read_key(void)
{
	fd_set read_fds;
	struct timeval timeout = { 0, 0 };
	char key;

	FD_ZERO(&read_fds);
	FD_SET(STDIN_FILENO, &read_fds);

	if (select(STDIN_FILENO + 1, &read_fds, NULL, NULL, &timeout) <= 0)
		return 0;

	if (read(STDIN_FILENO, &key, 1) == 1)
		return key;

	return 0;
}

static void format_key_name(int key, char *buffer, size_t buffer_size)
{
	if (key >= 32 && key <= 126) {
		snprintf(buffer, buffer_size, "'%c'", key);
		return;
	}

	snprintf(buffer, buffer_size, "0x%02x", key & 0xff);
}

static const char *view_name(enum labtop_view view)
{
	switch (view) {
	case VIEW_DASHBOARD:
		return "dashboard";
	case VIEW_MODULE:
		return "module";
	case VIEW_FILES:
		return "files";
	case VIEW_TRACE:
		return "trace";
	case VIEW_DMESG:
		return "dmesg";
	case VIEW_DOCTOR:
		return "doctor";
	case VIEW_SELFTEST:
		return "selftest";
	case VIEW_PROJECT:
		return "project";
	case VIEW_OPS:
		return "ops";
	case VIEW_FILTER:
		return "filter";
	case VIEW_COLLECTOR:
		return "collector";
	default:
		return "unknown";
	}
}

static const char *snapshot_title(enum labtop_view view)
{
	switch (view) {
	case VIEW_MODULE:
		return "Kernel Proc Lab Module";
	case VIEW_FILES:
		return "Kernel Proc Lab Files";
	case VIEW_TRACE:
		return "Kernel Proc Lab Trace";
	case VIEW_DMESG:
		return "Kernel Proc Lab Dmesg";
	case VIEW_DOCTOR:
		return "Kernel Proc Lab Doctor";
	case VIEW_SELFTEST:
		return "Kernel Proc Lab Selftest";
	case VIEW_PROJECT:
		return "Kernel Proc Lab Project";
	case VIEW_OPS:
		return "Kernel Proc Lab Ops";
	case VIEW_FILTER:
		return "Kernel Proc Lab Filter";
	case VIEW_COLLECTOR:
		return "Kernel Proc Lab Collector";
	default:
		return "Kernel Proc Lab";
	}
}

static void clear_snapshot(struct text_snapshot *snapshot)
{
	memset(snapshot, 0, sizeof(*snapshot));
}

static void append_snapshot_line(struct text_snapshot *snapshot, const char *line)
{
	if (snapshot->count >= SNAPSHOT_LINE_COUNT)
		return;

	snprintf(snapshot->lines[snapshot->count], SNAPSHOT_LINE_SIZE, "%.*s",
		 SNAPSHOT_LINE_SIZE - 1, line);
	snapshot->lines[snapshot->count][strcspn(snapshot->lines[snapshot->count],
						 "\n")] = '\0';
	snapshot->count += 1;
}

static void read_file_lines(const char *path, struct text_snapshot *snapshot)
{
	FILE *file = fopen(path, "r");
	char line[SNAPSHOT_LINE_SIZE];

	if (!file) {
		snprintf(line, sizeof(line), "%s: %s", path, strerror(errno));
		append_snapshot_line(snapshot, line);
		return;
	}

	append_snapshot_line(snapshot, path);
	while (snapshot->count < SNAPSHOT_LINE_COUNT &&
	       fgets(line, sizeof(line), file))
		append_snapshot_line(snapshot, line);

	fclose(file);
}

static int command_exit_code(int status)
{
	if (status == 0)
		return 0;
	if (WIFEXITED(status))
		return WEXITSTATUS(status);
	if (WIFSIGNALED(status))
		return 128 + WTERMSIG(status);
	return 1;
}

static void append_command_bytes(struct text_snapshot *snapshot, char *line,
				 size_t *line_len, const char *buffer, size_t length)
{
	size_t index;

	for (index = 0; index < length; index += 1) {
		char ch = buffer[index];

		if (ch == '\n') {
			line[*line_len] = '\0';
			append_snapshot_line(snapshot, line);
			*line_len = 0;
			continue;
		}
		if (*line_len + 1 < SNAPSHOT_LINE_SIZE) {
			line[*line_len] = ch;
			*line_len += 1;
		}
	}
}

static int read_command_lines(const char *command, struct text_snapshot *snapshot)
{
	int pipe_fds[2];
	pid_t pid;
	char line[SNAPSHOT_LINE_SIZE];
	char buffer[512];
	size_t line_len = 0;
	time_t started_at;
	int status = 0;
	int exit_code;

	if (pipe(pipe_fds) != 0) {
		snprintf(line, sizeof(line), "%s: %s", command, strerror(errno));
		append_snapshot_line(snapshot, line);
		return 127;
	}

	pid = fork();
	if (pid < 0) {
		close(pipe_fds[0]);
		close(pipe_fds[1]);
		snprintf(line, sizeof(line), "%s: %s", command, strerror(errno));
		append_snapshot_line(snapshot, line);
		return 127;
	}
	if (pid == 0) {
		close(pipe_fds[0]);
		dup2(pipe_fds[1], STDOUT_FILENO);
		dup2(pipe_fds[1], STDERR_FILENO);
		close(pipe_fds[1]);
		execl("/bin/sh", "sh", "-c", command, (char *)NULL);
		_exit(127);
	}

	close(pipe_fds[1]);
	fcntl(pipe_fds[0], F_SETFL, fcntl(pipe_fds[0], F_GETFL, 0) | O_NONBLOCK);
	started_at = time(NULL);

	for (;;) {
		fd_set read_fds;
		struct timeval timeout = { 0, 100000 };
		int child_status;
		int ready;
		ssize_t bytes_read;

		FD_ZERO(&read_fds);
		FD_SET(pipe_fds[0], &read_fds);
		ready = select(pipe_fds[0] + 1, &read_fds, NULL, NULL, &timeout);
		if (ready > 0 && FD_ISSET(pipe_fds[0], &read_fds)) {
			while ((bytes_read = read(pipe_fds[0], buffer,
						  sizeof(buffer))) > 0) {
				if (snapshot->count < SNAPSHOT_LINE_COUNT)
					append_command_bytes(snapshot, line, &line_len,
							     buffer, (size_t)bytes_read);
			}
		}

		if (waitpid(pid, &child_status, WNOHANG) == pid) {
			status = child_status;
			break;
		}

		if (difftime(time(NULL), started_at) >= COMMAND_TIMEOUT_SEC) {
			kill(pid, SIGTERM);
			usleep(100000);
			if (waitpid(pid, &child_status, WNOHANG) == 0)
				kill(pid, SIGKILL);
			waitpid(pid, &child_status, 0);
			snprintf(line, sizeof(line), "command timed out after %d seconds",
				 COMMAND_TIMEOUT_SEC);
			append_snapshot_line(snapshot, line);
			close(pipe_fds[0]);
			return 124;
		}
	}

	while (snapshot->count < SNAPSHOT_LINE_COUNT) {
		ssize_t bytes_read = read(pipe_fds[0], buffer, sizeof(buffer));

		if (bytes_read <= 0)
			break;
		append_command_bytes(snapshot, line, &line_len, buffer,
				     (size_t)bytes_read);
	}
	close(pipe_fds[0]);

	if (line_len > 0 && snapshot->count < SNAPSHOT_LINE_COUNT) {
		line[line_len] = '\0';
		append_snapshot_line(snapshot, line);
	}

	exit_code = command_exit_code(status);
	if (exit_code != 0 && snapshot->count < SNAPSHOT_LINE_COUNT) {
		snprintf(line, sizeof(line), "command exited with status %d", exit_code);
		append_snapshot_line(snapshot, line);
	}
	return exit_code;
}

static void run_action_view(enum labtop_view *current_view,
			    struct text_snapshot *snapshot,
			    const char *title, const char *command,
			    char *action_status, size_t action_status_size)
{
	int exit_code;

	clear_snapshot(snapshot);
	exit_code = read_command_lines(command, snapshot);
	if (exit_code == 0)
		snprintf(action_status, action_status_size, "%s ok", title);
	else
		snprintf(action_status, action_status_size, "%s failed status %d",
			 title, exit_code);

	if (strcmp(title, "doctor") == 0)
		*current_view = VIEW_DOCTOR;
	else if (strcmp(title, "selftest") == 0)
		*current_view = VIEW_SELFTEST;
	else if (strcmp(title, "ci-check") == 0)
		*current_view = VIEW_PROJECT;
	else if (strcmp(title, "holders") == 0)
		*current_view = VIEW_OPS;
	else if (strcmp(title, "ops") == 0)
		*current_view = VIEW_OPS;
	else
		*current_view = VIEW_OPS;
}

static int monotonic_now(struct timespec *now)
{
	return clock_gettime(CLOCK_MONOTONIC, now);
}

static int cache_expired(const struct timespec *updated_at, int ttl_seconds)
{
	struct timespec now;

	if (monotonic_now(&now) != 0)
		return 1;
	return elapsed_seconds(updated_at, &now) >= (double)ttl_seconds;
}

static int read_kernel_taint(void)
{
	FILE *file = fopen("/proc/sys/kernel/tainted", "r");
	int tainted = 0;

	if (!file)
		return -1;

	if (fscanf(file, "%d", &tainted) != 1)
		tainted = -1;
	fclose(file);
	return tainted;
}

static int process_has_cap_sys_admin(void)
{
	FILE *status = fopen("/proc/self/status", "r");
	char line[160];
	unsigned long long effective;

	if (!status)
		return 0;

	while (fgets(line, sizeof(line), status)) {
		if (sscanf(line, "CapEff:\t%llx", &effective) == 1) {
			fclose(status);
			return (effective & (1ULL << CAP_SYS_ADMIN_BIT)) != 0;
		}
	}

	fclose(status);
	return 0;
}

static void format_kernel_taint(int tainted, char *buffer, size_t buffer_size)
{
	char reasons[64] = "";

	if (tainted < 0) {
		snprintf(buffer, buffer_size, "taint unknown");
		return;
	}
	if (tainted == 0) {
		snprintf(buffer, buffer_size, "clean");
		return;
	}

	if (tainted & TAINT_OUT_OF_TREE_MODULE)
		snprintf(reasons + strlen(reasons), sizeof(reasons) - strlen(reasons),
			 "%sout-of-tree", reasons[0] ? "," : "");
	if (tainted & TAINT_UNSIGNED_MODULE)
		snprintf(reasons + strlen(reasons), sizeof(reasons) - strlen(reasons),
			 "%sunsigned", reasons[0] ? "," : "");
	if (tainted & TAINT_PROPRIETARY_MODULE)
		snprintf(reasons + strlen(reasons), sizeof(reasons) - strlen(reasons),
			 "%sproprietary", reasons[0] ? "," : "");
	if (!reasons[0])
		snprintf(reasons, sizeof(reasons), "flags=%d", tainted);

	if ((tainted & ~EXPECTED_LAB_TAINT) == 0)
		snprintf(buffer, buffer_size, "expected taint: out-of-tree,unsigned");
	else
		snprintf(buffer, buffer_size, "tainted %d (%s)", tainted, reasons);
}

static int path_exists(const char *path)
{
	return access(path, F_OK) == 0;
}

static int command_available(const char *command)
{
	const char *path = getenv("PATH");
	char path_copy[4096];
	char *saveptr = NULL;
	char *dir;

	if (!path || strchr(command, '/'))
		return 0;

	snprintf(path_copy, sizeof(path_copy), "%s", path);
	for (dir = strtok_r(path_copy, ":", &saveptr); dir;
	     dir = strtok_r(NULL, ":", &saveptr)) {
		char candidate[PATH_MAX];

		if (dir[0] == '\0')
			dir = ".";
		snprintf(candidate, sizeof(candidate), "%s/%s", dir, command);
		if (access(candidate, X_OK) == 0)
			return 1;
	}

	return 0;
}

static int command_succeeds(const char *command)
{
	return system(command) == 0;
}

static int make_target_exists(const char *target)
{
	FILE *makefile = fopen("Makefile", "r");
	char line[256];
	size_t target_len = strlen(target);
	int found = 0;

	if (!makefile)
		return 0;

	while (fgets(line, sizeof(line), makefile)) {
		if (strncmp(line, target, target_len) == 0 &&
		    line[target_len] == ':') {
			found = 1;
			break;
		}
	}

	fclose(makefile);
	return found;
}

static void read_collector_metrics(struct collector_metrics *collector)
{
	struct stat st;
	FILE *pid_file;
	long local_pid = 0;

	memset(collector, 0, sizeof(*collector));
	collector->service_active = command_succeeds(
		"systemctl is-active --quiet kernel-proc-lab-collector.service >/dev/null 2>&1");
	pid_file = fopen("logs/collector.pid", "r");
	if (pid_file) {
		if (fscanf(pid_file, "%ld", &local_pid) == 1 && local_pid > 0 &&
		    kill((pid_t)local_pid, 0) == 0)
			collector->local_active = 1;
		fclose(pid_file);
	}
	if (stat(COLLECTOR_LOG_PATH, &st) == 0 ||
	    stat(LOCAL_COLLECTOR_LOG_PATH, &st) == 0) {
		collector->log_exists = 1;
		collector->log_size = (unsigned long long)st.st_size;
		collector->log_mtime = st.st_mtime;
	}
}

static int collector_is_active(const struct collector_metrics *collector)
{
	return collector->service_active || collector->local_active;
}

static void read_project_metrics(struct project_metrics *project)
{
	memset(project, 0, sizeof(*project));
	project->udev_rule_installed =
		path_exists("/etc/udev/rules.d/99-kernel-proc-lab.rules");
	project->shellcheck_available = command_available("shellcheck");
	project->kunit_file_present = path_exists("tests/kernel_proc_lab_kunit.c");
	project->changelog_present = path_exists("CHANGELOG.md");
	project->ring_helper_present = path_exists("kernel_proc_lab_ring.h");
	project->ring_host_test_present = path_exists("tests/ring_host_test.c");
	project->abi_host_test_present = path_exists("tests/abi_host_test.c");
	project->ci_check_target_present = make_target_exists("ci-check");
	project->holders_target_present = make_target_exists("holders");
	project->dkms_registered = path_exists("/var/lib/dkms/kernel-proc-lab");
#ifdef CONFIG_COMPAT
	project->compat_ioctl_compiled = 1;
#else
	project->compat_ioctl_compiled = 0;
#endif
}

static void read_project_metrics_cached(struct cached_project_metrics *cache,
					struct project_metrics *project)
{
	if (!cache->valid || cache_expired(&cache->updated_at,
					   PROJECT_METRICS_TTL_SEC)) {
		read_project_metrics(&cache->metrics);
		if (monotonic_now(&cache->updated_at) == 0)
			cache->valid = 1;
	}

	*project = cache->metrics;
}

static void read_collector_metrics_cached(struct cached_collector_metrics *cache,
					  struct collector_metrics *collector)
{
	if (!cache->valid || cache_expired(&cache->updated_at,
					   COLLECTOR_METRICS_TTL_SEC)) {
		read_collector_metrics(&cache->metrics);
		if (monotonic_now(&cache->updated_at) == 0)
			cache->valid = 1;
	}

	*collector = cache->metrics;
}

static void update_health(const struct kernel_proc_lab_stats *stats,
			  struct kernel_health *health)
{
	health->tainted = read_kernel_taint();

	if (health->tainted > 0) {
		if ((health->tainted & ~EXPECTED_LAB_TAINT) == 0)
			snprintf(health->state, sizeof(health->state), "INFO");
		else
			snprintf(health->state, sizeof(health->state), "WARN");
		format_kernel_taint(health->tainted, health->detail,
				    sizeof(health->detail));
		return;
	}
	if (!stats->heartbeat_enabled) {
		snprintf(health->state, sizeof(health->state), "IDLE");
		snprintf(health->detail, sizeof(health->detail), "heartbeat off");
		return;
	}

	snprintf(health->state, sizeof(health->state), "OK");
	snprintf(health->detail, sizeof(health->detail), "driver responsive");
}

static int read_stats(int fd, struct kernel_proc_lab_stats *stats)
{
	return ioctl(fd, KERNEL_PROC_LAB_IOC_GET_STATS, stats);
}

static int set_heartbeat_interval_fd(int fd, unsigned int interval_ms)
{
	struct kernel_proc_lab_config config = {
		.heartbeat_interval_ms = interval_ms,
	};

	return ioctl(fd, KERNEL_PROC_LAB_IOC_SET_CONFIG, &config);
}

static int write_test_message_fd(int fd)
{
	char message[64];
	time_t now = time(NULL);

	snprintf(message, sizeof(message), "labtop test write %lld",
		 (long long)now);
	return write(fd, message, strlen(message)) < 0 ? -1 : 0;
}

static void format_control_error(char *buffer, size_t buffer_size,
				 const char *action)
{
	if (errno == EPERM || errno == EACCES) {
		snprintf(buffer, buffer_size, "%s needs CAP_SYS_ADMIN; run sudo labtop",
			 action);
		return;
	}

	snprintf(buffer, buffer_size, "%s failed: %s", action, strerror(errno));
}

static int require_control_capability(char *buffer, size_t buffer_size,
				      const char *action)
{
	if (process_has_cap_sys_admin())
		return 1;

	snprintf(buffer, buffer_size, "%s needs CAP_SYS_ADMIN; run sudo labtop",
		 action);
	return 0;
}

static int parse_counter(const char *line, const char *label,
			 unsigned long long *value)
{
	size_t label_len = strlen(label);

	if (strncmp(line, label, label_len) != 0)
		return 0;

	return sscanf(line + label_len, ": %llu", value) == 1;
}

static int read_proc_snapshot(struct kernel_proc_lab_stats *stats,
			      struct kernel_proc_lab_log_snapshot *log_snapshot,
			      struct module_metrics *module,
			      char *message, size_t message_size)
{
	FILE *proc = fopen(PROC_PATH, "r");
	char line[512];
	unsigned long long seq;
	char log_message[KERNEL_PROC_LAB_MESSAGE_MAX];

	if (!proc)
		return -1;

	memset(stats, 0, sizeof(*stats));
	memset(log_snapshot, 0, sizeof(*log_snapshot));
	memset(module, 0, sizeof(*module));
	message[0] = '\0';

	while (fgets(line, sizeof(line), proc)) {
		line[strcspn(line, "\n")] = '\0';

		if (parse_counter(line, "reads", &stats->reads))
			continue;
		if (parse_counter(line, "writes", &stats->writes))
			continue;
		if (parse_counter(line, "opens", &stats->opens))
			continue;
		if (parse_counter(line, "log_events", &stats->log_events))
			continue;
		if (parse_counter(line, "heartbeat_ticks", &stats->heartbeat_ticks))
			continue;
		if (parse_counter(line, "retained_log_start",
				  &module->retained_log_start))
			continue;
		if (parse_counter(line, "dropped_log_events",
				  &module->dropped_log_events)) {
			stats->dropped_log_events = module->dropped_log_events;
			continue;
		}
		if (strncmp(line, "abi_version: ", 13) == 0) {
			unsigned long long value = 0;

			if (sscanf(line + 13, "%llu", &value) == 1) {
				module->abi_version = (unsigned int)value;
				stats->abi_version = (unsigned int)value;
			}
			continue;
		}
		if (strncmp(line, "heartbeat_enabled: ", 19) == 0) {
			unsigned long long enabled = 0;

			if (sscanf(line + 19, "%llu", &enabled) == 1)
				stats->heartbeat_enabled = enabled ? 1 : 0;
			continue;
		}
		if (strncmp(line, "log_capacity: ", 14) == 0) {
			unsigned long long value = 0;

			if (sscanf(line + 14, "%llu", &value) == 1) {
				module->log_capacity = (unsigned int)value;
				stats->log_capacity = (unsigned int)value;
			}
			continue;
		}
		if (strncmp(line, "message_max: ", 13) == 0) {
			unsigned long long value = 0;

			if (sscanf(line + 13, "%llu", &value) == 1) {
				module->message_max = (unsigned int)value;
				stats->message_max = (unsigned int)value;
			}
			continue;
		}
		if (strncmp(line, "device_major: ", 14) == 0) {
			unsigned long long value = 0;

			if (sscanf(line + 14, "%llu", &value) == 1)
				module->device_major = (unsigned int)value;
			continue;
		}
		if (strncmp(line, "device_minor: ", 14) == 0) {
			unsigned long long value = 0;

			if (sscanf(line + 14, "%llu", &value) == 1)
				module->device_minor = (unsigned int)value;
			continue;
		}
		if (strncmp(line, "initial_heartbeat_interval_ms: ", 31) == 0) {
			unsigned long long value = 0;

			if (sscanf(line + 31, "%llu", &value) == 1)
				module->initial_heartbeat_interval_ms =
					(unsigned int)value;
			continue;
		}
		if (strncmp(line, "start_heartbeat_on_load: ", 25) == 0) {
			unsigned long long value = 0;

			if (sscanf(line + 25, "%llu", &value) == 1)
				module->start_heartbeat_on_load = value ? 1 : 0;
			continue;
		}
		if (strncmp(line, "last_writer_pid: ", 17) == 0) {
			int value = 0;

			if (sscanf(line + 17, "%d", &value) == 1)
				module->last_writer_pid = value;
			continue;
		}
		if (strncmp(line, "last_writer_comm: ", 18) == 0) {
			snprintf(module->last_writer_comm,
				 sizeof(module->last_writer_comm), "%.*s",
				 (int)sizeof(module->last_writer_comm) - 1,
				 line + 18);
			continue;
		}
		if (strncmp(line, "version: ", 9) == 0) {
			snprintf(module->version, sizeof(module->version), "%.*s",
				 (int)sizeof(module->version) - 1, line + 9);
			continue;
		}
		if (strncmp(line, "heartbeat_interval_ms: ", 23) == 0) {
			unsigned long long interval_ms = 0;

			if (sscanf(line + 23, "%llu", &interval_ms) == 1)
				stats->heartbeat_interval_ms = (__u32)interval_ms;
			continue;
		}
		if (strncmp(line, "last_message: ", 14) == 0) {
			snprintf(message, message_size, "%.*s", (int)message_size - 1,
				 line + 14);
			continue;
		}
		{
			unsigned long long timestamp_ns;
			unsigned int type;
			unsigned int pid;
			unsigned int uid;
			char comm[KERNEL_PROC_LAB_COMM_MAX];

			if (sscanf(line,
				   " #%llu type=%u ts=%llu pid=%u uid=%u comm=%15s msg=%127[^\n]",
				   &seq, &type, &timestamp_ns, &pid, &uid, comm,
				   log_message) == 7 ||
			    sscanf(line,
				   "  #%llu type=%u ts=%llu pid=%u uid=%u comm=%15s msg=%127[^\n]",
				   &seq, &type, &timestamp_ns, &pid, &uid, comm,
				   log_message) == 7) {
				__u32 index;

				if (log_snapshot->count <
				    KERNEL_PROC_LAB_USER_LOG_CAPACITY) {
					index = log_snapshot->count;
					log_snapshot->entries[index].seq = seq;
					log_snapshot->entries[index].timestamp_ns =
						timestamp_ns;
					log_snapshot->entries[index].type = type;
					log_snapshot->entries[index].pid = pid;
					log_snapshot->entries[index].uid = uid;
					snprintf(log_snapshot->entries[index].comm,
						 sizeof(log_snapshot->entries[index].comm),
						 "%s", comm);
					snprintf(log_snapshot->entries[index].message,
						 sizeof(log_snapshot->entries[index].message),
						 "%s", log_message);
					log_snapshot->count += 1;
				}
				continue;
			}
		}
		if (sscanf(line, " #%llu %127[^\n]", &seq, log_message) == 2 ||
		    sscanf(line, "  #%llu %127[^\n]", &seq, log_message) == 2) {
			__u32 index;

			if (log_snapshot->count < KERNEL_PROC_LAB_USER_LOG_CAPACITY) {
				index = log_snapshot->count;
				log_snapshot->entries[index].seq = seq;
				snprintf(log_snapshot->entries[index].message,
					 sizeof(log_snapshot->entries[index].message),
					 "%s", log_message);
				log_snapshot->count += 1;
			}
			continue;
		}
	}

	fclose(proc);
	return 0;
}

static int read_log_snapshot(int fd, struct kernel_proc_lab_log_snapshot *snapshot)
{
	struct kernel_proc_lab_log_read request;

	memset(snapshot, 0, sizeof(*snapshot));
	memset(&request, 0, sizeof(request));
	request.abi_version = KERNEL_PROC_LAB_ABI_VERSION;
	request.entry_size = sizeof(struct kernel_proc_lab_log_entry);
	request.capacity = KERNEL_PROC_LAB_USER_LOG_CAPACITY;
	request.entries = (__u64)(uintptr_t)snapshot->entries;
	if (ioctl(fd, KERNEL_PROC_LAB_IOC_GET_LOG_V2, &request) < 0)
		return -1;
	snapshot->count = request.count;
	return 0;
}

static void read_module_metrics(struct module_metrics *module)
{
	struct kernel_proc_lab_stats ignored_stats;
	struct kernel_proc_lab_log_snapshot ignored_log;
	char ignored_message[MESSAGE_SIZE];

	if (read_proc_snapshot(&ignored_stats, &ignored_log, module, ignored_message,
			       sizeof(ignored_message)) < 0)
		memset(module, 0, sizeof(*module));
}

static int read_cpu_sample(struct cpu_sample *sample)
{
	FILE *stat = fopen("/proc/stat", "r");
	char cpu_label[8];
	unsigned long long user;
	unsigned long long nice;
	unsigned long long system;
	unsigned long long idle;
	unsigned long long iowait;
	unsigned long long irq;
	unsigned long long softirq;
	unsigned long long steal;

	if (!stat)
		return -1;

	if (fscanf(stat, "%7s %llu %llu %llu %llu %llu %llu %llu %llu", cpu_label,
		   &user, &nice, &system, &idle, &iowait, &irq, &softirq,
		   &steal) != 9) {
		fclose(stat);
		return -1;
	}

	fclose(stat);
	sample->idle = idle + iowait;
	sample->total = user + nice + system + idle + iowait + irq + softirq + steal;
	return 0;
}

static void update_cpu_percent(struct system_metrics *metrics,
			       struct cpu_sample *previous_cpu)
{
	struct cpu_sample current;
	unsigned long long idle_delta;
	unsigned long long total_delta;

	if (read_cpu_sample(&current) < 0)
		return;

	if (previous_cpu->total > 0 && current.total >= previous_cpu->total &&
	    current.idle >= previous_cpu->idle) {
		total_delta = current.total - previous_cpu->total;
		idle_delta = current.idle - previous_cpu->idle;
		if (total_delta > 0)
			metrics->cpu_percent =
				(int)(((total_delta - idle_delta) * 100) / total_delta);
	}

	*previous_cpu = current;
}

static void read_memory_metrics(struct system_metrics *metrics)
{
	FILE *meminfo = fopen("/proc/meminfo", "r");
	char key[64];
	char unit[16];
	unsigned long long value;
	unsigned long long total_kb = 0;
	unsigned long long available_kb = 0;

	if (!meminfo)
		return;

	while (fscanf(meminfo, "%63s %llu %15s", key, &value, unit) == 3) {
		if (strcmp(key, "MemTotal:") == 0)
			total_kb = value;
		else if (strcmp(key, "MemAvailable:") == 0)
			available_kb = value;

		if (total_kb > 0 && available_kb > 0)
			break;
	}

	fclose(meminfo);
	if (total_kb == 0)
		return;

	metrics->mem_total_mb = total_kb / 1024;
	metrics->mem_used_mb = (total_kb - available_kb) / 1024;
	metrics->mem_percent = (int)(((total_kb - available_kb) * 100) / total_kb);
}

static void read_load_metrics(struct system_metrics *metrics)
{
	FILE *loadavg = fopen("/proc/loadavg", "r");

	if (!loadavg)
		return;

	if (fscanf(loadavg, "%lf %lf %lf", &metrics->load_1m, &metrics->load_5m,
		   &metrics->load_15m) != 3) {
		metrics->load_1m = 0;
		metrics->load_5m = 0;
		metrics->load_15m = 0;
	}
	fclose(loadavg);
}

static void read_uptime_metric(struct system_metrics *metrics)
{
	FILE *uptime = fopen("/proc/uptime", "r");
	double seconds;

	if (!uptime)
		return;

	if (fscanf(uptime, "%lf", &seconds) == 1)
		metrics->uptime_seconds = (unsigned long long)seconds;
	fclose(uptime);
}

static void read_system_metrics(struct system_metrics *metrics,
				struct cpu_sample *previous_cpu)
{
	update_cpu_percent(metrics, previous_cpu);
	read_memory_metrics(metrics);
	read_load_metrics(metrics);
	read_uptime_metric(metrics);
}

static double elapsed_seconds(const struct timespec *start,
			      const struct timespec *end)
{
	return (double)(end->tv_sec - start->tv_sec) +
	       (double)(end->tv_nsec - start->tv_nsec) / 1000000000.0;
}

static double counter_rate(unsigned long long current, unsigned long long previous,
			   double elapsed)
{
	unsigned long long delta;

	if (elapsed <= 0.0)
		return 0.0;
	delta = current >= previous ? current - previous : current;
	return (double)delta / elapsed;
}

static void update_counter_rates(const struct kernel_proc_lab_stats *stats,
				 struct counter_rates *rates)
{
	struct timespec now;
	double elapsed;

	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
		return;

	if (!rates->initialized) {
		rates->previous = *stats;
		rates->previous_time = now;
		rates->initialized = 1;
		return;
	}

	elapsed = elapsed_seconds(&rates->previous_time, &now);
	rates->reads_per_sec = counter_rate(stats->reads, rates->previous.reads,
					    elapsed);
	rates->writes_per_sec = counter_rate(stats->writes, rates->previous.writes,
					     elapsed);
	rates->opens_per_sec = counter_rate(stats->opens, rates->previous.opens,
					    elapsed);
	rates->log_per_sec = counter_rate(stats->log_events,
					  rates->previous.log_events, elapsed);
	rates->heartbeat_per_sec = counter_rate(stats->heartbeat_ticks,
						rates->previous.heartbeat_ticks,
						elapsed);
	rates->previous = *stats;
	rates->previous_time = now;
}

static int clamp_int(int value, int min, int max)
{
	if (value < min)
		return min;
	if (value > max)
		return max;
	return value;
}

static int terminal_width(void)
{
	struct winsize size;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0)
		return size.ws_col;

	return 80;
}

static int terminal_height(void)
{
	struct winsize size;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_row > 0)
		return size.ws_row;

	return 24;
}

static int panel_width(void)
{
	int columns = terminal_width();
	int width = columns - 4;

	if (width < 20)
		width = columns > 2 ? columns - 2 : columns;
	if (width > MAX_PANEL_WIDTH)
		width = MAX_PANEL_WIDTH;
	if (width < MIN_PANEL_WIDTH && columns >= MIN_PANEL_WIDTH + 4)
		width = MIN_PANEL_WIDTH;
	if (width < 10)
		width = 10;
	return width;
}

static void constrain_scroll_region(void)
{
	printf("\033[1;%dr", terminal_height());
}

static void repeat_text(const char *text, int count)
{
	for (int index = 0; index < count; index += 1)
		printf("%s", text);
}

static void draw_border(const char *left, const char *fill, const char *right,
			int width)
{
	printf("\033[38;5;39m%s", left);
	repeat_text(fill, width - 2);
	printf("%s\033[0m\n", right);
}

static void draw_text_row(const char *label, const char *value, int width)
{
	int inner = width - 2;
	int label_width = 9;
	int value_width = inner - label_width - 3;

	if (value_width < 1)
		value_width = 1;

	printf("\033[38;5;39m│\033[0m \033[38;5;111m%-*s\033[0m "
	       "\033[1;97m%-*.*s\033[0m \033[38;5;39m│\033[0m\n",
	       label_width, label, value_width, value_width, value);
}

static void draw_title_row(const char *title, const char *time_text, int width)
{
	int inner = width - 2;
	int title_len = (int)strlen(title);
	int time_len = (int)strlen(time_text);
	int gap = inner - title_len - time_len - 2;

	if (gap < 1)
		gap = 1;

	printf("\033[38;5;39m│\033[0m \033[1;97m%s\033[0m", title);
	repeat_text(" ", gap);
	printf("\033[38;5;245m%s\033[0m \033[38;5;39m│\033[0m\n", time_text);
}

static void draw_section_title(const char *title, int width)
{
	int inner = width - 2;
	int title_len = (int)strlen(title);
	int fill = inner - title_len - 3;

	if (fill < 1)
		fill = 1;

	printf("\033[38;5;39m├─\033[0m \033[1;97m%s\033[0m ", title);
	printf("\033[38;5;39m");
	repeat_text("─", fill);
	printf("┤\033[0m\n");
}

static void draw_bar_row(const char *label, unsigned long long value,
			 unsigned long long max_value, int width, const char *color)
{
	char value_text[32];
	int inner = width - 2;
	int label_width = 8;
	int value_width = 10;
	int bar_width = inner - label_width - value_width - 4;
	int filled = 0;

	if (max_value > 0)
		filled = (int)((value * (unsigned long long)bar_width) / max_value);
	filled = clamp_int(filled, 0, bar_width);
	snprintf(value_text, sizeof(value_text), "%llu", value);

	printf("\033[38;5;39m│\033[0m \033[1;37m%-*s\033[0m %s",
	       label_width, label, color);
	for (int index = 0; index < bar_width; index += 1)
		printf("%s", index < filled ? "█" : "░");
	printf("\033[0m \033[1;36m%*s\033[0m \033[38;5;39m│\033[0m\n",
	       value_width, value_text);
}

static void draw_percent_row(const char *label, int percent, const char *detail,
			     int width, const char *color)
{
	char value_text[64];
	int inner = width - 2;
	int label_width = 8;
	int value_width = 18;
	int bar_width = inner - label_width - value_width - 4;
	int filled;

	percent = clamp_int(percent, 0, 100);
	filled = (percent * bar_width) / 100;
	snprintf(value_text, sizeof(value_text), "%3d%% %s", percent, detail);

	printf("\033[38;5;39m│\033[0m \033[1;37m%-*s\033[0m %s",
	       label_width, label, color);
	for (int index = 0; index < bar_width; index += 1)
		printf("%s", index < filled ? "█" : "░");
	printf("\033[0m \033[1;36m%*.*s\033[0m \033[38;5;39m│\033[0m\n",
	       value_width, value_width, value_text);
}

static void draw_message_row(const char *message, int width)
{
	int inner = width - 2;
	int value_width = inner - 2;

	printf("\033[38;5;39m│\033[0m \033[48;5;236m\033[38;5;230m%-*.*s"
	       "\033[0m \033[38;5;39m│\033[0m\n",
	       value_width, value_width, message[0] ? message : "(empty)");
}

static const char *state_color(const char *state)
{
	if (strcmp(state, "OK") == 0 || strcmp(state, "READY") == 0)
		return "\033[38;5;120m";
	if (strcmp(state, "INFO") == 0)
		return "\033[38;5;81m";
	if (strcmp(state, "IDLE") == 0)
		return "\033[38;5;220m";
	return "\033[38;5;203m";
}

static void draw_status_row(const char *label, const char *state,
			    const char *detail, int width)
{
	char value[160];
	int inner = width - 2;
	int label_width = 9;
	int value_width = inner - label_width - 3;

	if (value_width < 1)
		value_width = 1;

	snprintf(value, sizeof(value), "%.16s | %.120s", state, detail);
	printf("\033[38;5;39m│\033[0m \033[38;5;111m%-*s\033[0m "
	       "%s%-*.*s\033[0m \033[38;5;39m│\033[0m\n",
	       label_width, label, state_color(state), value_width, value_width,
	       value);
}

static const char *log_event_type(const char *message)
{
	if (strncmp(message, "heartbeat", 9) == 0)
		return "HEART";
	if (strstr(message, "write") || strstr(message, "test"))
		return "WRITE";
	if (strstr(message, "reset"))
		return "RESET";
	if (strstr(message, "clear"))
		return "CLEAR";
	return "MSG";
}

static const char *log_entry_type_name(const struct kernel_proc_lab_log_entry *entry)
{
	switch (entry->type) {
	case KERNEL_PROC_LAB_EVENT_WRITE:
		return "WRITE";
	case KERNEL_PROC_LAB_EVENT_HEARTBEAT:
		return "HEART";
	case KERNEL_PROC_LAB_EVENT_CLEAR:
		return "CLEAR";
	case KERNEL_PROC_LAB_EVENT_RESET:
		return "RESET";
	case KERNEL_PROC_LAB_EVENT_STREAM:
		return "STREAM";
	case KERNEL_PROC_LAB_EVENT_CONFIG:
		return "CONFIG";
	case KERNEL_PROC_LAB_EVENT_MESSAGE:
	default:
		return log_event_type(entry->message);
	}
}

static const char *log_entry_color(const struct kernel_proc_lab_log_entry *entry)
{
	switch (entry->type) {
	case KERNEL_PROC_LAB_EVENT_WRITE:
		return "\033[38;5;214m";
	case KERNEL_PROC_LAB_EVENT_HEARTBEAT:
		return "\033[38;5;120m";
	case KERNEL_PROC_LAB_EVENT_CLEAR:
	case KERNEL_PROC_LAB_EVENT_RESET:
		return "\033[38;5;220m";
	case KERNEL_PROC_LAB_EVENT_STREAM:
		return "\033[38;5;81m";
	case KERNEL_PROC_LAB_EVENT_CONFIG:
		return "\033[38;5;177m";
	default:
		return "\033[38;5;250m";
	}
}

static void draw_log_row(const struct kernel_proc_lab_log_entry *entry, int width)
{
	char line[KERNEL_PROC_LAB_MESSAGE_MAX + 32];
	int inner = width - 2;
	int label_width = 9;
	int value_width = inner - label_width - 3;

	if (value_width < 1)
		value_width = 1;

	if (entry->seq == 0)
		snprintf(line, sizeof(line), "(empty)");
	else
		snprintf(line, sizeof(line), "%s #%llu pid=%u %.16s: %.80s",
			 log_entry_type_name(entry), (unsigned long long)entry->seq,
			 entry->pid, entry->comm[0] ? entry->comm : "-",
			 entry->message);

	printf("\033[38;5;39m│\033[0m \033[38;5;111m%-*s\033[0m "
	       "%s%-*.*s\033[0m \033[38;5;39m│\033[0m\n",
	       label_width, "event", log_entry_color(entry), value_width,
	       value_width, line);
}

static void draw_view_header(const char *title, enum labtop_view view, int width)
{
	time_t now = time(NULL);
	struct tm *local = localtime(&now);
	char time_text[32] = { 0 };
	char active_text[64];

	if (local)
		strftime(time_text, sizeof(time_text), "%H:%M:%S", local);
	snprintf(active_text, sizeof(active_text), "6 doctor | 7 test | 8 proj | 9 ops | active %s",
		 view_name(view));

	constrain_scroll_region();
	printf("\033[H");
	printf("  ");
	draw_border("╭", "─", "╮", width);
	printf("  ");
	draw_title_row(title, time_text, width);
	printf("  ");
	draw_text_row("tabs", "1 dash | 2 mod | 3 files | 4 trace | 5 dmesg", width);
	printf("  ");
	draw_text_row("tabs", active_text, width);
}

static void draw_snapshot_view(const char *title, enum labtop_view view,
			       const struct text_snapshot *snapshot,
			       int scroll_offset, const char *action_status)
{
	int width = panel_width();
	int index;
	int visible_lines = clamp_int(terminal_height() - 12, 1, SNAPSHOT_VISIBLE_LINES);
	int end = scroll_offset + visible_lines;
	char scroll_text[64];

	if (end > snapshot->count)
		end = snapshot->count;
	snprintf(scroll_text, sizeof(scroll_text), "%d-%d/%d", snapshot->count == 0 ? 0 :
		 scroll_offset + 1, end, snapshot->count);

	draw_view_header(title, view, width);
	printf("  ");
	draw_text_row("scroll", scroll_text, width);
	printf("  ");
	draw_section_title("snapshot", width);
	if (snapshot->count == 0) {
		printf("  ");
		draw_text_row("line", "(empty)", width);
	} else {
		for (index = scroll_offset; index < end; index += 1) {
			printf("  ");
			draw_text_row("line", snapshot->lines[index], width);
		}
	}
	printf("  ");
	draw_section_title("actions", width);
	printf("  ");
	draw_text_row("keys", "1 dash | j/k scroll | d doctor | i ci | m holders | q quit",
		      width);
	printf("  ");
	draw_text_row("status", action_status, width);
	printf("  ");
	draw_border("╰", "─", "╯", width);
	printf("\033[J");
	fflush(stdout);
}

static void read_module_view(struct text_snapshot *snapshot)
{
	clear_snapshot(snapshot);
	read_command_lines("modinfo ./kernel_proc_lab.ko 2>&1", snapshot);
}

static void read_files_view(struct text_snapshot *snapshot)
{
	clear_snapshot(snapshot);
	read_file_lines(PROC_PATH, snapshot);
	if (snapshot->count < SNAPSHOT_LINE_COUNT)
		read_command_lines("find -L /sys/class/kernel_proc_lab/kernel_proc_lab -maxdepth 1 -type f -print -exec cat {} \\; 2>&1",
				   snapshot);
}

static void read_trace_view(struct text_snapshot *snapshot)
{
	clear_snapshot(snapshot);
	read_command_lines("find /sys/kernel/tracing/events/kernel_proc_lab -maxdepth 2 -type f -name format -print 2>&1",
			   snapshot);
}

static void read_dmesg_view(struct text_snapshot *snapshot)
{
	clear_snapshot(snapshot);
	read_command_lines("dmesg | grep kernel_proc_lab | tail -18 2>&1", snapshot);
}

static void read_doctor_view(struct text_snapshot *snapshot)
{
	clear_snapshot(snapshot);
	read_command_lines("./scripts/doctor.sh 2>&1", snapshot);
}

static void read_selftest_view(struct text_snapshot *snapshot, int run_test)
{
	clear_snapshot(snapshot);
	if (run_test)
		read_command_lines("./scripts/selftest.sh 2>&1", snapshot);
	else {
		append_snapshot_line(snapshot, "selftest is available");
		append_snapshot_line(snapshot, "press t to run ./scripts/selftest.sh");
		append_snapshot_line(snapshot, "press i to run make ci-check");
		append_snapshot_line(snapshot, "");
		read_command_lines("./usercli doctor 2>&1", snapshot);
	}
}

static void read_project_view(struct text_snapshot *snapshot)
{
	clear_snapshot(snapshot);
	append_snapshot_line(snapshot, "project files");
	read_command_lines("test -f CHANGELOG.md && sed -n '1,80p' CHANGELOG.md || true",
			   snapshot);
	if (snapshot->count < SNAPSHOT_LINE_COUNT)
		read_command_lines("printf '\\nring helper:\\n'; test -f kernel_proc_lab_ring.h && sed -n '1,120p' kernel_proc_lab_ring.h || true",
				   snapshot);
	if (snapshot->count < SNAPSHOT_LINE_COUNT)
		read_command_lines("printf '\\nhost ring tests:\\n'; test -f tests/ring_host_test.c && sed -n '1,140p' tests/ring_host_test.c || true",
				   snapshot);
	if (snapshot->count < SNAPSHOT_LINE_COUNT)
		read_command_lines("printf '\\nKUnit tests:\\n'; test -f tests/kernel_proc_lab_kunit.c && sed -n '1,120p' tests/kernel_proc_lab_kunit.c || true",
				   snapshot);
	if (snapshot->count < SNAPSHOT_LINE_COUNT)
		read_command_lines("printf '\\npackaging:\\n'; test -f docs/packaging.md && sed -n '1,80p' docs/packaging.md || true",
				   snapshot);
}

static void read_filter_view(struct text_snapshot *snapshot)
{
	clear_snapshot(snapshot);
	read_command_lines("./usercli filter show 2>&1", snapshot);
	if (snapshot->count < SNAPSHOT_LINE_COUNT) {
		append_snapshot_line(snapshot, "");
		append_snapshot_line(snapshot, "examples");
		append_snapshot_line(snapshot, "sudo ./usercli filter set type=1");
		append_snapshot_line(snapshot, "sudo ./usercli filter set uid=1000 comm=usercli");
		append_snapshot_line(snapshot, "sudo ./usercli filter clear");
	}
}

static void read_collector_view(struct text_snapshot *snapshot)
{
	clear_snapshot(snapshot);
	append_snapshot_line(snapshot, "collector service");
	read_command_lines("systemctl --no-pager --plain status kernel-proc-lab-collector.service 2>&1 | sed -n '1,18p'",
			   snapshot);
	if (snapshot->count < SNAPSHOT_LINE_COUNT)
		read_command_lines("printf '\\njsonl tail:\\n'; test -f /var/log/kernel-proc-lab/events.jsonl && tail -20 /var/log/kernel-proc-lab/events.jsonl || echo '/var/log/kernel-proc-lab/events.jsonl missing'",
				   snapshot);
}

static void read_ops_view(struct text_snapshot *snapshot)
{
	clear_snapshot(snapshot);
	read_command_lines("./scripts/ops-check.sh 2>&1", snapshot);
}

static void refresh_snapshot_view(enum labtop_view view, struct text_snapshot *snapshot)
{
	switch (view) {
	case VIEW_MODULE:
		read_module_view(snapshot);
		break;
	case VIEW_FILES:
		read_files_view(snapshot);
		break;
	case VIEW_TRACE:
		read_trace_view(snapshot);
		break;
	case VIEW_DMESG:
		read_dmesg_view(snapshot);
		break;
	case VIEW_DOCTOR:
		read_doctor_view(snapshot);
		break;
	case VIEW_SELFTEST:
		read_selftest_view(snapshot, 0);
		break;
	case VIEW_PROJECT:
		read_project_view(snapshot);
		break;
	case VIEW_OPS:
		read_ops_view(snapshot);
		break;
	case VIEW_FILTER:
		read_filter_view(snapshot);
		break;
	case VIEW_COLLECTOR:
		read_collector_view(snapshot);
		break;
	default:
		clear_snapshot(snapshot);
		break;
	}
}

static void format_uptime(unsigned long long seconds, char *buffer,
			  size_t buffer_size)
{
	unsigned long long days = seconds / 86400;
	unsigned long long hours = (seconds % 86400) / 3600;
	unsigned long long minutes = (seconds % 3600) / 60;

	if (days > 0)
		snprintf(buffer, buffer_size, "%llud %lluh %llum", days, hours,
			 minutes);
	else
		snprintf(buffer, buffer_size, "%lluh %llum", hours, minutes);
}

static void draw_dashboard(const struct kernel_proc_lab_stats *stats,
			   const struct kernel_proc_lab_log_snapshot *log_snapshot,
			   const struct module_metrics *module,
			   const struct kernel_health *health,
			   const struct system_metrics *metrics,
			   const struct counter_rates *rates,
			   const struct project_metrics *project,
				   const struct collector_metrics *collector,
				   const struct kernel_proc_lab_filter *filter,
				   const char *message,
				   const char *source, const char *action_status,
				   int can_control)
{
	time_t now = time(NULL);
	struct tm *local = localtime(&now);
	char time_text[32] = { 0 };
	char status_text[64];
	char health_text[128];
	char refresh_text[96];
	char interval_text[64];
	char abi_text[96];
	char retained_text[96];
	char device_text[96];
	char writer_text[96];
	char mem_detail[64];
	char load_detail[64];
	char uptime_text[64];
	char project_text[128];
	char tooling_text[128];
	char tests_text[128];
	char ops_text[128];
	char readiness_state[16];
	char readiness_detail[160];
	char next_action_text[160];
	char install_text[128];
	char collector_text[128];
	char filter_text[128];
	char io_rate_text[128];
	char event_rate_text[128];
	char mode_text[128];
	unsigned long long max_value = stats->reads;
	int width = panel_width();
	int max_lines = clamp_int(terminal_height() - 3, 8, 200);
	int body_lines = clamp_int(max_lines - 5, 4, 195);
	int lines = 0;
	int compact = max_lines < 40;
	int tiny = max_lines < 32;
	int log_rows = 1;
	int reserved_log_lines = log_rows + 1;
	int draw_limit = body_lines;
	int pre_log_limit = body_lines - reserved_log_lines;

	if (pre_log_limit < 10)
		pre_log_limit = 10;

#define DRAW_ONE(call)                                                          \
	do {                                                                    \
		if (lines < draw_limit) {                                      \
			printf("  ");                                           \
			call;                                                   \
			lines += 1;                                             \
		}                                                               \
	} while (0)

	if (stats->writes > max_value)
		max_value = stats->writes;
	if (stats->opens > max_value)
		max_value = stats->opens;
	if (stats->log_events > max_value)
		max_value = stats->log_events;
	if (stats->heartbeat_ticks > max_value)
		max_value = stats->heartbeat_ticks;
	if (max_value == 0)
		max_value = 1;

	if (local)
		strftime(time_text, sizeof(time_text), "%H:%M:%S", local);
	snprintf(status_text, sizeof(status_text), "module loaded | %s | heartbeat %s",
		 strcmp(source, DEVICE_PATH) == 0 ? "device ok" : "proc fallback",
		 stats->heartbeat_enabled ? "on" : "off");
	snprintf(health_text, sizeof(health_text), "%s | %s",
		 health->state, health->detail);
	if (stats->abi_version != 0 &&
		 stats->abi_version != KERNEL_PROC_LAB_ABI_VERSION)
		snprintf(ops_text, sizeof(ops_text), "WARN ABI tool=%u driver=%u",
			 KERNEL_PROC_LAB_ABI_VERSION, stats->abi_version);
	else if (!collector_is_active(collector))
		snprintf(ops_text, sizeof(ops_text), "WARN collector inactive | logging off");
	else
		snprintf(ops_text, sizeof(ops_text), "READY | ABI ok | logging on");
	if (stats->abi_version != KERNEL_PROC_LAB_ABI_VERSION) {
		snprintf(readiness_state, sizeof(readiness_state), "WARN");
		snprintf(readiness_detail, sizeof(readiness_detail),
			 "ABI mismatch tool=%u driver=%u",
			 KERNEL_PROC_LAB_ABI_VERSION, stats->abi_version);
		snprintf(next_action_text, sizeof(next_action_text),
			 "run: make reload");
	} else if (!collector_is_active(collector)) {
		snprintf(readiness_state, sizeof(readiness_state), "WARN");
		snprintf(readiness_detail, sizeof(readiness_detail),
			 "collector inactive | events not persisted");
		snprintf(next_action_text, sizeof(next_action_text),
			 "run: make install-collector-service");
	} else {
		snprintf(readiness_state, sizeof(readiness_state), "READY");
		snprintf(readiness_detail, sizeof(readiness_detail),
			 "ABI 4 | DKMS %s | CI %s | collector %s",
			 project->dkms_registered ? "installed" : "missing",
			 project->ci_check_target_present ? "ready" : "missing",
			 collector_is_active(collector) ? "active" : "inactive");
		snprintf(next_action_text, sizeof(next_action_text),
			 "monitoring ok | d doctor | w test write");
	}
	snprintf(interval_text, sizeof(interval_text), "heartbeat interval %u ms",
		 stats->heartbeat_interval_ms);
	snprintf(abi_text, sizeof(abi_text), "abi %u | log cap %u | message max %u",
		 module->abi_version ? module->abi_version : stats->abi_version,
		 module->log_capacity ? module->log_capacity : stats->log_capacity,
		 module->message_max ? module->message_max : stats->message_max);
	snprintf(retained_text, sizeof(retained_text), "events %llu | retained from %llu",
		 stats->log_events, module->retained_log_start);
	snprintf(device_text, sizeof(device_text), "major %u | minor %u",
		 module->device_major, module->device_minor);
	snprintf(writer_text, sizeof(writer_text), "pid %d | comm %s",
		 module->last_writer_pid,
		 module->last_writer_comm[0] ? module->last_writer_comm : "none");
	if (can_control)
		snprintf(refresh_text, sizeof(refresh_text),
			 "w write | h heart | r stats | l log | d doctor | q quit");
	else
		snprintf(refresh_text, sizeof(refresh_text),
			 "w write | d doctor | g logs | m holders | q quit");
	if (strcmp(source, DEVICE_PATH) != 0)
		snprintf(mode_text, sizeof(mode_text),
			 "read-only fallback | load device node for controls");
	else if (can_control)
		snprintf(mode_text, sizeof(mode_text),
			 "control mode | CAP_SYS_ADMIN available");
	else
		snprintf(mode_text, sizeof(mode_text),
			 "monitor mode | sudo labtop enables h/r/l/a/c/+/-");
	snprintf(project_text, sizeof(project_text), "ring %s | ring-test %s | abi-test %s",
		 project->ring_helper_present ? "yes" : "no",
		 project->ring_host_test_present ? "yes" : "no",
		 project->abi_host_test_present ? "yes" : "no");
	snprintf(tooling_text, sizeof(tooling_text), "udev %s | shellcheck %s | holders %s",
		 project->udev_rule_installed ? "installed" : "missing",
		 project->shellcheck_available ? "found" : "missing",
		 project->holders_target_present ? "target" : "missing");
	snprintf(tests_text, sizeof(tests_text), "selftest yes | KUnit %s | compat %s | changelog %s",
		 project->kunit_file_present ? "yes" : "no",
		 project->compat_ioctl_compiled ? "built" : "not built",
		 project->changelog_present ? "yes" : "no");
	snprintf(install_text, sizeof(install_text), "DKMS %s | command %s | CI %s",
		 project->dkms_registered ? "installed" : "missing",
		 path_exists("/usr/local/bin/labtop") ? "installed" : "local",
		 project->ci_check_target_present ? "target" : "missing");
	snprintf(collector_text, sizeof(collector_text), "svc %s | jsonl %s | %llu bytes",
		 collector->service_active ? "service" :
		 collector->local_active ? "local" : "inactive",
		 collector->log_exists ? "present" : "missing",
		 collector->log_size);
	snprintf(filter_text, sizeof(filter_text), "%s | mask 0x%x | pid %u | uid %u | comm %s",
		 filter->enabled ? "enabled" : "disabled", filter->type_mask,
		 filter->pid, filter->uid, filter->comm[0] ? filter->comm : "any");
	snprintf(mem_detail, sizeof(mem_detail), "%llu/%llu MB", metrics->mem_used_mb,
		 metrics->mem_total_mb);
	snprintf(load_detail, sizeof(load_detail), "%.2f %.2f %.2f",
		 metrics->load_1m, metrics->load_5m, metrics->load_15m);
	snprintf(io_rate_text, sizeof(io_rate_text),
		 "read %.1f/s | write %.1f/s | open %.1f/s",
		 rates->reads_per_sec, rates->writes_per_sec, rates->opens_per_sec);
	snprintf(event_rate_text, sizeof(event_rate_text),
		 "log %.1f/s | heartbeat %.1f/s",
		 rates->log_per_sec, rates->heartbeat_per_sec);
	format_uptime(metrics->uptime_seconds, uptime_text, sizeof(uptime_text));

	constrain_scroll_region();
	printf("\033[H");
	DRAW_ONE(draw_border("╭", "─", "╮", width));
	DRAW_ONE(draw_title_row("Kernel Proc Lab Monitor", time_text, width));
	DRAW_ONE(draw_text_row("tabs", "1 dash | 2 mod | 3 files | 4 trace | 5 dmesg", width));
	if (!tiny)
		DRAW_ONE(draw_text_row("tabs", "6 doctor | 7 test | 8 proj | 9 ops | f filter | g logs", width));
	DRAW_ONE(draw_status_row("ready", readiness_state, readiness_detail, width));
	if (!tiny)
		DRAW_ONE(draw_text_row("next", next_action_text, width));

	draw_limit = pre_log_limit;
	DRAW_ONE(draw_section_title("module", width));
	if (!tiny)
		DRAW_ONE(draw_text_row("source", source, width));
	DRAW_ONE(draw_text_row("status", status_text, width));
	if (!tiny)
		DRAW_ONE(draw_text_row("health", health_text, width));
	DRAW_ONE(draw_text_row("ops", ops_text, width));
	if (!tiny)
		DRAW_ONE(draw_text_row("config", interval_text, width));
	if (!compact) {
		DRAW_ONE(draw_text_row("driver", "chrdev | ioctl | poll | stream", width));
		DRAW_ONE(draw_text_row("exports", "/proc | /dev | sysfs | debugfs | trace", width));
		DRAW_ONE(draw_section_title("kernel", width));
		DRAW_ONE(draw_text_row("abi", abi_text, width));
		DRAW_ONE(draw_text_row("retained", retained_text, width));
		DRAW_ONE(draw_text_row("drops", module->dropped_log_events ?
				       "normal ring rotation" : "none", width));
		DRAW_ONE(draw_text_row("device", device_text, width));
		DRAW_ONE(draw_text_row("writer", writer_text, width));
		DRAW_ONE(draw_text_row("version", module->version[0] ?
				       module->version : "(unknown)", width));
	} else if (!tiny) {
		DRAW_ONE(draw_text_row("abi", abi_text, width));
	}
	if (!tiny) {
		DRAW_ONE(draw_section_title("project", width));
		if (!compact) {
			DRAW_ONE(draw_text_row("runtime", project_text, width));
			DRAW_ONE(draw_text_row("tooling", tooling_text, width));
			DRAW_ONE(draw_text_row("tests", tests_text, width));
			DRAW_ONE(draw_text_row("install", install_text, width));
		}
		DRAW_ONE(draw_text_row("collect", collector_text, width));
		DRAW_ONE(draw_text_row("filter", filter_text, width));
	} else if (compact) {
		DRAW_ONE(draw_text_row("collect", collector_text, width));
	}
	if (!tiny) {
		DRAW_ONE(draw_section_title("system", width));
		DRAW_ONE(draw_percent_row("cpu", metrics->cpu_percent, "usage", width,
					  "\033[38;5;81m"));
		if (!compact)
			DRAW_ONE(draw_percent_row("memory", metrics->mem_percent,
						  mem_detail, width,
						  "\033[38;5;214m"));
		DRAW_ONE(draw_text_row("load", load_detail, width));
		if (!compact)
			DRAW_ONE(draw_text_row("uptime", uptime_text, width));
	}
	DRAW_ONE(draw_section_title("counters", width));
	DRAW_ONE(draw_bar_row("reads", stats->reads, max_value, width,
			      "\033[38;5;81m"));
	DRAW_ONE(draw_bar_row("writes", stats->writes, max_value, width,
			      "\033[38;5;214m"));
	DRAW_ONE(draw_bar_row("opens", stats->opens, max_value, width,
			      "\033[38;5;120m"));
	DRAW_ONE(draw_bar_row("log", stats->log_events, max_value, width,
			      "\033[38;5;177m"));
	DRAW_ONE(draw_bar_row("heart", stats->heartbeat_ticks, max_value, width,
			      "\033[38;5;120m"));
	if (!compact) {
		DRAW_ONE(draw_section_title("activity", width));
		DRAW_ONE(draw_text_row("io/s", io_rate_text, width));
		DRAW_ONE(draw_text_row("event/s", event_rate_text, width));
	}
	if (!tiny) {
		DRAW_ONE(draw_section_title("last message", width));
		DRAW_ONE(draw_message_row(message, width));
	}

	draw_limit = body_lines;
	DRAW_ONE(draw_section_title("recent log", width));
	if (log_snapshot->count == 0) {
		struct kernel_proc_lab_log_entry empty = { 0 };

		DRAW_ONE(draw_log_row(&empty, width));
	} else {
		__u32 start = 0;
		__u32 index;

		if (log_snapshot->count > (__u32)log_rows)
			start = log_snapshot->count - (__u32)log_rows;
		for (index = start; index < log_snapshot->count; index += 1) {
			DRAW_ONE(draw_log_row(&log_snapshot->entries[index], width));
		}
	}
	printf("  ");
	draw_section_title("actions", width);
	printf("  ");
	draw_text_row("keys", refresh_text, width);
	printf("  ");
	draw_text_row("mode", mode_text, width);
	printf("  ");
	draw_text_row("status", action_status, width);
	printf("  ");
	draw_border("╰", "─", "╯", width);
	printf("\033[J");
	fflush(stdout);
#undef DRAW_ONE
}

int main(void)
{
	struct kernel_proc_lab_stats stats = { 0 };
	struct kernel_proc_lab_log_snapshot log_snapshot = { 0 };
	struct module_metrics module = { 0 };
	struct kernel_health health = { 0 };
	struct system_metrics metrics = { 0 };
	struct counter_rates rates = { 0 };
	struct project_metrics project = { 0 };
	struct collector_metrics collector = { 0 };
	struct cached_project_metrics project_cache = { 0 };
	struct cached_collector_metrics collector_cache = { 0 };
	struct kernel_proc_lab_filter filter = { 0 };
	struct cpu_sample previous_cpu = { 0 };
	struct text_snapshot snapshot = { 0 };
	enum labtop_view current_view = VIEW_DASHBOARD;
	enum labtop_view snapshot_view = VIEW_COUNT;
	struct timespec snapshot_updated_at = { 0 };
	int snapshot_valid = 0;
	int scroll_offset = 0;
	char message[MESSAGE_SIZE] = { 0 };
	char action_status[128] = "ready";
	int fd = open(DEVICE_PATH, O_RDWR);
	const char *source = DEVICE_PATH;
	int can_control = process_has_cap_sys_admin();

	if (fd < 0) {
		if (access(PROC_PATH, R_OK) != 0) {
			fprintf(stderr, "failed to open %s: %s\n", DEVICE_PATH,
				strerror(errno));
			fprintf(stderr, "failed to read %s: %s\n", PROC_PATH,
				strerror(errno));
			fprintf(stderr, "load the module first: ./demo.sh or make load\n");
			return 1;
		}
		source = PROC_PATH;
	}

	setup_terminal();

	while (keep_running) {
		if (fd >= 0) {
			if (read_stats(fd, &stats) < 0) {
				fprintf(stderr, "ioctl stats failed: %s\n", strerror(errno));
				break;
			}

			if (read_log_snapshot(fd, &log_snapshot) < 0)
				memset(&log_snapshot, 0, sizeof(log_snapshot));
			if (ioctl(fd, KERNEL_PROC_LAB_IOC_GET_FILTER, &filter) < 0)
				memset(&filter, 0, sizeof(filter));
			if (read_proc_snapshot(&stats, &log_snapshot, &module,
					       message, sizeof(message)) < 0)
				read_module_metrics(&module);
			} else if (read_proc_snapshot(&stats, &log_snapshot, &module,
						      message, sizeof(message)) < 0) {
				fprintf(stderr, "failed to read %s: %s\n", PROC_PATH,
					strerror(errno));
				break;
			}

			update_health(&stats, &health);
			read_system_metrics(&metrics, &previous_cpu);
			update_counter_rates(&stats, &rates);
			read_project_metrics_cached(&project_cache, &project);
			read_collector_metrics_cached(&collector_cache, &collector);
			if (current_view == VIEW_DASHBOARD) {
				scroll_offset = 0;
				snapshot_valid = 0;
				draw_dashboard(&stats, &log_snapshot, &module, &health,
					       &metrics, &rates, &project, &collector,
					       &filter, message, source, action_status,
					       can_control);
			} else if (current_view > VIEW_DASHBOARD &&
				   current_view < VIEW_COUNT) {
				if (!snapshot_valid || snapshot_view != current_view ||
				    cache_expired(&snapshot_updated_at,
						  SNAPSHOT_REFRESH_TTL_SEC)) {
					refresh_snapshot_view(current_view, &snapshot);
					snapshot_view = current_view;
					if (monotonic_now(&snapshot_updated_at) == 0)
						snapshot_valid = 1;
				}
				draw_snapshot_view(snapshot_title(current_view),
						   current_view, &snapshot,
						   scroll_offset, action_status);
			} else {
				current_view = VIEW_DASHBOARD;
			}

			for (int index = 0; index < 10 && keep_running; index += 1) {
			int key = read_key();

			if (key == 'q' || key == 'Q') {
				keep_running = 0;
				break;
			}
				if (key >= '1' && key <= '9') {
					current_view = (enum labtop_view)(key - '1');
					scroll_offset = 0;
					snapshot_valid = 0;
					snprintf(action_status, sizeof(action_status), "view %s",
						 view_name(current_view));
					break;
				}
				if (key == 'f' || key == 'F') {
					current_view = VIEW_FILTER;
					scroll_offset = 0;
					snapshot_valid = 0;
					snprintf(action_status, sizeof(action_status), "view filter");
					break;
				}
				if (key == 'g' || key == 'G') {
					current_view = VIEW_COLLECTOR;
					scroll_offset = 0;
					snapshot_valid = 0;
					snprintf(action_status, sizeof(action_status), "view collector");
					break;
				}
			if ((key == 'j' || key == 'J') && current_view != VIEW_DASHBOARD) {
				if (scroll_offset + SNAPSHOT_VISIBLE_LINES < snapshot.count)
					scroll_offset += 1;
				snprintf(action_status, sizeof(action_status), "scroll %d",
					 scroll_offset);
				break;
			}
			if ((key == 'k' || key == 'K') && current_view != VIEW_DASHBOARD) {
				if (scroll_offset > 0)
					scroll_offset -= 1;
				snprintf(action_status, sizeof(action_status), "scroll %d",
					 scroll_offset);
				break;
			}
				if ((key == 't' || key == 'T') && current_view == VIEW_SELFTEST) {
					read_selftest_view(&snapshot, 1);
					snapshot_view = current_view;
					if (monotonic_now(&snapshot_updated_at) == 0)
						snapshot_valid = 1;
					snprintf(action_status, sizeof(action_status), "selftest run");
					draw_snapshot_view("Kernel Proc Lab Selftest", current_view,
							   &snapshot, scroll_offset,
						   action_status);
				break;
			}
			if (key == 'o' || key == 'O') {
					run_action_view(&current_view, &snapshot, "ops",
							"./scripts/ops-check.sh 2>&1",
							action_status, sizeof(action_status));
					scroll_offset = 0;
					snapshot_view = current_view;
					if (monotonic_now(&snapshot_updated_at) == 0)
						snapshot_valid = 1;
					draw_snapshot_view("Kernel Proc Lab Ops", current_view,
							   &snapshot, scroll_offset,
							   action_status);
				break;
			}
			if (key == 'd' || key == 'D') {
					run_action_view(&current_view, &snapshot, "doctor",
							"./scripts/doctor.sh 2>&1",
							action_status, sizeof(action_status));
					scroll_offset = 0;
					snapshot_view = current_view;
					if (monotonic_now(&snapshot_updated_at) == 0)
						snapshot_valid = 1;
					draw_snapshot_view("Kernel Proc Lab Doctor", current_view,
							   &snapshot, scroll_offset,
							   action_status);
				break;
			}
			if (key == 's' || key == 'S') {
					run_action_view(&current_view, &snapshot, "selftest",
							"./scripts/selftest.sh 2>&1",
							action_status, sizeof(action_status));
					scroll_offset = 0;
					snapshot_view = current_view;
					if (monotonic_now(&snapshot_updated_at) == 0)
						snapshot_valid = 1;
					draw_snapshot_view("Kernel Proc Lab Selftest", current_view,
							   &snapshot, scroll_offset,
							   action_status);
				break;
			}
			if (key == 'i' || key == 'I') {
					run_action_view(&current_view, &snapshot, "ci-check",
							"make ci-check 2>&1",
							action_status, sizeof(action_status));
					scroll_offset = 0;
					snapshot_view = current_view;
					if (monotonic_now(&snapshot_updated_at) == 0)
						snapshot_valid = 1;
					draw_snapshot_view("Kernel Proc Lab CI", current_view,
							   &snapshot, scroll_offset,
							   action_status);
				break;
			}
			if (key == 'm' || key == 'M') {
					run_action_view(&current_view, &snapshot, "holders",
							"./scripts/holders.sh 2>&1",
							action_status, sizeof(action_status));
					scroll_offset = 0;
					snapshot_view = current_view;
					if (monotonic_now(&snapshot_updated_at) == 0)
						snapshot_valid = 1;
					draw_snapshot_view("Kernel Proc Lab Holders", current_view,
							   &snapshot, scroll_offset,
							   action_status);
				break;
			}
			if (key == 'u' || key == 'U') {
					run_action_view(&current_view, &snapshot, "fix-perms",
							"./scripts/fix-perms.sh 2>&1",
							action_status, sizeof(action_status));
					scroll_offset = 0;
					snapshot_view = current_view;
					if (monotonic_now(&snapshot_updated_at) == 0)
						snapshot_valid = 1;
					draw_snapshot_view("Kernel Proc Lab Ops", current_view,
							   &snapshot, scroll_offset,
							   action_status);
				break;
			}
				if ((key == 'w' || key == 'W') && fd >= 0) {
					if (write_test_message_fd(fd) == 0)
						snprintf(action_status, sizeof(action_status),
							 "write ok | writes %llu -> %llu",
							 stats.writes, stats.writes + 1);
					else
						snprintf(action_status, sizeof(action_status),
							 "write failed: %s", strerror(errno));
				break;
			}
				if ((key == '+' || key == '=') && fd >= 0) {
					unsigned int interval = stats.heartbeat_interval_ms;

					if (!require_control_capability(action_status,
									sizeof(action_status),
									"interval"))
						break;
					if (interval > MIN_HEARTBEAT_INTERVAL_MS + HEARTBEAT_STEP_MS)
						interval -= HEARTBEAT_STEP_MS;
					else
						interval = MIN_HEARTBEAT_INTERVAL_MS;
					if (set_heartbeat_interval_fd(fd, interval) == 0)
						snprintf(action_status, sizeof(action_status),
							 "heartbeat interval set to %u ms", interval);
					else
						format_control_error(action_status,
								     sizeof(action_status),
								     "interval");
					break;
				}
				if (key == '-' && fd >= 0) {
					unsigned int interval = stats.heartbeat_interval_ms;

					if (!require_control_capability(action_status,
									sizeof(action_status),
									"interval"))
						break;
					if (interval + HEARTBEAT_STEP_MS < MAX_HEARTBEAT_INTERVAL_MS)
						interval += HEARTBEAT_STEP_MS;
					else
						interval = MAX_HEARTBEAT_INTERVAL_MS;
					if (set_heartbeat_interval_fd(fd, interval) == 0)
						snprintf(action_status, sizeof(action_status),
							 "heartbeat interval set to %u ms", interval);
					else
						format_control_error(action_status,
								     sizeof(action_status),
								     "interval");
					break;
				}
				if ((key == 'r' || key == 'R') && fd >= 0) {
					if (!require_control_capability(action_status,
									sizeof(action_status),
									"stats reset"))
						break;
					if (ioctl(fd, KERNEL_PROC_LAB_IOC_RESET_STATS) == 0)
						snprintf(action_status, sizeof(action_status),
							 "stats reset ok");
					else
						format_control_error(action_status,
								     sizeof(action_status),
								     "stats reset");
					break;
				}
				if ((key == 'l' || key == 'L') && fd >= 0) {
					if (!require_control_capability(action_status,
									sizeof(action_status),
									"log reset"))
						break;
					if (ioctl(fd, KERNEL_PROC_LAB_IOC_RESET_LOG) == 0)
						snprintf(action_status, sizeof(action_status),
							 "log reset ok");
					else
						format_control_error(action_status,
								     sizeof(action_status),
								     "log reset");
					break;
				}
				if ((key == 'a' || key == 'A') && fd >= 0) {
					if (!require_control_capability(action_status,
									sizeof(action_status),
									"reset all"))
						break;
					if (ioctl(fd, KERNEL_PROC_LAB_IOC_RESET_ALL) == 0)
						snprintf(action_status, sizeof(action_status),
							 "all counters and log reset ok");
					else
						format_control_error(action_status,
								     sizeof(action_status),
								     "reset all");
					break;
				}
				if ((key == 'h' || key == 'H') && fd >= 0) {
					unsigned long request = stats.heartbeat_enabled ?
						       KERNEL_PROC_LAB_IOC_STOP_HEARTBEAT :
						       KERNEL_PROC_LAB_IOC_START_HEARTBEAT;

					if (!require_control_capability(action_status,
									sizeof(action_status),
									"heartbeat"))
						break;
					if (ioctl(fd, request) == 0)
						snprintf(action_status, sizeof(action_status),
							 "heartbeat %s ok",
							 stats.heartbeat_enabled ? "stopped" :
										   "started");
					else
						format_control_error(action_status,
								     sizeof(action_status),
								     "heartbeat");
					break;
				}
				if ((key == 'c' || key == 'C') && fd >= 0) {
					if (!require_control_capability(action_status,
									sizeof(action_status),
									"clear message"))
						break;
					if (ioctl(fd, KERNEL_PROC_LAB_IOC_CLEAR_MESSAGE) == 0)
						snprintf(action_status, sizeof(action_status),
							 "message clear ok");
					else
						format_control_error(action_status,
								     sizeof(action_status),
								     "clear message");
					break;
				}
				if ((key == 'r' || key == 'R' || key == 'c' || key == 'C' ||
				     key == 'l' || key == 'L' || key == 'a' || key == 'A' ||
				     key == 'h' || key == 'H' || key == 'w' || key == 'W' ||
				     key == '+' || key == '=' || key == '-') &&
				    fd < 0) {
				snprintf(action_status, sizeof(action_status),
					 "control keys need %s", DEVICE_PATH);
				break;
			}
			if (key != 0) {
				char key_name[16];

				format_key_name(key, key_name, sizeof(key_name));
				snprintf(action_status, sizeof(action_status),
					 "unknown key %s | q quit | d doctor", key_name);
				break;
			}
			usleep(100000);
		}
	}

	if (fd >= 0)
		close(fd);
	return 0;
}
