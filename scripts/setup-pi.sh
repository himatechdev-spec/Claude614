#!/usr/bin/env bash
# Run once on the Raspberry Pi to enable hardware interfaces.
# Build tools are NOT needed — the binary is cross-compiled on the Mac.
set -euo pipefail

# Determine the real config path. On modern Raspberry Pi OS (Bookworm+),
# /boot/config.txt is only a stub note; the real file is /boot/firmware/config.txt.
CONFIG=/boot/firmware/config.txt
if [ -f /boot/config.txt ] && ! grep -q "moved to" /boot/config.txt 2>/dev/null; then
    CONFIG=/boot/config.txt   # genuine older Raspbian config
fi

echo "==> Enabling hardware interfaces in ${CONFIG}"
grep -q "^dtparam=i2c_arm=on"   "$CONFIG" || echo "dtparam=i2c_arm=on"   | sudo tee -a "$CONFIG"
grep -q "^dtparam=spi=on"       "$CONFIG" || echo "dtparam=spi=on"        | sudo tee -a "$CONFIG"
grep -q "^dtoverlay=pwm-2chan"   "$CONFIG" || echo "dtoverlay=pwm-2chan,pin=18,func=2,pin2=19,func2=2" | sudo tee -a "$CONFIG"

echo "==> Swapping UART: disabling Bluetooth to give BCM14/15 the full UART"
grep -q "^dtoverlay=disable-bt" "$CONFIG" || echo "dtoverlay=disable-bt"  | sudo tee -a "$CONFIG"
sudo systemctl disable --now hciuart 2>/dev/null || true

echo ""
echo "==> Done."
echo "    Reboot to activate all settings: sudo reboot"
echo ""
echo "    After reboot, interface devices will be:"
echo "      I2C  : /dev/i2c-1      (SDA=BCM2/pin3, SCL=BCM3/pin5)"
echo "      SPI  : /dev/spidev0.0  (MOSI=BCM10, MISO=BCM9, SCLK=BCM11, CE0=BCM8)"
echo "      UART : /dev/ttyAMA0    (TX=BCM14/pin8, RX=BCM15/pin10)"
echo "      PWM  : BCM18/pin12 (CH0), BCM19/pin35 (CH1)"
