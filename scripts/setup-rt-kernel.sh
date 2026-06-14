#!/usr/bin/env bash
# Install the PREEMPT_RT kernel and configure the Pi bootloader automatically.
# Run via: make rt-kernel
set -euo pipefail

# ── 1. Install RT kernel package ─────────────────────────────────────────────
echo "==> Checking package availability"
sudo apt-get update -q

if ! apt-cache show linux-image-rt-arm64 &>/dev/null; then
    echo "ERROR: linux-image-rt-arm64 not in apt repos."
    echo "       Confirm you are running Raspberry Pi OS Bookworm 64-bit: uname -m"
    exit 1
fi

echo "==> Installing PREEMPT_RT kernel and headers"
sudo apt-get install -y linux-image-rt-arm64 linux-headers-rt-arm64

# ── 2. Locate the installed kernel files ─────────────────────────────────────
RT_KERNEL=$(ls /boot/vmlinuz-*rt* 2>/dev/null | sort -V | tail -1 | xargs -I{} basename {})
RT_INITRD=$(ls /boot/initrd.img-*rt* 2>/dev/null | sort -V | tail -1 | xargs -I{} basename {} || true)

if [ -z "$RT_KERNEL" ]; then
    echo "ERROR: No RT kernel image found in /boot after install."
    exit 1
fi

echo "    Kernel : $RT_KERNEL"
[ -n "$RT_INITRD" ] && echo "    Initrd : $RT_INITRD"

# ── 3. Configure Pi bootloader ────────────────────────────────────────────────
CONFIG=/boot/firmware/config.txt
if [ -f /boot/config.txt ] && ! grep -q "moved to" /boot/config.txt 2>/dev/null; then
    CONFIG=/boot/config.txt   # genuine older Raspbian config
fi

# Remove any previous RT entry so re-running this script stays idempotent.
sudo sed -i '/^# PREEMPT_RT kernel/d;/^kernel=.*rt/d;/^initramfs.*rt/d' "$CONFIG"

printf '\n# PREEMPT_RT kernel\nkernel=%s\n' "$RT_KERNEL" | sudo tee -a "$CONFIG"
[ -n "$RT_INITRD" ] && printf 'initramfs %s followkernel\n' "$RT_INITRD" | sudo tee -a "$CONFIG"

# ── 4. Copy kernel + initrd into the FAT32 firmware partition ────────────────
# Debian's post-install scripts skip this for non-RPi kernels — do it manually.
echo "==> Copying kernel and initrd to /boot/firmware/"
sudo cp "/boot/${RT_KERNEL}" /boot/firmware/
[ -n "$RT_INITRD" ] && sudo cp "/boot/${RT_INITRD}" /boot/firmware/

# ── 5. Add RT isolation parameters to kernel command line ────────────────────
# The file must remain a single line — sed appends in place.
CMDLINE=/boot/firmware/cmdline.txt
if [ -f /boot/cmdline.txt ] && ! grep -q "moved to" /boot/cmdline.txt 2>/dev/null; then
    CMDLINE=/boot/cmdline.txt   # genuine older Raspbian cmdline
fi

if ! grep -q "isolcpus=3" "$CMDLINE"; then
    sudo sed -i 's/$/ isolcpus=3 nohz_full=3 rcu_nocbs=3/' "$CMDLINE"
    echo "==> RT isolation parameters added to $CMDLINE"
else
    echo "==> RT isolation parameters already present in $CMDLINE"
fi

# ── 5. Print summary ─────────────────────────────────────────────────────────
echo ""
echo "==> ${CONFIG} (last 5 lines):"
tail -5 "$CONFIG"
echo ""
echo "==> ${CMDLINE}:"
cat "$CMDLINE"
echo ""
echo "==> Done.  Reboot to activate the RT kernel:"
echo "    sudo reboot"
