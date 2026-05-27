#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"
tmp_out="/tmp/kernel-proc-lab-selftest.out.$$"
tmp_err="/tmp/kernel-proc-lab-selftest.err.$$"

# shellcheck source=scripts/module-common.sh
source "${repo_root}/scripts/module-common.sh"

fail() {
  echo "selftest failed: $*" >&2
  exit 1
}

require_loaded_module() {
  if ! is_module_loaded; then
    fail "module is not loaded; run: make load"
  fi
}

expect_fail() {
	if "$@" >"${tmp_out}" 2>"${tmp_err}"; then
    cat "${tmp_out}"
    fail "expected command to fail: $*"
	fi
}

control_cmd() {
	if [[ "${EUID}" -eq 0 ]]; then
		"$@"
	elif [[ -t 0 ]]; then
		sudo "$@"
	else
		if sudo -n true 2>/dev/null; then
			sudo "$@"
		else
			fail "privileged control tests need sudo; run make selftest in an interactive terminal"
		fi
	fi
}

require_loaded_module
ensure_device_node
[[ -e "${device_path}" ]] || fail "${device_path} does not exist"

./usercli doctor
control_cmd ./usercli reset-all

stats_json="$(./usercli stats --json)"
[[ "${stats_json}" == *'"abi_version":4'* ]] || fail "stats json missing abi_version=4"
[[ "${stats_json}" == *'"log_capacity":64'* ]] || fail "stats json missing log_capacity=64"

health_json="$(./usercli health --json)"
[[ "${health_json}" == *'"state":"READY"'* ]] || fail "health is not READY: ${health_json}"
./usercli health

./usercli write "selftest message"
read_output="$(./usercli read)"
[[ "${read_output}" == "selftest message" ]] || fail "read mismatch: ${read_output}"

log_output="$(./usercli log)"
[[ "${log_output}" == *"selftest message"* ]] || fail "log missing selftest message"

log_json="$(./usercli log --json)"
[[ "${log_json}" == *'"count":1'* ]] || fail "log json count mismatch: ${log_json}"
[[ "${log_json}" == *'"message":"selftest message"'* ]] || fail "log json message mismatch"
[[ "${log_json}" == *'"type_name":"write"'* ]] || fail "log json missing typed write event"

expect_fail control_cmd ./usercli heartbeat interval 249
control_cmd ./usercli heartbeat interval 250
control_cmd ./usercli heartbeat interval 60000
expect_fail control_cmd ./usercli heartbeat interval 60001
control_cmd ./usercli heartbeat interval 5000

control_cmd ./usercli heartbeat on
control_cmd ./usercli heartbeat off

control_cmd ./usercli reset-stats
post_reset_stats="$(./usercli stats --json)"
[[ "${post_reset_stats}" == *'"writes":0'* ]] || fail "reset-stats did not clear writes"

control_cmd ./usercli reset-log
[[ "$(./usercli log)" == "(empty)" ]] || fail "reset-log did not clear log"

./usercli write "stream event"
stream_output="$(./usercli stream --nonblock)"
[[ "${stream_output}" == *"stream event"* ]] || fail "stream --nonblock missing event"

control_cmd ./usercli reset-all
final_stats="$(./usercli stats --json)"
[[ "${final_stats}" == *'"log_events":0'* ]] || fail "reset-all did not clear log events"

rm -f "${tmp_out}" "${tmp_err}"
echo "selftest passed"
