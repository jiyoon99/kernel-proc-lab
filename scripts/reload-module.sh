#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

# shellcheck source=scripts/module-common.sh
source "${repo_root}/scripts/module-common.sh"

make
unload_module_if_loaded
load_module
