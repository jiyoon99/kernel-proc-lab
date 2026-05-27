#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

make collector
sudo install -D -m 0755 kernel-lab-collector /usr/local/bin/kernel-lab-collector
sudo install -D -m 0644 systemd/kernel-proc-lab-collector.service /etc/systemd/system/kernel-proc-lab-collector.service
sudo install -D -m 0644 logrotate/kernel-proc-lab /etc/logrotate.d/kernel-proc-lab
sudo mkdir -p /var/log/kernel-proc-lab
sudo systemctl daemon-reload

echo "collector service installed"
echo "start with: sudo systemctl enable --now kernel-proc-lab-collector.service"
