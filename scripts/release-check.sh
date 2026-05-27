#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

fail() {
  echo "release-check failed: $*" >&2
  exit 1
}

./scripts/version-check.sh >/dev/null

for target in ci-check runtime-smoke stress-test kernel-build-check version-check release-check packaging-check; do
  grep -Eq "^${target}:" Makefile || fail "missing Makefile target: ${target}"
done

grep -Fq "SIGHUP" collector.c || fail "collector does not handle SIGHUP"
grep -Fq "systemctl kill -s HUP kernel-proc-lab-collector.service" logrotate/kernel-proc-lab || fail "logrotate does not notify collector"
grep -Fq "kernel_proc_lab_version.h" scripts/install-dkms.sh || fail "DKMS install omits version header"
grep -Fq "dpkg-buildpackage -us -uc -b" README.md || fail "README missing Debian package build command"
grep -Fq "sudo apt install build-essential debhelper dkms" README.md || fail "README missing Debian build dependencies"
grep -Fq "docs/assets/kernel-proc-lab-hero.png" README.md || fail "README missing hero image"
grep -Fq "docs/assets/labtop-preview.svg" README.md || fail "README missing labtop preview"
grep -Fq "docs/assets/architecture.svg" README.md || fail "README missing architecture diagram"
grep -Fq "SECURITY.md" README.md || fail "README missing security policy link"
grep -Fq "docs/testing.md" README.md || fail "README missing testing guide link"
grep -Fq "make runtime-smoke" README.md || fail "README missing runtime-smoke"
grep -Fq "make stress-test" README.md || fail "README missing stress-test"
grep -Fq "docs/release.md" README.md || fail "README missing release checklist link"
grep -Fq "docs/distribution.md" README.md || fail "README missing distribution guide link"
grep -Fq "make ci-check" docs/release.md || fail "release checklist missing ci-check"
grep -Fq "make runtime-smoke" docs/release.md || fail "release checklist missing runtime-smoke"
grep -Fq "dpkg-buildpackage -us -uc -b" docs/release.md || fail "release checklist missing Debian build"
grep -Fq "sudo apt install build-essential debhelper dkms" docs/packaging.md || fail "packaging docs missing Debian build dependencies"
grep -Fq "git tag -a v0.8.0" docs/distribution.md || fail "distribution guide missing tag flow"
grep -Fq "debian/" docs/distribution.md || fail "distribution guide missing Debian packaging"
grep -Fq "KUnit" docs/testing.md || fail "testing guide missing KUnit notes"
grep -Fq "QEMU" docs/testing.md || fail "testing guide missing QEMU notes"
grep -Fq "Secure Boot" SECURITY.md || fail "security policy missing Secure Boot notes"
grep -Fq "Kernel Proc Lab v0.8.0" docs/release-notes-v0.8.0.md || fail "release notes missing version title"
grep -Fq "make install-command" docs/release-notes-v0.8.0.md || fail "release notes missing source install command"
grep -Fq "dpkg-buildpackage -us -uc -b" docs/release-notes-v0.8.0.md || fail "release notes missing Debian package build"
grep -Fq "3.0 (native)" debian/source/format || fail "Debian source format missing"
grep -Fq "/usr/src/kernel-proc-lab-0.8.0" debian/rules || fail "Debian rules missing DKMS source install"
grep -Fq "dkms add -m kernel-proc-lab -v 0.8.0" debian/postinst || fail "Debian postinst missing DKMS add"
test -s docs/assets/kernel-proc-lab-hero.png || fail "hero image asset missing"
test -s docs/assets/labtop-preview.svg || fail "labtop preview asset missing"
test -s docs/assets/architecture.svg || fail "architecture asset missing"
grep -Fq '"abi_version": 4' docs/json-schema.md || fail "JSON schema ABI is stale"

echo "release-check passed"
