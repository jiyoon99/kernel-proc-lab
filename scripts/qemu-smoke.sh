#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

cat <<'EOF'
kernel-proc-lab QEMU smoke skeleton

Expected flow:
  1. boot a kernel with matching headers in QEMU
  2. copy this tree into the guest
  3. run: make
  4. run: sudo insmod kernel_proc_lab.ko
  5. run: make selftest
  6. run: sudo rmmod kernel_proc_lab

This script is a placeholder until a project-specific kernel image and rootfs
are selected.
EOF
