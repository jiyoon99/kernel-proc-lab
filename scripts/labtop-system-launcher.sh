#!/usr/bin/env bash
set -euo pipefail

package_name="kernel-proc-lab"
package_version="0.8.0"
source_dir="/usr/src/${package_name}-${package_version}"
lib_dir="/usr/lib/kernel-proc-lab"
tui_binary="${lib_dir}/labtop"

say() {
  printf 'labtop: %s\n' "$*" >&2
}

if [[ ! -d "${source_dir}" ]]; then
  echo "missing DKMS source directory: ${source_dir}" >&2
  echo "reinstall the ${package_name} package" >&2
  exit 1
fi

if [[ ! -x "${tui_binary}" ]]; then
  echo "missing TUI binary: ${tui_binary}" >&2
  echo "reinstall the ${package_name} package" >&2
  exit 1
fi

cd "${source_dir}"

# shellcheck source=scripts/module-common.sh
source "${source_dir}/scripts/module-common.sh"

if ! is_module_loaded; then
  say "building kernel module"
  make kernel_proc_lab.ko >/dev/null
  say "loading kernel_proc_lab"
  MODULE_ARGS="${MODULE_ARGS:-initial_heartbeat_interval_ms=1000 start_heartbeat_on_load=1}" \
    ./scripts/load-module.sh
else
  say "kernel_proc_lab is already loaded"
  ensure_device_node
fi

ensure_device_node

say "starting TUI"
exec "${tui_binary}"
