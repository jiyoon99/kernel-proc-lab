# Changelog

## 0.8.0

- Raised the ioctl ABI to `KERNEL_PROC_LAB_ABI_VERSION=4`.
- Added `log_capacity` module parameter with load-time dynamic ring allocation.
- Added event filter ioctl support and `usercli filter` commands.
- Added `kernel-lab-collector` for long-running JSONL collection.
- Added systemd service and logrotate templates for collector deployment.
- Improved `labtop` with filter/collector views and reduced redraw flicker/read-counter side effects.
- Made `labtop` height-aware so dashboard and snapshot views do not scroll the terminal when events arrive.

## 0.7.0

- Raised the ioctl ABI to `KERNEL_PROC_LAB_ABI_VERSION=3`.
- Added typed log event metadata: event type, monotonic timestamp, pid, uid, and comm.
- Added host-side ring helper tests to `make ci-check` and GitHub Actions.
- Added host-side ABI layout tests for exported ioctl structures.
- Added `labtop` CI integration with the `i` key and project status rows for host tests.
- Restricted destructive/control ioctl and sysfs operations to `CAP_SYS_ADMIN`.
- Added explicit CLI hints when a control command needs sudo/capability access.
- Extended proc and stream output with ABI v3 typed event metadata.
- Added `make holders` and `./run holders` to diagnose modules blocked from unloading.
- Added DKMS install/uninstall/status targets and scripts.
- Improved `labtop` dashboard readiness, DKMS/CI visibility, typed-event coloring, and holder diagnostics.

## 0.6.0

- Added ioctl ABI version reporting (`KERNEL_PROC_LAB_ABI_VERSION=2`).
- Replaced log reads with `GET_LOG_V2`, using a stable request structure and user-provided entry buffer.
- Added JSON output for `usercli stats`, `usercli log`, `usercli config`, and stream events.
- Added `usercli doctor` and strengthened `make doctor` diagnostics for device permissions, udev rules, sysfs, and ABI mismatch.
- Added split reset commands: `reset-stats`, `reset-log`, and `reset-all`.
- Added `make selftest` runtime coverage for ABI, JSON, reset, heartbeat, stream, and log behavior.
- Changed the device permission model from world-writable to `0660` with udev `uaccess` support.
- Added compat ioctl forwarding for 32-bit user-space callers on compat-enabled kernels.
- Increased the retained ring buffer capacity to 64 events.
- Split pure ring-buffer index/snapshot/lookup helpers into `kernel_proc_lab_ring.h`.
- Expanded the KUnit starter suite to cover retained-start calculation, wraparound snapshots, and retained-entry lookup.

## 0.5.0

- Added heartbeat delayed work, sysfs attributes, debugfs status/log files, tracepoints, `poll`, streaming reads, and the `labtop` TUI.
