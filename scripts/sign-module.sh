#!/usr/bin/env bash
set -euo pipefail

module_path="${1:-kernel_proc_lab.ko}"
sign_file="/lib/modules/$(uname -r)/build/scripts/sign-file"
key_path="certs/MOK.priv"
cert_path="certs/MOK.der"

if [[ ! -x "${sign_file}" ]]; then
  echo "missing sign-file: ${sign_file}"
  exit 1
fi

if [[ ! -f "${key_path}" || ! -f "${cert_path}" ]]; then
  echo "missing signing key. run: ./scripts/create-mok-key.sh"
  exit 1
fi

if [[ ! -f "${module_path}" ]]; then
  echo "missing module: ${module_path}"
  exit 1
fi

"${sign_file}" sha256 "${key_path}" "${cert_path}" "${module_path}"
echo "signed ${module_path}"
