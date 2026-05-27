#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

# shellcheck source=scripts/module-common.sh
source "${repo_root}/scripts/module-common.sh"

count="${1:-100}"
prefix="load-test"

if ! [[ "${count}" =~ ^[0-9]+$ ]] || (( count < 1 )); then
  echo "usage: $0 [positive-count]"
  exit 2
fi

if ! is_module_loaded; then
  echo "module is not loaded"
  echo "run: make load"
  exit 1
fi

ensure_device_node
if [[ ! -e "${device_path}" ]]; then
  echo "${device_path} does not exist"
  exit 1
fi

before_writes="$(./usercli stats | awk -F': ' '$1 == "writes" { print $2 }')"

echo "writing ${count} messages"
for index in $(seq 1 "${count}"); do
  ./usercli write "${prefix}-${index}" >/dev/null
done

after_writes="$(./usercli stats | awk -F': ' '$1 == "writes" { print $2 }')"
delta=$((after_writes - before_writes))

if (( delta != count )); then
  echo "write counter mismatch: expected +${count}, got +${delta}"
  exit 1
fi

last_line="$(./usercli log | tail -n 1)"
expected_suffix="${prefix}-${count}"
if [[ "${last_line}" != *"${expected_suffix}" ]]; then
  echo "last log entry mismatch"
  echo "expected suffix: ${expected_suffix}"
  echo "actual: ${last_line}"
  exit 1
fi

echo "load test passed: writes +${delta}, last event ${expected_suffix}"
