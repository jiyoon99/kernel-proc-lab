#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

# shellcheck source=scripts/module-common.sh
source "${repo_root}/scripts/module-common.sh"

fail() {
  echo "runtime-smoke failed: $*" >&2
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
[[ -r "${proc_path}" ]] || fail "${proc_path} is not readable"
[[ -r "${device_path}" && -w "${device_path}" ]] || fail "${device_path} is not read/write"

if can_control; then
  control_cmd ./usercli reset-all >/dev/null
else
  echo "runtime-smoke: skipping privileged reset/setup checks; sudo is unavailable"
fi
./usercli doctor >/dev/null

message="runtime smoke $$"
./usercli write "${message}"
[[ "$(./usercli read)" == "${message}" ]] || fail "read path mismatch"
./usercli stats --json | grep -Fq '"abi_version":4' || fail "stats ABI missing"
./usercli log --json | grep -Fq "\"message\":\"${message}\"" || fail "log JSON missing message"

stream_log="/tmp/kernel-proc-lab-stream-smoke.$$.jsonl"
rm -f "${stream_log}"
timeout 2 ./usercli stream --json >"${stream_log}" &
stream_pid=$!
sleep 0.2
stream_message="stream smoke $$"
./usercli write "${stream_message}"
sleep 0.3
kill -TERM "${stream_pid}" 2>/dev/null || true
wait "${stream_pid}" 2>/dev/null || true
grep -Fq "\"message\":\"${stream_message}\"" "${stream_log}" || fail "stream JSON missing message"
rm -f "${stream_log}"

tmp_log="/tmp/kernel-proc-lab-collector-smoke.$$.jsonl"
rm -f "${tmp_log}"
timeout 2 ./kernel-lab-collector --output "${tmp_log}" &
collector_pid=$!
sleep 0.2
./usercli write "collector smoke $$"
sleep 0.3
kill -TERM "${collector_pid}" 2>/dev/null || true
wait "${collector_pid}" 2>/dev/null || true
grep -Fq "collector smoke $$" "${tmp_log}" || fail "collector did not persist event"
rm -f "${tmp_log}"

if can_control; then
  control_cmd ./usercli reset-log >/dev/null
  [[ "$(./usercli log)" == "(empty)" ]] || fail "reset-log failed"
fi

echo "runtime-smoke passed"
