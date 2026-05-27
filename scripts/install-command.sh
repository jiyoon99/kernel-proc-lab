#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
bin_dir="${HOME}/.local/bin"

cd "${repo_root}"
make usercli labtop collector
mkdir -p "${bin_dir}"
ln -sfn "${repo_root}/scripts/labtop-launcher.sh" "${bin_dir}/labtop"
ln -sfn "${repo_root}/run" "${bin_dir}/kernel-lab"
ln -sfn "${repo_root}/kernel-lab-collector" "${bin_dir}/kernel-lab-collector"

echo "installed commands:"
echo "  ${bin_dir}/labtop"
echo "  ${bin_dir}/kernel-lab"
echo "  ${bin_dir}/kernel-lab-collector"
