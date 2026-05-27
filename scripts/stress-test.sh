#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

# shellcheck source=scripts/module-common.sh
source "${repo_root}/scripts/module-common.sh"

fail() {
  echo "stress-test failed: $*" >&2
  exit 1
}

can_control() {
  [[ "${EUID}" -eq 0 ]] || sudo -n true 2>/dev/null
}

control_cmd() {
  if [[ "${EUID}" -eq 0 ]]; then
    "$@"
  elif [[ -t 0 ]]; then
    sudo "$@"
  elif sudo -n true 2>/dev/null; then
    sudo "$@"
  else
    return 125
  fi
}

if ! is_module_loaded; then
  fail "module is not loaded; run: make load"
fi

ensure_device_node
if can_control; then
  control_cmd ./usercli reset-all >/dev/null
else
  echo "stress-test: skipping privileged reset/heartbeat/filter checks; sudo is unavailable"
fi

iterations="${KERNEL_PROC_LAB_STRESS_ITERATIONS:-200}"
stream_out="/tmp/kernel-proc-lab-stream-stress.$$.log"
rm -f "${stream_out}"

timeout 10 ./usercli stream >"${stream_out}" &
stream_pid=$!

for index in $(seq 1 "${iterations}"); do
  ./usercli write "stress event ${index}" >/dev/null
  if can_control && (( index % 25 == 0 )); then
    control_cmd ./usercli heartbeat interval 250 >/dev/null
    control_cmd ./usercli heartbeat on >/dev/null
    control_cmd ./usercli heartbeat off >/dev/null
  fi
  if can_control && (( index % 40 == 0 )); then
    control_cmd ./usercli filter set type=1 >/dev/null
    control_cmd ./usercli filter clear >/dev/null
  fi
  if can_control && (( index % 75 == 0 )); then
    control_cmd ./usercli reset-stats >/dev/null
  fi
done

sleep 0.5
kill -TERM "${stream_pid}" 2>/dev/null || true
wait "${stream_pid}" 2>/dev/null || true

./usercli doctor >/dev/null
health_json="$(./usercli health --json || true)"
grep -Fq '"abi_version":4' <<<"${health_json}" || fail "health ABI missing"
grep -Fq "stress event ${iterations}" "${stream_out}" || fail "stream missed final event"
rm -f "${stream_out}"

if can_control; then
  control_cmd ./usercli reset-all >/dev/null
fi
echo "stress-test passed: iterations=${iterations}"
