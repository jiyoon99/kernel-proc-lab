#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

# shellcheck source=scripts/module-common.sh
source "${repo_root}/scripts/module-common.sh"

control_cmd() {
  if [[ "${EUID}" -eq 0 ]]; then
    "$@"
  elif [[ -t 0 ]]; then
    sudo "$@"
  else
    if sudo -n true 2>/dev/null; then
      sudo "$@"
    else
      echo "privileged smoke-test controls need sudo; run this in an interactive terminal" >&2
      exit 1
    fi
  fi
}

if ! is_module_loaded; then
  echo "module is not loaded"
  echo "run: make load"
  exit 1
fi

echo "reading ${proc_path}"
cat "${proc_path}"

ensure_device_node

if [[ ! -e "${device_path}" ]]; then
  echo "${device_path} does not exist"
  echo "check dmesg or create the node manually from /proc/devices"
  exit 1
fi

echo "writing test message through ${device_path}"
echo "smoke test $(date +%H:%M:%S)" | sudo tee "${device_path}" >/dev/null

echo "reading message through usercli"
./usercli read

echo "reading stats through ioctl"
./usercli stats

echo "checking ioctl log"
./usercli log | tail -n 1 | grep -F "smoke test"

echo "checking heartbeat interval bounds"
if control_cmd ./usercli heartbeat interval 249 >/dev/null 2>&1; then
  echo "expected interval 249 to fail"
  exit 1
fi
control_cmd ./usercli heartbeat interval 250
control_cmd ./usercli heartbeat interval 60000
if control_cmd ./usercli heartbeat interval 60001 >/dev/null 2>&1; then
  echo "expected interval 60001 to fail"
  exit 1
fi
control_cmd ./usercli heartbeat interval 5000

echo "checking split resets"
control_cmd ./usercli reset-stats
control_cmd ./usercli reset-log
if [[ "$(./usercli log)" != "(empty)" ]]; then
  echo "expected empty log after reset-log"
  exit 1
fi

echo "reading updated state"
cat "${proc_path}"
