#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

# shellcheck source=scripts/module-common.sh
source "${repo_root}/scripts/module-common.sh"

echo "kernel-proc-lab holders"
echo

if is_module_loaded; then
  lsmod | awk -v name="${module_name}" '$1 == name { printf "module: %s size=%s users=%s\n", $1, $2, $3 }'
else
  echo "module is not loaded"
fi

echo
echo "open files:"
if command -v fuser >/dev/null 2>&1; then
  sudo fuser -v "${device_path}" "${proc_path}" 2>&1 || true
else
  echo "fuser not found"
fi

echo
echo "likely related processes:"
ps -eo pid,ppid,stat,comm,args | awk '
  /labtop|usercli|kernel_proc_lab|trace_pipe/ && !/awk / {
    print
  }
'
