#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

# shellcheck source=scripts/module-common.sh
source "${repo_root}/scripts/module-common.sh"

load_module

if [[ ! -e "${proc_path}" ]]; then
  echo "missing ${proc_path}; module may not have initialized correctly"
  exit 1
fi

if [[ ! -e "${device_path}" ]]; then
  echo "missing ${device_path}; udev did not create the device node"
  echo "try: sudo dmesg | tail -50"
  exit 1
fi
