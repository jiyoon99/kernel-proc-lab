#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
rule_name="99-kernel-proc-lab.rules"
source_rule="${repo_root}/udev/${rule_name}"
target_rule="/etc/udev/rules.d/${rule_name}"

sudo install -m 0644 "${source_rule}" "${target_rule}"
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=kernel_proc_lab || true

echo "installed ${target_rule}"
