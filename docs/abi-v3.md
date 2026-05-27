# ABI v3

`KERNEL_PROC_LAB_ABI_VERSION=3` extends log entries from message-only records to typed events.

## Stable ioctl surface

- `KERNEL_PROC_LAB_IOC_GET_STATS`
- `KERNEL_PROC_LAB_IOC_GET_LOG_V2`
- `KERNEL_PROC_LAB_IOC_GET_CONFIG`
- `KERNEL_PROC_LAB_IOC_START_STREAM`
- `KERNEL_PROC_LAB_IOC_STOP_STREAM`

Control operations require `CAP_SYS_ADMIN`:

- `KERNEL_PROC_LAB_IOC_RESET_STATS`
- `KERNEL_PROC_LAB_IOC_CLEAR_MESSAGE`
- `KERNEL_PROC_LAB_IOC_START_HEARTBEAT`
- `KERNEL_PROC_LAB_IOC_STOP_HEARTBEAT`
- `KERNEL_PROC_LAB_IOC_SET_CONFIG`
- `KERNEL_PROC_LAB_IOC_RESET_LOG`
- `KERNEL_PROC_LAB_IOC_RESET_ALL`

## Log Entry

Each retained event is exported as:

```c
struct kernel_proc_lab_log_entry {
	__u64 seq;
	__u64 timestamp_ns;
	__u32 type;
	__u32 pid;
	__u32 uid;
	__u32 reserved;
	char comm[16];
	char message[128];
};
```

`timestamp_ns` is from `ktime_get_ns()`. `pid`, `uid`, and `comm` describe the task that created the event.

## Event Types

- `0`: message
- `1`: write
- `2`: heartbeat
- `3`: clear
- `4`: reset
- `5`: stream
- `6`: config

User-space should prefer the numeric `type` for compatibility and treat display names as presentation text.
