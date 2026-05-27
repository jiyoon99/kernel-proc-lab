#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

# shellcheck source=scripts/module-common.sh
source "${repo_root}/scripts/module-common.sh"

reports_dir="${repo_root}/reports"
timestamp="$(date +%Y%m%d-%H%M%S)"
report_path="${reports_dir}/kernel-proc-lab-${timestamp}.txt"
json_report_path="${reports_dir}/kernel-proc-lab-${timestamp}.json"

mkdir -p "${reports_dir}"

section() {
  printf '\n## %s\n\n' "$1"
}

run_or_note() {
  local label="$1"
  shift

  section "${label}"
  if ! "$@" 2>&1; then
    printf '[command failed] %s\n' "$*"
  fi
}

{
  printf '# kernel-proc-lab report\n'
  printf 'generated: %s\n' "$(date -Is)"
  printf 'repo: %s\n' "${repo_root}"

  run_or_note "uname" uname -a
  run_or_note "compiler" cc --version
  run_or_note "module file" ls -l "${repo_root}/${module_file}"
  run_or_note "modinfo" modinfo "${repo_root}/${module_file}"
  run_or_note "lsmod" sh -c "lsmod | grep '^${module_name}' || true"
  run_or_note "proc status" sh -c "test -e '${proc_path}' && cat '${proc_path}' || echo '${proc_path} missing'"
  run_or_note "device node" sh -c "test -e '${device_path}' && ls -l '${device_path}' || echo '${device_path} missing'"
  run_or_note "sysfs" sh -c "test -d '/sys/class/${module_name}/${module_name}' && find -L '/sys/class/${module_name}/${module_name}' -maxdepth 1 -type f -print -exec cat {} \\; || echo 'sysfs missing'"
  run_or_note "debugfs" sh -c "if ! test -x /sys/kernel/debug; then echo 'debugfs mounted but inaccessible to current user'; elif test -d '/sys/kernel/debug/${module_name}'; then find '/sys/kernel/debug/${module_name}' -maxdepth 1 -type f -print -exec cat {} \\;; else echo 'debugfs module directory missing'; fi"
  run_or_note "trace events" sh -c "test -d '/sys/kernel/tracing/events/${module_name}' && find '/sys/kernel/tracing/events/${module_name}' -maxdepth 2 -type f -name format -print || echo 'trace events missing'"
  run_or_note "dmesg tail" sh -c "dmesg | grep '${module_name}' | tail -30 || true"
} >"${report_path}"

python3 - "${repo_root}" "${json_report_path}" <<'PY'
import json
import os
import subprocess
import sys
from pathlib import Path

repo = Path(sys.argv[1])
output = Path(sys.argv[2])
module = "kernel_proc_lab"


def run(args):
    completed = subprocess.run(
        args,
        cwd=repo,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    return {
        "command": args,
        "returncode": completed.returncode,
        "stdout": completed.stdout,
        "stderr": completed.stderr,
    }


def json_command(args):
    result = run(args)
    if result["returncode"] != 0:
        return {"error": result["stderr"] or result["stdout"], "raw": result}
    try:
        return json.loads(result["stdout"])
    except json.JSONDecodeError:
        return {"error": "invalid json", "raw": result}


def read_text(path):
    try:
        return Path(path).read_text()
    except OSError as exc:
        return f"{path}: {exc}"


payload = {
    "generated": run(["date", "-Is"])["stdout"].strip(),
    "repo": str(repo),
    "kernel": run(["uname", "-r"])["stdout"].strip(),
    "module": module,
    "module_loaded": run(["sh", "-c", f"lsmod | grep '^{module}'"])["returncode"] == 0,
    "module_file": run(["sh", "-c", "test -f kernel_proc_lab.ko && ls -l kernel_proc_lab.ko || true"])["stdout"].strip(),
    "modinfo": run(["sh", "-c", "test -f kernel_proc_lab.ko && modinfo kernel_proc_lab.ko || true"])["stdout"],
    "device_node": run(["sh", "-c", "test -e /dev/kernel_proc_lab && ls -l /dev/kernel_proc_lab || true"])["stdout"].strip(),
    "proc": read_text("/proc/kernel_proc_lab"),
    "stats": json_command(["./usercli", "stats", "--json"]),
    "config": json_command(["./usercli", "config", "--json"]),
    "log": json_command(["./usercli", "log", "--json"]),
    "ops_check": run(["./scripts/ops-check.sh"]),
    "doctor": run(["./scripts/doctor.sh"]),
}

output.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")
PY

echo "${report_path}"
echo "${json_report_path}"
