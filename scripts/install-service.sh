#!/usr/bin/env bash
# First-time service installation.  Run from Mac via: make install
# Requires the binary to already be on the Pi (run 'make deploy' first).
set -euo pipefail

PI_HOST="${PI_HOST:-raspberrypi.local}"
PI_USER="${PI_USER:-pi}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

if [ ! -f "${ROOT}/build/cross/cook" ]; then
    echo "ERROR: Binary not found at build/cross/cook"
    echo "       Run 'make deploy' first (which also installs the service)."
    exit 1
fi

echo "==> Uploading service file to ${PI_USER}@${PI_HOST}"
scp -q "${ROOT}/systemd/cook.service" "${PI_USER}@${PI_HOST}:~/cook.service"

echo "==> Installing and enabling systemd service"
ssh "${PI_USER}@${PI_HOST}" bash -s <<'REMOTE'
    sudo install -m 644 ~/cook.service /etc/systemd/system/cook.service
    rm -f ~/cook.service
    sudo systemctl daemon-reload
    sudo systemctl enable cook.service
    sudo systemctl start  cook.service
    sleep 2
    sudo systemctl status cook --no-pager
REMOTE
