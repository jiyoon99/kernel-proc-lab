#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

version="$(awk -F'"' '$2 != "" && $0 ~ /^#define KERNEL_PROC_LAB_VERSION / { print $2 }' kernel_proc_lab_version.h)"
abi="$(awk '/KERNEL_PROC_LAB_ABI_VERSION/ { print $3 }' kernel_proc_lab_ioctl.h)"

fail() {
  echo "version-check failed: $*" >&2
  exit 1
}

[[ -n "${version}" ]] || fail "missing KERNEL_PROC_LAB_VERSION"
[[ -n "${abi}" ]] || fail "missing KERNEL_PROC_LAB_ABI_VERSION"

grep -Fq "PACKAGE_VERSION=\"${version}\"" dkms.conf || fail "dkms.conf version mismatch"
grep -Fq "package_version=\"${version}\"" scripts/install-dkms.sh || fail "install-dkms version mismatch"
grep -Fq "package_version=\"${version}\"" scripts/uninstall-dkms.sh || fail "uninstall-dkms version mismatch"
grep -Fq "dkms status -m kernel-proc-lab -v ${version}" Makefile || fail "Makefile dkms-status mismatch"
grep -Fq "## ${version}" CHANGELOG.md || fail "CHANGELOG missing ${version}"
grep -Fq "버전은 \`${version}\`" README.md || fail "README version summary mismatch"
grep -Fq "ABI 버전은 \`${abi}\`" README.md || fail "README ABI summary mismatch"

echo "version-check passed: version=${version} abi=${abi}"
