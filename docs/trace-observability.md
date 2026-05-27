# Trace Observability

`kernel_proc_lab` exposes tracepoints under `kernel_proc_lab:*`.

## Quick Checks

```bash
sudo mount -t tracefs nodev /sys/kernel/tracing 2>/dev/null || true
find /sys/kernel/tracing/events/kernel_proc_lab -maxdepth 2 -type f -name format -print
```

## Live Trace Pipe

```bash
sudo sh -c 'echo 1 > /sys/kernel/tracing/events/kernel_proc_lab/enable'
sudo cat /sys/kernel/tracing/trace_pipe
```

In another terminal:

```bash
./usercli write "trace test"
sudo ./usercli heartbeat on
sudo ./usercli heartbeat off
```

## trace-cmd

```bash
sudo trace-cmd record -e 'kernel_proc_lab:*' -- ./usercli write "trace-cmd test"
sudo trace-cmd report
```

## bpftrace

```bash
sudo bpftrace -e 'tracepoint:kernel_proc_lab:kernel_proc_lab_event { printf("%s seq=%d message=%s\n", str(args->action), args->seq, str(args->message)); }'
```

## Operational Notes

- Use `make ops-check` before relying on trace output.
- If trace events are missing, verify tracefs is mounted and the module is loaded.
- Tracepoints are observability APIs for diagnostics, not a stable user-space ABI.
