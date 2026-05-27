# ABI v4

`KERNEL_PROC_LAB_ABI_VERSION=4` adds operational controls for longer-running deployments.

## Changes From ABI v3

- `log_capacity` is now a module parameter.
- The kernel allocates the retained event ring at load time.
- Event filtering is exposed through ioctl:
  - `KERNEL_PROC_LAB_IOC_GET_FILTER`
  - `KERNEL_PROC_LAB_IOC_SET_FILTER`
  - `KERNEL_PROC_LAB_IOC_CLEAR_FILTER`
- A userspace collector can stream events to JSONL.

## Ring Capacity

Default retained capacity remains `64` events for compatibility with older expectations.

Load with a larger retained ring:

```bash
MODULE_ARGS="log_capacity=1024" make load
```

Valid range: `8..4096`.

`GET_LOG_V2` still returns at most `KERNEL_PROC_LAB_USER_LOG_CAPACITY` entries per call. Larger kernel retention primarily benefits streaming readers and late snapshot readers that page from newer retained history in future ABI revisions.

## Event Filter

Filters are applied before storing events in the ring buffer. Filtered-out events still advance the global sequence counter, so sequence gaps can indicate filtered events.

Fields:

```c
struct kernel_proc_lab_filter {
	__u32 enabled;
	__u32 type_mask;
	__u32 pid;
	__u32 uid;
	char comm[16];
};
```

Examples:

```bash
sudo ./usercli filter set type=1
sudo ./usercli filter set uid=1000 comm=usercli
./usercli filter show
sudo ./usercli filter clear
```

`type_mask` uses bit `1 << event_type`.
