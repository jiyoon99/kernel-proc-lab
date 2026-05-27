#!/usr/bin/env bash
set -euo pipefail

bin_dir="${HOME}/.local/bin"

rm -f "${bin_dir}/labtop" "${bin_dir}/kernel-lab" "${bin_dir}/kernel-lab-collector"

echo "removed commands:"
echo "  ${bin_dir}/labtop"
echo "  ${bin_dir}/kernel-lab"
echo "  ${bin_dir}/kernel-lab-collector"
