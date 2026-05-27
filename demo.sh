#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${repo_root}"

# shellcheck source=scripts/module-common.sh
source "${repo_root}/scripts/module-common.sh"

cleanup() {
  if is_module_loaded; then
    echo
    unload_module_if_loaded || true
  fi
}

trap cleanup EXIT

echo "== checking sudo =="
sudo -v

echo
echo "== building =="
make

if is_module_loaded; then
  echo
  unload_module_if_loaded
fi

echo
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

echo
echo "== proc status =="
cat "${proc_path}"

echo
echo "== write through /dev =="
echo "hello from demo.sh" | tee "${device_path}" >/dev/null

echo
echo "== read through usercli =="
./usercli read

echo
echo "== ioctl stats =="
./usercli stats

echo
echo "== clear message with ioctl =="
./usercli clear
./usercli read

echo
echo "== final proc status =="
cat "${proc_path}"
