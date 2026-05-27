#!/usr/bin/env bash
set -euo pipefail

module_name="kernel_proc_lab"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
proc_path="/proc/${module_name}"
device_path="/dev/${module_name}"
sysfs_dir="/sys/class/${module_name}/${module_name}"
trace_dir="/sys/kernel/tracing/events/${module_name}"
debugfs_dir="/sys/kernel/debug/${module_name}"
udev_rule="/etc/udev/rules.d/99-kernel-proc-lab.rules"
tmp_config="/tmp/kernel-proc-lab-ops-config.$$"
tmp_config_error="/tmp/kernel-proc-lab-ops-config.err.$$"
failures=0
warnings=0

pass() {
  printf '[pass] %s\n' "$1"
}

warn() {
  printf '[warn] %s\n' "$1"
  warnings=$((warnings + 1))
}

fail() {
  printf '[fail] %s\n' "$1"
  failures=$((failures + 1))
}

stat_value() {
  "${repo_root}/usercli" stats 2>/dev/null | awk -F': ' -v key="$1" '$1 == key { print $2 }'
}

debugfs_accessible() {
  [[ -x /sys/kernel/debug ]]
}

echo "kernel-proc-lab ops-check"
echo "time: $(date '+%Y-%m-%d %H:%M:%S')"
echo

if awk -v name="${module_name}" '$1 == name { found = 1 } END { exit !found }' /proc/modules; then
  pass "module is loaded"
else
  fail "module is not loaded; run: make load"
fi

if [[ -r "${proc_path}" ]]; then
  pass "proc status is readable"
else
  fail "proc status missing or unreadable: ${proc_path}"
fi

if [[ -e "${device_path}" ]]; then
  pass "device node exists: $(ls -l "${device_path}")"
  if [[ -r "${device_path}" && -w "${device_path}" ]]; then
    pass "device node is readable and writable by current user"
  else
    fail "device permission denied; run: make device-node or make install-udev-rule"
  fi
else
  fail "device node missing: ${device_path}; run: make device-node"
fi

if "${repo_root}/usercli" config >"${tmp_config}" 2>"${tmp_config_error}"; then
  pass "usercli config works"
  sed 's/^/       /' "${tmp_config}"
  if awk -F': ' '$1 == "abi_version" { tool=$2 } $1 == "driver_abi_version" { driver=$2 } END { exit !(tool == driver && tool != "") }' "${tmp_config}"; then
    pass "tool and driver ABI match"
  else
    fail "tool and driver ABI mismatch; run: make reload"
  fi
else
  fail "usercli config failed"
  sed 's/^/       /' "${tmp_config_error}" || true
fi
rm -f "${tmp_config}" "${tmp_config_error}"

if [[ -d "${sysfs_dir}" ]]; then
  pass "sysfs device directory exists"
else
  warn "sysfs device directory missing: ${sysfs_dir}"
fi

if [[ -f "${udev_rule}" ]]; then
  pass "udev rule installed"
else
  warn "udev rule not installed; run: make install-udev-rule"
fi

if mountpoint -q /sys/kernel/debug; then
  pass "debugfs is mounted"
  if ! debugfs_accessible; then
    pass "debugfs access is restricted for current user; use sudo to inspect ${debugfs_dir}"
  elif [[ -d "${debugfs_dir}" ]]; then
    pass "debugfs module directory exists"
  else
    warn "debugfs module directory missing; proc/ioctl still work"
  fi
else
  warn "debugfs is not mounted; run: sudo mount -t debugfs none /sys/kernel/debug"
fi

if [[ -d "${trace_dir}" ]]; then
  pass "tracepoint directory exists"
else
  warn "tracepoint directory missing; check tracefs mount or module trace registration"
fi

if [[ -x "${repo_root}/usercli" && -e "${device_path}" ]]; then
  dropped="$(stat_value dropped_log_events || true)"
  log_events="$(stat_value log_events || true)"
  heartbeat="$(stat_value heartbeat_enabled || true)"
  interval="$(stat_value heartbeat_interval_ms || true)"

  [[ -n "${log_events}" ]] && pass "log_events=${log_events}"
  if [[ "${dropped:-0}" == "0" ]]; then
    pass "no dropped log events"
  else
    warn "dropped_log_events=${dropped}; consider larger ring capacity or lower event rate"
  fi
  pass "heartbeat_enabled=${heartbeat:-unknown} interval_ms=${interval:-unknown}"
fi

echo
if (( failures > 0 )); then
  echo "ops-check result: NOT READY (${failures} fail, ${warnings} warn)"
  exit 1
fi

if (( warnings > 0 )); then
  echo "ops-check result: READY WITH WARNINGS (${warnings} warn)"
else
  echo "ops-check result: READY"
fi
