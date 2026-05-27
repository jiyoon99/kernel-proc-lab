#!/usr/bin/env bash
set -euo pipefail

script_path="$(readlink -f "${BASH_SOURCE[0]}")"
repo_root="$(cd "$(dirname "${script_path}")/.." && pwd)"
cd "${repo_root}"

# shellcheck source=scripts/module-common.sh
source "${repo_root}/scripts/module-common.sh"

if ! is_module_loaded; then
  make >/dev/null
  MODULE_ARGS="${MODULE_ARGS:-initial_heartbeat_interval_ms=1000 start_heartbeat_on_load=1}" \
    ./scripts/load-module.sh
else
  make usercli labtop >/dev/null
  ensure_device_node
fi

exec "${repo_root}/labtop"
