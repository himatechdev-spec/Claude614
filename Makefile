# All compilation happens inside Docker on your Mac — the Pi only runs the binary.
# Prerequisites: Docker Desktop running on Mac, Pi reachable over SSH.

PI_HOST ?= raspberrypi.local
PI_USER ?= pi

export PI_HOST PI_USER

.PHONY: build deploy install rollback setup rt-kernel tune logs status start stop

## ── Build ────────────────────────────────────────────────────────────────────

# Cross-compile for aarch64 — output: build/cross/cook
build:
	@bash scripts/build.sh

## ── Deploy ───────────────────────────────────────────────────────────────────

# Build + upload + atomic swap on Pi (auto-rolls back if new binary fails to start)
deploy:
	@bash scripts/deploy.sh

# First-time: install the systemd unit so cook auto-starts on boot.
# Run 'make deploy' at least once before this.
install:
	@bash scripts/install-service.sh

# Revert to the previous binary on the Pi
rollback:
	@bash scripts/rollback.sh

## ── Pi first-time setup ──────────────────────────────────────────────────────

# Enable I2C, SPI, PWM, and full UART on the Pi (run once after first boot)
setup:
	@scp scripts/setup-pi.sh $(PI_USER)@$(PI_HOST):/tmp/setup-pi.sh
	@ssh $(PI_USER)@$(PI_HOST) "bash /tmp/setup-pi.sh"

# Install PREEMPT_RT kernel and configure the bootloader (run after 'make setup' reboot)
rt-kernel:
	@scp scripts/setup-rt-kernel.sh $(PI_USER)@$(PI_HOST):/tmp/setup-rt-kernel.sh
	@ssh $(PI_USER)@$(PI_HOST) "bash /tmp/setup-rt-kernel.sh"

# Apply OS-level RT tuning: CPU governor, IRQ affinity, disable noisy services
# Run once after rebooting into the PREEMPT_RT kernel
tune:
	@scp scripts/tune-rt.sh $(PI_USER)@$(PI_HOST):/tmp/tune-rt.sh
	@ssh $(PI_USER)@$(PI_HOST) "bash /tmp/tune-rt.sh"

## ── Service control ──────────────────────────────────────────────────────────

logs:
	@ssh $(PI_USER)@$(PI_HOST) "sudo journalctl -fu cook"

status:
	@ssh $(PI_USER)@$(PI_HOST) "sudo systemctl status cook --no-pager"

start:
	@ssh $(PI_USER)@$(PI_HOST) "sudo systemctl start cook"

stop:
	@ssh $(PI_USER)@$(PI_HOST) "sudo systemctl stop cook"
