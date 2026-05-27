#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

fail() {
  echo "release-check failed: $*" >&2
  exit 1
}

./scripts/version-check.sh >/dev/null

for target in ci-check runtime-smoke stress-test kernel-build-check version-check release-check; do
  grep -Eq "^${target}:" Makefile || fail "missing Makefile target: ${target}"
done

grep -Fq "SIGHUP" collector.c || fail "collector does not handle SIGHUP"
grep -Fq "systemctl kill -s HUP kernel-proc-lab-collector.service" logrotate/kernel-proc-lab || fail "logrotate does not notify collector"
grep -Fq "kernel_proc_lab_version.h" scripts/install-dkms.sh || fail "DKMS install omits version header"
grep -Fq "make runtime-smoke" README.md || fail "README missing runtime-smoke"
grep -Fq "make stress-test" README.md || fail "README missing stress-test"
grep -Fq "docs/release.md" README.md || fail "README missing release checklist link"
grep -Fq "make ci-check" docs/release.md || fail "release checklist missing ci-check"
grep -Fq "make runtime-smoke" docs/release.md || fail "release checklist missing runtime-smoke"
grep -Fq '"abi_version": 4' docs/json-schema.md || fail "JSON schema ABI is stale"

echo "release-check passed"
