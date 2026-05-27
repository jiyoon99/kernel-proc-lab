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

ensure_labtop_binary() {
  if [[ ! -x "${repo_root}/labtop" ]]; then
    say "building user-space monitor"
    make labtop >/dev/null
  fi
}

if ! is_module_loaded; then
  say "building kernel module and tools"
  make >/dev/null
  say "loading kernel_proc_lab"
  MODULE_ARGS="${MODULE_ARGS:-initial_heartbeat_interval_ms=1000 start_heartbeat_on_load=1}" \
    ./scripts/load-module.sh
else
  ensure_labtop_binary
  say "kernel_proc_lab is already loaded"
  ensure_device_node
fi

ensure_labtop_binary
ensure_device_node

say "starting TUI"
exec "${repo_root}/labtop"
