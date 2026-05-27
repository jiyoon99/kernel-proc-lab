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

echo "== install or refresh udev rule =="
./scripts/install-udev-rule.sh

echo
echo "== ensure device node =="
ensure_device_node

if [[ -e "${device_path}" ]]; then
  ls -l "${device_path}"
else
  echo "missing ${device_path}"
  exit 1
fi
