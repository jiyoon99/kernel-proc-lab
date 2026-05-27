#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "kernel_proc_lab_ioctl.h"
#include "kernel_proc_lab_version.h"

#define DEFAULT_DEVICE "/dev/kernel_proc_lab"
#define DEFAULT_OUTPUT "/var/log/kernel-proc-lab/events.jsonl"
#define BUFFER_SIZE 512
#define LINE_BUFFER_SIZE 2048
#define COLLECTOR_VERSION KERNEL_PROC_LAB_VERSION

static volatile sig_atomic_t keep_running = 1;
static volatile sig_atomic_t reopen_requested = 0;

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
	default:
		return "message";
	}
}

static void print_json_string(FILE *out, const char *value)
{
	const unsigned char *cursor = (const unsigned char *)value;

	fputc('"', out);
	while (*cursor) {
		if (*cursor == '"' || *cursor == '\\')
			fprintf(out, "\\%c", *cursor);
		else if (*cursor == '\n')
			fprintf(out, "\\n");
		else if (*cursor == '\r')
			fprintf(out, "\\r");
		else if (*cursor == '\t')
			fprintf(out, "\\t");
		else if (*cursor < 0x20)
			fprintf(out, "\\u%04x", *cursor);
		else
			fputc(*cursor, out);
		cursor += 1;
	}
	fputc('"', out);
}

static void handle_signal(int signal_number)
{
	if (signal_number == SIGHUP)
		reopen_requested = 1;
	else
		keep_running = 0;
}

static int ensure_parent_dir(void)
{
	if (mkdir("/var/log/kernel-proc-lab", 0755) < 0 && errno != EEXIST) {
		perror("mkdir /var/log/kernel-proc-lab");
		return 1;
	}
	return 0;
}

static int write_event(FILE *out, const char *line)
{
	unsigned long long seq;
	unsigned long long timestamp_ns;
	unsigned int type;
	unsigned int pid;
	unsigned int uid;
	char comm[KERNEL_PROC_LAB_COMM_MAX];
	char message[KERNEL_PROC_LAB_MESSAGE_MAX];
	time_t now = time(NULL);

	if (sscanf(line,
		   "#%llu type=%u ts=%llu pid=%u uid=%u comm=%15s msg=%127[^\n]",
		   &seq, &type, &timestamp_ns, &pid, &uid, comm, message) == 7) {
		fprintf(out,
			"{\"collector_ts\":%lld,\"seq\":%llu,\"timestamp_ns\":%llu,"
			"\"type\":%u,\"type_name\":",
			(long long)now, seq, timestamp_ns, type);
		print_json_string(out, event_type_name(type));
		fprintf(out, ",\"pid\":%u,\"uid\":%u,\"comm\":", pid, uid);
		print_json_string(out, comm);
		fprintf(out, ",\"message\":");
		print_json_string(out, message);
		fprintf(out, "}\n");
		fflush(out);
		return 0;
	}

	fprintf(out, "{\"collector_ts\":%lld,\"raw\":", (long long)now);
	print_json_string(out, line);
	fprintf(out, "}\n");
	fflush(out);
	return 0;
}

static FILE *open_output_file(const char *output_path)
{
	FILE *out = fopen(output_path, "a");

	if (!out)
		perror("fopen output");
	return out;
}

static void process_stream_bytes(FILE *out, const char *buffer, size_t length,
				 char *line_buffer, size_t *line_length)
{
	size_t index;

	for (index = 0; index < length; index += 1) {
		char ch = buffer[index];

		if (ch == '\n') {
			line_buffer[*line_length] = '\0';
			if (*line_length > 0)
				write_event(out, line_buffer);
			*line_length = 0;
			continue;
		}

		if (*line_length + 1 < LINE_BUFFER_SIZE) {
			line_buffer[*line_length] = ch;
			*line_length += 1;
			continue;
		}

		line_buffer[*line_length] = '\0';
		write_event(out, line_buffer);
		*line_length = 0;
	}
}

int main(int argc, char **argv)
{
	const char *device_path = DEFAULT_DEVICE;
	const char *output_path = DEFAULT_OUTPUT;
	FILE *out;
	int fd;
	int index;
	char line_buffer[LINE_BUFFER_SIZE];
	size_t line_length = 0;

	for (index = 1; index < argc; index += 1) {
		if (strcmp(argv[index], "--device") == 0 && index + 1 < argc) {
			device_path = argv[++index];
		} else if (strcmp(argv[index], "--output") == 0 && index + 1 < argc) {
			output_path = argv[++index];
		} else if (strcmp(argv[index], "--version") == 0) {
			printf("%s abi=%u\n", COLLECTOR_VERSION,
			       KERNEL_PROC_LAB_ABI_VERSION);
			return 0;
		} else {
			fprintf(stderr,
				"usage: %s [--device PATH] [--output PATH]\n",
				argv[0]);
			return 2;
		}
	}

	line_buffer[0] = '\0';

	if (strcmp(output_path, DEFAULT_OUTPUT) == 0 && ensure_parent_dir() != 0)
		return 1;

	out = open_output_file(output_path);
	if (!out) {
		return 1;
	}

	fd = open(device_path, O_RDONLY);
	if (fd < 0) {
		perror("open device");
		fclose(out);
		return 1;
	}

	if (ioctl(fd, KERNEL_PROC_LAB_IOC_START_STREAM) < 0) {
		perror("ioctl START_STREAM");
		close(fd);
		fclose(out);
		return 1;
	}

	signal(SIGINT, handle_signal);
	signal(SIGTERM, handle_signal);
	signal(SIGHUP, handle_signal);

	while (keep_running) {
		struct pollfd poll_fd = {
			.fd = fd,
			.events = POLLIN,
			.revents = 0,
		};
		char buffer[BUFFER_SIZE];
		ssize_t bytes_read;
		int poll_result;

		if (reopen_requested) {
			FILE *new_out;

			reopen_requested = 0;
			fflush(out);
			new_out = open_output_file(output_path);
			if (new_out) {
				fclose(out);
				out = new_out;
			}
		}

		poll_result = poll(&poll_fd, 1, 1000);
		if (poll_result < 0) {
			if (errno == EINTR)
				continue;
			perror("poll");
			break;
		}
		if (poll_result == 0)
			continue;
		if ((poll_fd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
			break;
		if ((poll_fd.revents & POLLIN) == 0)
			continue;

		bytes_read = read(fd, buffer, sizeof(buffer));
		if (bytes_read < 0) {
			if (errno == EINTR)
				continue;
			perror("read");
			break;
		}
		if (bytes_read == 0)
			continue;
		process_stream_bytes(out, buffer, (size_t)bytes_read, line_buffer,
				     &line_length);
	}

	if (line_length > 0) {
		line_buffer[line_length] = '\0';
		write_event(out, line_buffer);
	}

	ioctl(fd, KERNEL_PROC_LAB_IOC_STOP_STREAM);
	close(fd);
	fclose(out);
	return 0;
}
