#!/usr/bin/env bash
set -euo pipefail

sudo systemctl disable --now kernel-proc-lab-collector.service 2>/dev/null || true
sudo rm -f /etc/systemd/system/kernel-proc-lab-collector.service
sudo rm -f /etc/logrotate.d/kernel-proc-lab
sudo rm -f /usr/local/bin/kernel-lab-collector
sudo systemctl daemon-reload

echo "collector service uninstalled"
