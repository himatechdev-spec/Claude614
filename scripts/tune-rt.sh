#!/usr/bin/env bash
# Run once on the Pi after installing the PREEMPT_RT kernel.
# Applies OS-level tuning that persists across reboots.
set -euo pipefail

# ── 1. CPU frequency governor ────────────────────────────────────────────────
# Lock all cores to maximum frequency.  Variable frequency causes jitter because
# the processor stalls for a few microseconds while the PLL re-locks.
echo "==> Setting CPU governor to performance"
if [ -d /sys/devices/system/cpu/cpu0/cpufreq ]; then
    echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
else
    echo "    cpufreq not available yet — reboot into the RT kernel first, then re-run"
fi

sudo apt-get install -y --no-install-recommends cpufrequtils 2>/dev/null || true
echo 'GOVERNOR="performance"' | sudo tee /etc/default/cpufrequtils

# ── 2. Disable services that produce unnecessary CPU load or IRQs ─────────────
echo "==> Disabling non-essential services"
for svc in bluetooth avahi-daemon triggerhappy; do
    sudo systemctl disable --now "$svc" 2>/dev/null || true
done
# apt background update timers can fire at any time and spike CPU usage
sudo systemctl disable apt-daily.timer apt-daily-upgrade.timer 2>/dev/null || true

# ── 3. IRQ affinity service ───────────────────────────────────────────────────
# Move all interrupt handlers to cores 0-2, leaving core 3 uninterrupted.
# The isolcpus=3 kernel parameter stops the OS scheduler from placing tasks on
# core 3, but some IRQs may still fire there without this extra step.
echo "==> Installing irq-affinity.service"
sudo tee /etc/systemd/system/irq-affinity.service > /dev/null <<'UNIT'
[Unit]
Description=Move all IRQs away from isolated CPU core 3
After=local-fs.target
DefaultDependencies=no

[Service]
Type=oneshot
ExecStart=/bin/sh -c 'for f in /proc/irq/*/smp_affinity_list; do echo 0-2 > $f 2>/dev/null || true; done'
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
UNIT

sudo systemctl daemon-reload
sudo systemctl enable --now irq-affinity.service

# ── 4. Install rt-tests (cyclictest) ─────────────────────────────────────────
echo "==> Installing rt-tests (cyclictest)"
sudo apt-get install -y --no-install-recommends rt-tests

echo ""
echo "==> RT tuning complete."
echo ""
echo "    Verify the RT kernel is running:"
echo "      uname -a  (should contain 'PREEMPT RT')"
echo ""
echo "    Measure worst-case loop jitter (~30 seconds):"
echo "      sudo cyclictest -l 300000 -m -n -a 3 -t 1 -p 99 -i 10000 -h 200 -q"
echo "    Target: Max latency < 100 µs"
