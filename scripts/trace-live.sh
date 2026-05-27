#!/usr/bin/env bash
set -euo pipefail

trace_root="/sys/kernel/tracing"
event_path="${trace_root}/events/kernel_proc_lab"

if [[ ! -d "${trace_root}" ]]; then
  sudo mount -t tracefs nodev "${trace_root}"
fi

if [[ ! -d "${event_path}" ]]; then
  echo "kernel_proc_lab tracepoints are not available"
  echo "load the module first: labtop or make load"
  exit 1
fi

sudo sh -c "echo 0 > '${trace_root}/trace'"
sudo sh -c "echo 1 > '${event_path}/enable'"

echo "streaming kernel_proc_lab tracepoints; press Ctrl-C to stop"
sudo cat "${trace_root}/trace_pipe"
