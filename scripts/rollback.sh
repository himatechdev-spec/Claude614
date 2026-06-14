#!/usr/bin/env bash
# Manually revert to the previous binary on the Pi.
# Use this when the new version has a bug that was not caught by the deploy health-check.
set -euo pipefail

PI_HOST="${PI_HOST:-raspberrypi.local}"
PI_USER="${PI_USER:-pi}"

echo "==> Rolling back cook on ${PI_HOST}"
ssh "${PI_USER}@${PI_HOST}" bash -s <<'REMOTE'
    if [ ! -f /usr/local/bin/cook.prev ]; then
        echo "ERROR: No previous binary at /usr/local/bin/cook.prev"
        echo "       Nothing to roll back to."
        exit 1
    fi

    # Archive the bad binary so it can be inspected.
    sudo mv /usr/local/bin/cook      /usr/local/bin/cook.bad  2>/dev/null || true
    sudo mv /usr/local/bin/cook.prev /usr/local/bin/cook

    sudo systemctl restart cook
    sleep 3
    sudo systemctl status cook --no-pager -l | tail -6
    echo ""
    echo "==> Rolled back.  Broken binary saved as /usr/local/bin/cook.bad"
REMOTE
