#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

# shellcheck source=scripts/module-common.sh
source "${repo_root}/scripts/module-common.sh"

if ! is_module_loaded; then
  echo "module is not loaded"
  echo "run: make load"
  exit 1
fi

ensure_device_node

if [[ ! -e "${device_path}" ]]; then
  echo "missing ${device_path}; unable to determine device major/minor"
  exit 1
fi

ls -l "${device_path}"
