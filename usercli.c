#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/sysmacros.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "kernel_proc_lab_ioctl.h"
#include "kernel_proc_lab_version.h"

#define DEFAULT_DEVICE "/dev/kernel_proc_lab"
#define BUFFER_SIZE 256
#define LINE_BUFFER_SIZE 2048
#define TOOL_VERSION KERNEL_PROC_LAB_VERSION

static const char *event_type_name(__u32 type)
{
	switch (type) {
	case KERNEL_PROC_LAB_EVENT_WRITE:
		return "write";
	case KERNEL_PROC_LAB_EVENT_HEARTBEAT:
		return "heartbeat";
	case KERNEL_PROC_LAB_EVENT_CLEAR:
		return "clear";
	case KERNEL_PROC_LAB_EVENT_RESET:
		return "reset";
	case KERNEL_PROC_LAB_EVENT_STREAM:
		return "stream";
	case KERNEL_PROC_LAB_EVENT_CONFIG:
		return "config";
	case KERNEL_PROC_LAB_EVENT_MESSAGE:
	default:
		return "message";
	}
}

static void print_usage(const char *program_name)
{
	printf("kernel_proc_lab usercli %s\n", TOOL_VERSION);
	printf("usage: %s [--device PATH] <command> [args]\n", program_name);
	printf("\n");
	printf("commands:\n");
	printf("  read                         read the last message\n");
	printf("  write \"message\"              write a message to the driver\n");
	printf("  stats [--json]               read counters through ioctl\n");
	printf("  log [--json]                 print the recent ring buffer\n");
	printf("  config [--json]              print ABI and driver limits\n");
	printf("  health [--json]              print readiness and return nonzero on WARN\n");
	printf("  doctor                       diagnose device, proc, sysfs, ABI access\n");
	printf("  wait                         block until a new event arrives\n");
	printf("  stream [--nonblock] [--json] read log events as a stream\n");
	printf("  filter show|clear             inspect or clear event filter (sudo for clear)\n");
	printf("  filter set [type=N] [pid=N] [uid=N] [comm=NAME]\n");
	printf("  watch [--json] [seconds]     print stats repeatedly\n");
	printf("  heartbeat on|off|status      control delayed-work heartbeat (sudo for on/off)\n");
	printf("  heartbeat interval <ms>      set heartbeat interval (sudo)\n");
	printf("  reset-stats                  reset read/write/open counters (sudo)\n");
	printf("  reset-log                    clear ring buffer and dropped counter (sudo)\n");
	printf("  reset-all                    reset counters, log, heartbeat ticks, message (sudo)\n");
	printf("  clear                        clear the last message (sudo)\n");
	printf("  version                      print tool version\n");
}

static void print_ioctl_error(const char *name)
{
	perror(name);
	if (errno == EPERM || errno == EACCES)
		fprintf(stderr,
			"hint: this control command requires CAP_SYS_ADMIN; try sudo\n");
	if (errno == ENOTTY || errno == EINVAL)
		fprintf(stderr,
			"hint: ioctl ABI mismatch is possible; rebuild and reload the module with: make reload\n");
}

static int wants_json(int argc, char **argv, int arg_index)
{
	return arg_index + 1 < argc && strcmp(argv[arg_index + 1], "--json") == 0;
}

static void print_json_string(const char *value)
{
	const unsigned char *cursor = (const unsigned char *)value;

	putchar('"');
	while (*cursor) {
		if (*cursor == '"' || *cursor == '\\')
			printf("\\%c", *cursor);
		else if (*cursor == '\n')
			printf("\\n");
		else if (*cursor == '\r')
			printf("\\r");
		else if (*cursor == '\t')
			printf("\\t");
		else if (*cursor < 0x20)
			printf("\\u%04x", *cursor);
		else
			putchar(*cursor);
		cursor += 1;
	}
	putchar('"');
}

static void print_stats_json(const struct kernel_proc_lab_stats *stats)
{
	printf("{\"reads\":%llu,\"writes\":%llu,\"opens\":%llu,"
	       "\"log_events\":%llu,\"heartbeat_ticks\":%llu,"
	       "\"dropped_log_events\":%llu,\"heartbeat_enabled\":%u,"
	       "\"heartbeat_interval_ms\":%u,\"abi_version\":%u,"
	       "\"log_capacity\":%u,\"message_max\":%u}",
	       (unsigned long long)stats->reads,
	       (unsigned long long)stats->writes,
	       (unsigned long long)stats->opens,
	       (unsigned long long)stats->log_events,
	       (unsigned long long)stats->heartbeat_ticks,
	       (unsigned long long)stats->dropped_log_events,
	       stats->heartbeat_enabled, stats->heartbeat_interval_ms,
	       stats->abi_version, stats->log_capacity, stats->message_max);
}

static void print_stream_json_line(const char *line)
{
	unsigned long long seq;
	unsigned long long timestamp_ns;
	unsigned int type;
	unsigned int pid;
	unsigned int uid;
	char comm[KERNEL_PROC_LAB_COMM_MAX];
	char message[KERNEL_PROC_LAB_MESSAGE_MAX];

	if (sscanf(line,
		   "#%llu type=%u ts=%llu pid=%u uid=%u comm=%15s msg=%127[^\n]",
		   &seq, &type, &timestamp_ns, &pid, &uid, comm, message) == 7) {
		printf("{\"seq\":%llu,\"timestamp_ns\":%llu,"
		       "\"type\":%u,\"type_name\":",
		       seq, timestamp_ns, type);
		print_json_string(event_type_name(type));
		printf(",\"pid\":%u,\"uid\":%u,\"comm\":", pid, uid);
		print_json_string(comm);
		printf(",\"message\":");
		print_json_string(message);
		printf("}\n");
		return;
	}

	if (sscanf(line, "#%llu %127[^\n]", &seq, message) == 2) {
		printf("{\"seq\":%llu,\"message\":", seq);
		print_json_string(message);
		printf("}\n");
		return;
	}

	printf("{\"raw\":");
	print_json_string(line);
	printf("}\n");
}

static void process_stream_json_bytes(const char *buffer, size_t length,
				      char *line_buffer, size_t *line_length)
{
	size_t index;

	for (index = 0; index < length; index += 1) {
		char ch = buffer[index];

		if (ch == '\n') {
			line_buffer[*line_length] = '\0';
			if (*line_length > 0)
				print_stream_json_line(line_buffer);
			*line_length = 0;
			continue;
		}

		if (*line_length + 1 < LINE_BUFFER_SIZE) {
			line_buffer[*line_length] = ch;
			*line_length += 1;
			continue;
		}

		line_buffer[*line_length] = '\0';
		print_stream_json_line(line_buffer);
		*line_length = 0;
	}
}

static int read_device(const char *device_path)
{
	char buffer[BUFFER_SIZE];
	ssize_t bytes_read;
	int fd = open(device_path, O_RDONLY);

	if (fd < 0) {
		perror("open");
		return 1;
	}

	bytes_read = read(fd, buffer, sizeof(buffer) - 1);
	if (bytes_read < 0) {
		perror("read");
		close(fd);
		return 1;
	}

	buffer[bytes_read] = '\0';
	printf("%s", buffer);
	close(fd);
	return 0;
}

static int write_device(const char *device_path, const char *message)
{
	int fd = open(device_path, O_WRONLY);
	ssize_t bytes_written;

	if (fd < 0) {
		perror("open");
		return 1;
	}

	bytes_written = write(fd, message, strlen(message));
	if (bytes_written < 0) {
		perror("write");
		close(fd);
		return 1;
	}

	close(fd);
	return 0;
}

static int get_stats(const char *device_path, struct kernel_proc_lab_stats *stats)
{
	int fd = open(device_path, O_RDONLY);

	if (fd < 0) {
		perror("open");
		return 1;
	}

	if (ioctl(fd, KERNEL_PROC_LAB_IOC_GET_STATS, stats) < 0) {
		print_ioctl_error("ioctl GET_STATS");
		close(fd);
		return 1;
	}

	close(fd);
	return 0;
}

static int print_stats(const char *device_path, int json)
{
	struct kernel_proc_lab_stats stats;

	if (get_stats(device_path, &stats) != 0)
		return 1;

	if (json) {
		print_stats_json(&stats);
		putchar('\n');
		return 0;
	}

	printf("reads: %llu\n", (unsigned long long)stats.reads);
	printf("writes: %llu\n", (unsigned long long)stats.writes);
	printf("opens: %llu\n", (unsigned long long)stats.opens);
	printf("log_events: %llu\n", (unsigned long long)stats.log_events);
	printf("heartbeat_ticks: %llu\n",
	       (unsigned long long)stats.heartbeat_ticks);
	printf("dropped_log_events: %llu\n",
	       (unsigned long long)stats.dropped_log_events);
	printf("heartbeat_enabled: %s\n",
	       stats.heartbeat_enabled ? "yes" : "no");
	printf("heartbeat_interval_ms: %u\n", stats.heartbeat_interval_ms);
	printf("abi_version: %u\n", stats.abi_version);
	printf("log_capacity: %u\n", stats.log_capacity);
	printf("message_max: %u\n", stats.message_max);
	return 0;
}

static int print_config(const char *device_path, int json)
{
	struct kernel_proc_lab_stats stats;

	if (get_stats(device_path, &stats) != 0)
		return 1;

	if (json) {
		printf("{\"tool_version\":");
		print_json_string(TOOL_VERSION);
		printf(",\"abi_version\":%u,\"driver_abi_version\":%u,"
		       "\"log_capacity\":%u,\"message_max\":%u}\n",
		       KERNEL_PROC_LAB_ABI_VERSION, stats.abi_version,
		       stats.log_capacity, stats.message_max);
		return 0;
	}

	printf("tool_version: %s\n", TOOL_VERSION);
	printf("abi_version: %u\n", KERNEL_PROC_LAB_ABI_VERSION);
	printf("driver_abi_version: %u\n", stats.abi_version);
	printf("log_capacity: %u\n", stats.log_capacity);
	printf("message_max: %u\n", stats.message_max);
	return 0;
}

static int print_health(const char *device_path, int json)
{
	struct kernel_proc_lab_stats stats;
	const char *state = "READY";
	const char *reason = "ABI ok, retained ring operational";
	int ready = 1;

	if (get_stats(device_path, &stats) != 0)
		return 1;

	if (stats.abi_version != KERNEL_PROC_LAB_ABI_VERSION) {
		state = "WARN";
		reason = "tool and driver ABI versions differ";
		ready = 0;
	}

	if (json) {
		printf("{\"state\":");
		print_json_string(state);
		printf(",\"reason\":");
		print_json_string(reason);
		printf(",\"abi_version\":%u,\"driver_abi_version\":%u,"
		       "\"dropped_log_events\":%llu,\"heartbeat_enabled\":%u,"
		       "\"heartbeat_interval_ms\":%u}\n",
		       KERNEL_PROC_LAB_ABI_VERSION, stats.abi_version,
		       (unsigned long long)stats.dropped_log_events,
		       stats.heartbeat_enabled, stats.heartbeat_interval_ms);
		return ready ? 0 : 1;
	}

	printf("state: %s\n", state);
	printf("reason: %s\n", reason);
	printf("abi_version: %u\n", KERNEL_PROC_LAB_ABI_VERSION);
	printf("driver_abi_version: %u\n", stats.abi_version);
	printf("dropped_log_events: %llu\n",
	       (unsigned long long)stats.dropped_log_events);
	printf("heartbeat_enabled: %s\n", stats.heartbeat_enabled ? "yes" : "no");
	printf("heartbeat_interval_ms: %u\n", stats.heartbeat_interval_ms);
	return ready ? 0 : 1;
}

static int print_log(const char *device_path, int json)
{
	struct kernel_proc_lab_log_snapshot snapshot;
	struct kernel_proc_lab_log_read request;
	int fd = open(device_path, O_RDONLY);
	__u32 index;

	if (fd < 0) {
		perror("open");
		return 1;
	}

	memset(&snapshot, 0, sizeof(snapshot));
	memset(&request, 0, sizeof(request));
	request.abi_version = KERNEL_PROC_LAB_ABI_VERSION;
	request.entry_size = sizeof(struct kernel_proc_lab_log_entry);
	request.capacity = KERNEL_PROC_LAB_USER_LOG_CAPACITY;
	request.entries = (__u64)(uintptr_t)snapshot.entries;

	if (ioctl(fd, KERNEL_PROC_LAB_IOC_GET_LOG_V2, &request) < 0) {
		print_ioctl_error("ioctl GET_LOG_V2");
		close(fd);
		return 1;
	}
	snapshot.count = request.count;

	if (json) {
		printf("{\"count\":%u,\"entries\":[", snapshot.count);
		for (index = 0; index < snapshot.count; index += 1) {
			if (index > 0)
				putchar(',');
			printf("{\"seq\":%llu,\"timestamp_ns\":%llu,"
			       "\"type\":%u,\"type_name\":",
			       (unsigned long long)snapshot.entries[index].seq,
			       (unsigned long long)snapshot.entries[index].timestamp_ns,
			       snapshot.entries[index].type);
			print_json_string(event_type_name(snapshot.entries[index].type));
			printf(",\"pid\":%u,\"uid\":%u,\"comm\":",
			       snapshot.entries[index].pid,
			       snapshot.entries[index].uid);
			print_json_string(snapshot.entries[index].comm);
			printf(",\"message\":");
			print_json_string(snapshot.entries[index].message);
			putchar('}');
		}
		printf("]}\n");
		close(fd);
		return 0;
	}

	if (snapshot.count == 0) {
		printf("(empty)\n");
		close(fd);
		return 0;
	}

	for (index = 0; index < snapshot.count; index += 1)
		printf("#%llu %-9s pid=%u uid=%u comm=%s %s\n",
		       (unsigned long long)snapshot.entries[index].seq,
		       event_type_name(snapshot.entries[index].type),
		       snapshot.entries[index].pid, snapshot.entries[index].uid,
		       snapshot.entries[index].comm[0] ?
			       snapshot.entries[index].comm :
			       "-",
		       snapshot.entries[index].message);

	close(fd);
	return 0;
}

static int doctor_device(const char *device_path)
{
	struct kernel_proc_lab_stats stats;
	struct stat st;
	int failures = 0;

	printf("kernel_proc_lab usercli doctor\n");
	printf("device: %s\n", device_path);

	if (access("/proc/kernel_proc_lab", R_OK) == 0)
		printf("[ok]   proc readable: /proc/kernel_proc_lab\n");
	else
		printf("[warn] proc not readable: /proc/kernel_proc_lab (%s)\n",
		       strerror(errno));

	if (stat(device_path, &st) == 0) {
		printf("[ok]   device node exists: mode %o major %u minor %u\n",
		       st.st_mode & 0777, major(st.st_rdev), minor(st.st_rdev));
	} else {
		printf("[fail] device node missing: %s (%s)\n", device_path,
		       strerror(errno));
		failures += 1;
	}

	if (access(device_path, R_OK | W_OK) == 0)
		printf("[ok]   device is readable and writable\n");
	else {
		printf("[fail] device access failed: %s\n", strerror(errno));
		printf("       try: make device-node or make install-udev-rule\n");
		failures += 1;
	}

	if (get_stats(device_path, &stats) == 0) {
		printf("[ok]   ioctl GET_STATS works\n");
		if (stats.abi_version == KERNEL_PROC_LAB_ABI_VERSION) {
			printf("[ok]   ABI matches: %u\n", stats.abi_version);
		} else {
			printf("[fail] ABI mismatch: tool %u driver %u\n",
			       KERNEL_PROC_LAB_ABI_VERSION, stats.abi_version);
			failures += 1;
		}
		printf("[ok]   log_capacity=%u message_max=%u\n",
		       stats.log_capacity, stats.message_max);
	} else {
		printf("[fail] ioctl GET_STATS failed\n");
		failures += 1;
	}

	return failures == 0 ? 0 : 1;
}

static int wait_for_log_event(const char *device_path)
{
	struct pollfd poll_fd;
	int fd = open(device_path, O_RDONLY);
	int ret;

	if (fd < 0) {
		perror("open");
		return 1;
	}

	poll_fd.fd = fd;
	poll_fd.events = POLLIN;
	poll_fd.revents = 0;

	printf("waiting for a kernel_proc_lab event...\n");
	ret = poll(&poll_fd, 1, -1);
	if (ret < 0) {
		perror("poll");
		close(fd);
		return 1;
	}

	close(fd);
	return print_log(device_path, 0);
}

static int stream_device(const char *device_path, int nonblock, int json)
{
	int open_flags = O_RDONLY;
	int fd;
	char line_buffer[LINE_BUFFER_SIZE];
	size_t line_length = 0;

	if (nonblock)
		open_flags |= O_NONBLOCK;
	line_buffer[0] = '\0';

	fd = open(device_path, open_flags);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	if (ioctl(fd, KERNEL_PROC_LAB_IOC_START_STREAM) < 0) {
		perror("ioctl START_STREAM");
		close(fd);
		return 1;
	}

	for (;;) {
		char buffer[BUFFER_SIZE];
		ssize_t bytes_read = read(fd, buffer, sizeof(buffer));

		if (bytes_read < 0) {
			if (errno == EAGAIN && nonblock) {
				perror("read");
				ioctl(fd, KERNEL_PROC_LAB_IOC_STOP_STREAM);
				close(fd);
				return 1;
			}
			if (errno == EINTR)
				continue;
			perror("read");
			break;
		}

		if (bytes_read == 0)
			continue;
		if (json) {
			process_stream_json_bytes(buffer, (size_t)bytes_read,
						  line_buffer, &line_length);
		} else {
			fwrite(buffer, 1, (size_t)bytes_read, stdout);
		}
		fflush(stdout);

		if (nonblock)
			break;
	}

	if (json && line_length > 0) {
		line_buffer[line_length] = '\0';
		print_stream_json_line(line_buffer);
	}

	ioctl(fd, KERNEL_PROC_LAB_IOC_STOP_STREAM);
	close(fd);
	return 0;
}

static int watch_stats(const char *device_path, int json, unsigned int seconds)
{
	for (;;) {
		struct kernel_proc_lab_stats stats;

		if (get_stats(device_path, &stats) != 0)
			return 1;
		if (json) {
			print_stats_json(&stats);
			putchar('\n');
		} else {
			printf("reads=%llu writes=%llu opens=%llu log=%llu "
			       "heartbeat=%llu drops=%llu enabled=%u interval_ms=%u\n",
			       (unsigned long long)stats.reads,
			       (unsigned long long)stats.writes,
			       (unsigned long long)stats.opens,
			       (unsigned long long)stats.log_events,
			       (unsigned long long)stats.heartbeat_ticks,
			       (unsigned long long)stats.dropped_log_events,
			       stats.heartbeat_enabled, stats.heartbeat_interval_ms);
		}
		fflush(stdout);
		sleep(seconds);
	}
}

static int run_simple_ioctl(const char *device_path, unsigned long request,
			    const char *name)
{
	int fd = open(device_path, O_RDWR);

	if (fd < 0) {
		perror("open");
		return 1;
	}

	if (ioctl(fd, request) < 0) {
		print_ioctl_error(name);
		close(fd);
		return 1;
	}

	close(fd);
	return 0;
}

static int set_interval(const char *device_path, const char *value)
{
	struct kernel_proc_lab_config config;
	char *end = NULL;
	unsigned long interval_ms = strtoul(value, &end, 10);
	int fd;

	if (!value[0] || (end && *end != '\0') || interval_ms > UINT32_MAX) {
		fprintf(stderr, "invalid interval: %s\n", value);
		return 2;
	}

	config.heartbeat_interval_ms = (__u32)interval_ms;
	fd = open(device_path, O_RDWR);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	if (ioctl(fd, KERNEL_PROC_LAB_IOC_SET_CONFIG, &config) < 0) {
		print_ioctl_error("ioctl SET_CONFIG");
		close(fd);
		return 1;
	}

	close(fd);
	return 0;
}

static int get_filter(const char *device_path, struct kernel_proc_lab_filter *filter)
{
	int fd = open(device_path, O_RDONLY);

	if (fd < 0) {
		perror("open");
		return 1;
	}

	if (ioctl(fd, KERNEL_PROC_LAB_IOC_GET_FILTER, filter) < 0) {
		print_ioctl_error("ioctl GET_FILTER");
		close(fd);
		return 1;
	}

	close(fd);
	return 0;
}

static int print_filter(const char *device_path)
{
	struct kernel_proc_lab_filter filter;

	if (get_filter(device_path, &filter) != 0)
		return 1;

	printf("enabled: %s\n", filter.enabled ? "yes" : "no");
	printf("type_mask: 0x%x\n", filter.type_mask);
	printf("pid: %u\n", filter.pid);
	printf("uid: %u\n", filter.uid);
	printf("comm: %s\n", filter.comm[0] ? filter.comm : "(any)");
	return 0;
}

static int parse_u32_value(const char *value, __u32 *out)
{
	char *end = NULL;
	unsigned long parsed = strtoul(value, &end, 0);

	if (!value[0] || (end && *end != '\0') || parsed > UINT32_MAX)
		return -1;
	*out = (__u32)parsed;
	return 0;
}

static int set_filter(int argc, char **argv, int arg_index, const char *device_path)
{
	struct kernel_proc_lab_filter filter;
	int fd;
	int index;

	memset(&filter, 0, sizeof(filter));
	filter.enabled = 1;

	for (index = arg_index; index < argc; index += 1) {
		if (strncmp(argv[index], "type=", 5) == 0) {
			__u32 type;

			if (parse_u32_value(argv[index] + 5, &type) != 0 || type >= 32) {
				fprintf(stderr, "invalid filter type: %s\n", argv[index]);
				return 2;
			}
			filter.type_mask |= 1U << type;
		} else if (strncmp(argv[index], "type-mask=", 10) == 0) {
			if (parse_u32_value(argv[index] + 10, &filter.type_mask) != 0) {
				fprintf(stderr, "invalid filter type-mask: %s\n",
					argv[index]);
				return 2;
			}
		} else if (strncmp(argv[index], "pid=", 4) == 0) {
			if (parse_u32_value(argv[index] + 4, &filter.pid) != 0) {
				fprintf(stderr, "invalid filter pid: %s\n", argv[index]);
				return 2;
			}
		} else if (strncmp(argv[index], "uid=", 4) == 0) {
			if (parse_u32_value(argv[index] + 4, &filter.uid) != 0) {
				fprintf(stderr, "invalid filter uid: %s\n", argv[index]);
				return 2;
			}
		} else if (strncmp(argv[index], "comm=", 5) == 0) {
			snprintf(filter.comm, sizeof(filter.comm), "%.*s",
				 (int)sizeof(filter.comm) - 1, argv[index] + 5);
		} else {
			fprintf(stderr, "unknown filter option: %s\n", argv[index]);
			return 2;
		}
	}

	fd = open(device_path, O_RDWR);
	if (fd < 0) {
		perror("open");
		return 1;
	}
	if (ioctl(fd, KERNEL_PROC_LAB_IOC_SET_FILTER, &filter) < 0) {
		print_ioctl_error("ioctl SET_FILTER");
		close(fd);
		return 1;
	}
	close(fd);
	return 0;
}

int main(int argc, char **argv)
{
	const char *device_path = DEFAULT_DEVICE;
	int arg_index = 1;

	while (arg_index < argc) {
		if (strcmp(argv[arg_index], "--device") == 0 ||
		    strcmp(argv[arg_index], "-d") == 0) {
			if (arg_index + 1 >= argc) {
				fprintf(stderr, "missing value for %s\n", argv[arg_index]);
				return 2;
			}
			device_path = argv[arg_index + 1];
			arg_index += 2;
			continue;
		}
		break;
	}

	if (arg_index >= argc || strcmp(argv[arg_index], "help") == 0 ||
	    strcmp(argv[arg_index], "--help") == 0 ||
	    strcmp(argv[arg_index], "-h") == 0) {
		print_usage(argv[0]);
		return 0;
	}

	if (strcmp(argv[arg_index], "version") == 0 ||
	    strcmp(argv[arg_index], "--version") == 0) {
		printf("%s abi=%u\n", TOOL_VERSION, KERNEL_PROC_LAB_ABI_VERSION);
		return 0;
	}

	if (strcmp(argv[arg_index], "read") == 0)
		return read_device(device_path);

	if (strcmp(argv[arg_index], "write") == 0) {
		if (arg_index + 1 >= argc) {
			fprintf(stderr, "usage: %s write \"message\"\n", argv[0]);
			return 2;
		}
		return write_device(device_path, argv[arg_index + 1]);
	}

	if (strcmp(argv[arg_index], "stats") == 0)
		return print_stats(device_path, wants_json(argc, argv, arg_index));

	if (strcmp(argv[arg_index], "log") == 0)
		return print_log(device_path, wants_json(argc, argv, arg_index));

	if (strcmp(argv[arg_index], "config") == 0)
		return print_config(device_path, wants_json(argc, argv, arg_index));

	if (strcmp(argv[arg_index], "health") == 0)
		return print_health(device_path, wants_json(argc, argv, arg_index));

	if (strcmp(argv[arg_index], "doctor") == 0)
		return doctor_device(device_path);

	if (strcmp(argv[arg_index], "wait") == 0)
		return wait_for_log_event(device_path);

	if (strcmp(argv[arg_index], "stream") == 0) {
		int nonblock = 0;
		int json = 0;
		int index;

		for (index = arg_index + 1; index < argc; index += 1) {
			if (strcmp(argv[index], "--nonblock") == 0)
				nonblock = 1;
			else if (strcmp(argv[index], "--json") == 0)
				json = 1;
			else {
				fprintf(stderr, "unknown stream option: %s\n", argv[index]);
				return 2;
			}
		}

		return stream_device(device_path, nonblock, json);
	}

	if (strcmp(argv[arg_index], "filter") == 0) {
		if (arg_index + 1 >= argc || strcmp(argv[arg_index + 1], "show") == 0)
			return print_filter(device_path);
		if (strcmp(argv[arg_index + 1], "clear") == 0)
			return run_simple_ioctl(device_path,
						KERNEL_PROC_LAB_IOC_CLEAR_FILTER,
						"ioctl CLEAR_FILTER");
		if (strcmp(argv[arg_index + 1], "set") == 0)
			return set_filter(argc, argv, arg_index + 2, device_path);

		fprintf(stderr,
			"usage: %s filter show|clear|set [type=N] [pid=N] [uid=N] [comm=NAME]\n",
			argv[0]);
		return 2;
	}

	if (strcmp(argv[arg_index], "watch") == 0) {
		int json = 0;
		unsigned int seconds = 1;
		int index;

		for (index = arg_index + 1; index < argc; index += 1) {
			if (strcmp(argv[index], "--json") == 0) {
				json = 1;
			} else {
				char *end = NULL;
				unsigned long parsed = strtoul(argv[index], &end, 10);

				if (!argv[index][0] || (end && *end != '\0') ||
				    parsed == 0 || parsed > UINT32_MAX) {
					fprintf(stderr, "invalid watch interval: %s\n",
						argv[index]);
					return 2;
				}
				seconds = (unsigned int)parsed;
			}
		}

		return watch_stats(device_path, json, seconds);
	}

	if (strcmp(argv[arg_index], "heartbeat") == 0) {
		if (arg_index + 1 >= argc) {
			fprintf(stderr, "usage: %s heartbeat [on|off|status]\n", argv[0]);
			return 2;
		}
		if (strcmp(argv[arg_index + 1], "on") == 0)
			return run_simple_ioctl(device_path,
						KERNEL_PROC_LAB_IOC_START_HEARTBEAT,
						"ioctl START_HEARTBEAT");
		if (strcmp(argv[arg_index + 1], "off") == 0)
			return run_simple_ioctl(device_path,
						KERNEL_PROC_LAB_IOC_STOP_HEARTBEAT,
						"ioctl STOP_HEARTBEAT");
		if (strcmp(argv[arg_index + 1], "status") == 0)
			return print_stats(device_path, 0);
		if (strcmp(argv[arg_index + 1], "interval") == 0) {
			if (arg_index + 2 >= argc) {
				fprintf(stderr, "usage: %s heartbeat interval <ms>\n",
					argv[0]);
				return 2;
			}
			return set_interval(device_path, argv[arg_index + 2]);
		}

		fprintf(stderr, "usage: %s heartbeat [on|off|status|interval <ms>]\n",
			argv[0]);
		return 2;
	}

	if (strcmp(argv[arg_index], "reset-stats") == 0)
		return run_simple_ioctl(device_path, KERNEL_PROC_LAB_IOC_RESET_STATS,
					"ioctl RESET_STATS");

	if (strcmp(argv[arg_index], "reset-log") == 0)
		return run_simple_ioctl(device_path, KERNEL_PROC_LAB_IOC_RESET_LOG,
					"ioctl RESET_LOG");

	if (strcmp(argv[arg_index], "reset-all") == 0)
		return run_simple_ioctl(device_path, KERNEL_PROC_LAB_IOC_RESET_ALL,
					"ioctl RESET_ALL");

	if (strcmp(argv[arg_index], "clear") == 0)
		return run_simple_ioctl(device_path, KERNEL_PROC_LAB_IOC_CLEAR_MESSAGE,
					"ioctl CLEAR_MESSAGE");

	print_usage(argv[0]);
	return 2;
}
