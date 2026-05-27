#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

package_name="kernel-proc-lab"
package_version="0.8.0"
source_dir="/usr/src/${package_name}-${package_version}"

if ! command -v dkms >/dev/null 2>&1; then
  echo "dkms is not installed"
  echo "install it first, for example: sudo apt-get install dkms"
  exit 1
fi

echo "== installing DKMS source to ${source_dir} =="
sudo rm -rf "${source_dir}"
sudo mkdir -p "${source_dir}"
sudo cp -a \
  Makefile \
  dkms.conf \
  kernel_proc_lab.c \
  kernel_proc_lab_ioctl.h \
  kernel_proc_lab_ring.h \
  kernel_proc_lab_version.h \
  trace \
  "${source_dir}/"

if sudo dkms status -m "${package_name}" -v "${package_version}" >/dev/null 2>&1; then
  echo "== removing existing DKMS registration =="
  sudo dkms remove -m "${package_name}" -v "${package_version}" --all || true
fi

echo "== adding DKMS module =="
sudo dkms add -m "${package_name}" -v "${package_version}"

echo "== building DKMS module =="
sudo dkms build -m "${package_name}" -v "${package_version}"

echo "== installing DKMS module =="
sudo dkms install -m "${package_name}" -v "${package_version}"

echo "DKMS install complete"
sudo dkms status -m "${package_name}" -v "${package_version}" || true
