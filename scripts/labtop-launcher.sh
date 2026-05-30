#!/usr/bin/env bash
set -euo pipefail

script_path="$(readlink -f "${BASH_SOURCE[0]}")"
repo_root="$(cd "$(dirname "${script_path}")/.." && pwd)"
cd "${repo_root}"

# shellcheck source=scripts/module-common.sh
source "${repo_root}/scripts/module-common.sh"

say() {
  printf 'labtop: %s\n' "$*" >&2
}

ensure_user_tools() {
  say "building user-space tools"
  make usercli labtop collector >/dev/null
}

ensure_collector_service() {
  local local_log_dir="${repo_root}/logs"
  local local_log_path="${local_log_dir}/events.jsonl"
  local pid_file="${local_log_dir}/collector.pid"

  if ! command -v systemctl >/dev/null 2>&1; then
    start_local_collector "${local_log_dir}" "${local_log_path}" "${pid_file}"
    return 0
  fi

  if systemctl is-active --quiet kernel-proc-lab-collector.service 2>/dev/null; then
    say "collector service is already active"
    return 0
  fi

  say "installing collector service"
  if ! ./scripts/install-collector-service.sh >/dev/null 2>&1; then
    say "system collector install unavailable; starting local collector"
    start_local_collector "${local_log_dir}" "${local_log_path}" "${pid_file}"
    return 0
  fi

  say "starting collector service"
  if ! sudo systemctl enable --now kernel-proc-lab-collector.service >/dev/null 2>&1; then
    say "system collector start unavailable; starting local collector"
    start_local_collector "${local_log_dir}" "${local_log_path}" "${pid_file}"
  fi
}

start_local_collector() {
  local log_dir="$1"
  local log_path="$2"
  local pid_file="$3"

  mkdir -p "${log_dir}"
  if [[ -s "${pid_file}" ]] && kill -0 "$(cat "${pid_file}")" 2>/dev/null; then
    say "local collector is already active"
    return 0
  fi

  say "starting local collector"
  nohup "${repo_root}/kernel-lab-collector" --output "${log_path}" \
    >>"${log_dir}/collector.out" 2>>"${log_dir}/collector.err" &
  echo "$!" >"${pid_file}"
}

ensure_user_tools

if ! is_module_loaded; then
  say "building kernel module and tools"
  make >/dev/null
  say "loading kernel_proc_lab"
  MODULE_ARGS="${MODULE_ARGS:-initial_heartbeat_interval_ms=1000 start_heartbeat_on_load=1}" \
    ./scripts/load-module.sh
else
  say "kernel_proc_lab is already loaded"
  ensure_device_node
fi

ensure_device_node
ensure_collector_service

say "starting TUI"
exec "${repo_root}/labtop"
