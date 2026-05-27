#!/usr/bin/env bash
set -euo pipefail

module_name="kernel_proc_lab"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
kernel_release="$(uname -r)"
kbuild_dir="/lib/modules/${kernel_release}/build"
proc_path="/proc/${module_name}"
device_path="/dev/${module_name}"
sysfs_dir="/sys/class/${module_name}/${module_name}"
debugfs_dir="/sys/kernel/debug/${module_name}"
udev_rule="/etc/udev/rules.d/99-kernel-proc-lab.rules"
repo_udev_rule="${repo_root}/udev/99-kernel-proc-lab.rules"
tmp_config="/tmp/kernel-proc-lab-doctor-config.$$"
tmp_config_error="/tmp/kernel-proc-lab-doctor-config.err.$$"
failure_count=0

pass() {
  printf '[ok]   %s\n' "$1"
}

warn() {
  printf '[warn] %s\n' "$1"
}

fail() {
  printf '[fail] %s\n' "$1"
  failure_count=$((failure_count + 1))
}

check_command() {
  local command_name="$1"

  if command -v "${command_name}" >/dev/null 2>&1; then
    pass "found ${command_name}: $(command -v "${command_name}")"
  else
    fail "missing ${command_name}"
  fi
}

debugfs_accessible() {
  [[ -x /sys/kernel/debug ]]
}

echo "kernel-proc-lab doctor"
echo "repo: ${repo_root}"
echo "kernel: ${kernel_release}"
echo

check_command make
check_command cc
check_command sudo

if command -v shellcheck >/dev/null 2>&1; then
  pass "found shellcheck: $(command -v shellcheck)"
else
  warn "shellcheck not installed; CI still runs shellcheck on scripts"
fi

if [[ -d "${kbuild_dir}" ]]; then
  pass "kernel headers available: ${kbuild_dir}"
else
  fail "kernel headers missing: ${kbuild_dir}"
  echo "       install headers for ${kernel_release}"
fi

if [[ -f "${repo_root}/${module_name}.ko" ]]; then
  pass "module artifact exists: ${module_name}.ko"
else
  warn "module artifact missing; run: make"
fi

if lsmod | grep -q "^${module_name}"; then
  pass "module is loaded"
else
  warn "module is not loaded; run: make load or ./demo.sh"
fi

if [[ -e "${proc_path}" ]]; then
  pass "proc entry exists: ${proc_path}"
else
  warn "proc entry missing: ${proc_path}"
fi

if [[ -e "${device_path}" ]]; then
  pass "device node exists: ${device_path}"
  stat -c '       %A %U %G %t,%T %n' "${device_path}"
  if [[ -r "${device_path}" && -w "${device_path}" ]]; then
    pass "current user can read and write ${device_path}"
  else
    warn "current user cannot read/write ${device_path}; run: make device-node or make install-udev-rule"
  fi
  if command -v getfacl >/dev/null 2>&1; then
    getfacl -p "${device_path}" 2>/dev/null | sed 's/^/       /' || true
  fi
else
  warn "device node missing: ${device_path}; run: make load"
fi

if [[ -d "${sysfs_dir}" ]]; then
  pass "sysfs attributes available: ${sysfs_dir}"
  if [[ -r "${sysfs_dir}/dev" ]]; then
    pass "sysfs dev number: $(cat "${sysfs_dir}/dev")"
  fi
  if [[ -r "${sysfs_dir}/stats" ]]; then
    awk '/abi_version|log_capacity|message_max|heartbeat_interval_ms/ { print "       " $0 }' "${sysfs_dir}/stats"
  fi
else
  warn "sysfs attributes missing: ${sysfs_dir}"
fi

if [[ -f "${udev_rule}" ]]; then
  pass "udev rule installed: ${udev_rule}"
  if cmp -s "${repo_udev_rule}" "${udev_rule}"; then
    pass "installed udev rule matches repository rule"
  else
    warn "installed udev rule differs from repository rule; run: make install-udev-rule"
  fi
else
  warn "udev rule not installed: ${udev_rule}; run: make install-udev-rule"
fi

if [[ -x "${repo_root}/usercli" && -e "${device_path}" ]]; then
  if "${repo_root}/usercli" config >"${tmp_config}" 2>"${tmp_config_error}"; then
    pass "usercli config works"
    sed 's/^/       /' "${tmp_config}"
    if awk -F': ' '$1 == "abi_version" { tool=$2 } $1 == "driver_abi_version" { driver=$2 } END { exit !(tool == driver && tool != "") }' "${tmp_config}"; then
      pass "tool and driver ABI versions match"
    else
      fail "tool and driver ABI versions differ"
    fi
  else
    warn "usercli config failed"
    sed 's/^/       /' "${tmp_config_error}" || true
  fi
  rm -f "${tmp_config}" "${tmp_config_error}"
fi

if mountpoint -q /sys/kernel/debug; then
  pass "debugfs is mounted"
  if ! debugfs_accessible; then
    warn "debugfs is mounted but current user cannot inspect it; run: sudo ls -ld ${debugfs_dir}"
  elif [[ -d "${debugfs_dir}" ]]; then
    pass "debugfs module directory exists: ${debugfs_dir}"
  else
    warn "debugfs module directory missing: ${debugfs_dir}"
  fi
else
  warn "debugfs is not mounted; run: sudo mount -t debugfs none /sys/kernel/debug"
fi

if command -v mokutil >/dev/null 2>&1; then
  if mokutil --sb-state 2>/dev/null | grep -qi enabled; then
    warn "Secure Boot appears enabled; unsigned insmod may fail"
  else
    pass "Secure Boot does not appear to block unsigned modules"
  fi
else
  warn "mokutil not installed; Secure Boot state not checked"
fi

echo
if (( failure_count > 0 )); then
  echo "doctor finished with ${failure_count} required check failure(s)"
  exit 1
fi

echo "doctor finished without required check failures"
