#!/usr/bin/env bash
set -euo pipefail

package_name="kernel-proc-lab"
package_version="0.8.0"
source_dir="/usr/src/${package_name}-${package_version}"

if command -v dkms >/dev/null 2>&1; then
  if sudo dkms status -m "${package_name}" -v "${package_version}" >/dev/null 2>&1; then
    echo "== removing DKMS module =="
    sudo dkms remove -m "${package_name}" -v "${package_version}" --all
  else
    echo "== DKMS module is not registered =="
  fi
else
  echo "dkms is not installed; skipping dkms remove"
fi

if [[ -d "${source_dir}" ]]; then
  echo "== removing ${source_dir} =="
  sudo rm -rf "${source_dir}"
fi

echo "DKMS uninstall complete"
