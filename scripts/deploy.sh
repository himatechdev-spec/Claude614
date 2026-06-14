#!/usr/bin/env bash
# Cross-compile on Mac → upload binary → atomic swap on Pi → health-check with auto-rollback.
# Usage:  ./scripts/deploy.sh
#         PI_HOST=mypi.local PI_USER=pi ./scripts/deploy.sh
set -euo pipefail

PI_HOST="${PI_HOST:-raspberrypi.local}"
PI_USER="${PI_USER:-pi}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BINARY="${ROOT}/build/cross/cook"
SERVICE_FILE="${ROOT}/systemd/cook.service"

# ── Step 1: cross-compile ────────────────────────────────────────────────────
echo "==> [1/3] Cross-compiling"
bash "${ROOT}/scripts/build.sh"

# ── Step 2: upload ───────────────────────────────────────────────────────────
echo "==> [2/3] Uploading binary to ${PI_USER}@${PI_HOST}"
scp -q "$BINARY"       "${PI_USER}@${PI_HOST}:~/cook.new"
scp -q "$SERVICE_FILE" "${PI_USER}@${PI_HOST}:~/cook.service"

# ── Step 3: atomic swap + health check with auto-rollback ────────────────────
echo "==> [3/3] Atomic swap and restart"
ssh "${PI_USER}@${PI_HOST}" bash -s <<'REMOTE'
    set -e

    # Keep the current binary as a fallback.
    [ -f /usr/local/bin/cook ] && sudo cp /usr/local/bin/cook /usr/local/bin/cook.prev

    # Stage on the same filesystem as the destination, then rename atomically.
    # (mv across filesystems is not atomic; same-fs rename is a single syscall.)
    sudo cp  ~/cook.new /usr/local/bin/cook.staging
    sudo chmod 755      /usr/local/bin/cook.staging
    sudo mv             /usr/local/bin/cook.staging /usr/local/bin/cook
    rm -f ~/cook.new

    # Refresh the service file in case it changed.
    sudo install -m 644 ~/cook.service /etc/systemd/system/cook.service
    rm -f ~/cook.service
    sudo systemctl daemon-reload

    sudo systemctl restart cook

    # ── Health check ─────────────────────────────────────────────────────────
    echo -n "    Waiting for cook"
    for i in $(seq 1 10); do
        sleep 1
        echo -n "."
        if sudo systemctl is-active --quiet cook; then
            echo " OK"
            sudo systemctl status cook --no-pager -l | tail -5
            exit 0
        fi
    done

    # Service did not start — roll back automatically.
    echo " FAILED"
    echo "!!! cook did not start — rolling back"
    if [ -f /usr/local/bin/cook.prev ]; then
        sudo mv /usr/local/bin/cook.prev /usr/local/bin/cook
        sudo systemctl restart cook
        echo "!!! Previous binary restored.  Inspect logs: journalctl -u cook -n 60"
    else
        echo "!!! No previous binary — cook is stopped."
    fi
    exit 1
REMOTE
