#!/bin/sh
set -eu

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PROJECT_DIR="$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)"
SERVICE_FILE="$PROJECT_DIR/boot/env_control.service"

if [ ! -f "$SERVICE_FILE" ]; then
    echo "service file not found: $SERVICE_FILE"
    exit 1
fi

sudo install -m 644 "$SERVICE_FILE" /etc/systemd/system/env_control.service
sudo systemctl daemon-reload
sudo systemctl enable env_control.service
sudo systemctl restart env_control.service

echo "env_control.service installed and started"
sudo systemctl status env_control.service --no-pager -l || true