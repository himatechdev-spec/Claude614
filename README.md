# Cook — RPi 3B+ Industrial Automation Controller

A real-time C++ controller for the Raspberry Pi 3B+, targeting industrial automation workloads.  
Cross-compiled on macOS via Docker; deployed over SSH with atomic swap and auto-rollback.

## Features

- **PREEMPT_RT** kernel, SCHED_FIFO/90 on isolated core 3 — deterministic 10 ms control loop
- **RS485 Modbus RTU** master over `/dev/ttyAMA0` with DE/RE direction control (BCM4)
- **GPIO, I2C, SPI, PWM, UART** peripheral drivers
- **Systemd service** with OOM protection, restart-on-failure, and CPU affinity enforcement
- **Docker cross-compilation** — no build tools required on the Pi

## Hardware

| Peripheral | Pi header pin | BCM | Device |
|---|---|---|---|
| RS485 TX | 8 | 14 | `/dev/ttyAMA0` DI |
| RS485 RX | 10 | 15 | `/dev/ttyAMA0` RO |
| RS485 DE+RE | 7 | 4 | Transceiver direction |
| Status LED | 11 | 17 | LED + 330 Ω to GND |
| E-stop | 13 | 27 | Button to GND (active-low) |
| I2C SDA | 3 | 2 | `/dev/i2c-1` |
| I2C SCL | 5 | 3 | `/dev/i2c-1` |
| SPI MOSI | 19 | 10 | `/dev/spidev0.0` |
| SPI MISO | 21 | 9 | `/dev/spidev0.0` |
| SPI SCLK | 23 | 11 | `/dev/spidev0.0` |
| PWM CH0 | 12 | 18 | Hardware PWM |
| PWM CH1 | 35 | 19 | Hardware PWM |

Use a half-duplex RS485 transceiver (MAX485 / SN75176 / SP3485).  
Terminate long bus runs with 120 Ω between A and B at each end.

## Prerequisites

**macOS (dev machine)**
- Docker Desktop (running)
- SSH access to the Pi (`pi@<ip>`)

**Raspberry Pi 3B+**
- Raspberry Pi OS Bookworm (64-bit)
- PREEMPT_RT kernel (installed via `make rt-kernel`)

## First-time Pi setup

Run these once in order after a fresh Pi OS install:

```bash
# 1. Enable hardware interfaces and free the full UART from Bluetooth
PI_HOST=<pi-ip> make setup
# → reboot the Pi

# 2. Install the PREEMPT_RT kernel
PI_HOST=<pi-ip> make rt-kernel
# → reboot into the RT kernel

# 3. Apply OS-level RT tuning (CPU governor, IRQ affinity)
PI_HOST=<pi-ip> make tune

# 4. Disable the serial console so /dev/ttyAMA0 is dedicated to RS485
PI_HOST=<pi-ip> ssh pi@<pi-ip> "sudo systemctl disable --now serial-getty@ttyAMA0.service"
PI_HOST=<pi-ip> ssh pi@<pi-ip> "sudo sed -i 's/console=serial0,[0-9]* //' /boot/firmware/cmdline.txt && sudo reboot"
```

> **Why disable the serial console?**  
> By default, Raspberry Pi OS runs a getty login prompt on `/dev/ttyAMA0` and the kernel
> uses it as a console.  Any byte containing `0x0A` (LF) received from an RS485 slave
> triggers the login banner, which is then transmitted back onto the RS485 bus and corrupts
> subsequent frames.  The getty also re-enables ONLCR (LF→CRLF translation), inserting
> a spurious `0x0D` before every `0x0A` byte in transmitted frames — including the register
> address byte `0x000A` (register 10).

## Build and deploy

```bash
# Cross-compile, upload, and hot-swap on the Pi (auto-rolls back if new binary fails)
PI_HOST=<pi-ip> make deploy

# First-time only: install the systemd unit for auto-start on boot
PI_HOST=<pi-ip> make install
```

## Service management

```bash
PI_HOST=<pi-ip> make logs     # follow live journal output
PI_HOST=<pi-ip> make status   # systemctl status
PI_HOST=<pi-ip> make start    # start service
PI_HOST=<pi-ip> make stop     # stop service
PI_HOST=<pi-ip> make rollback # revert to previous binary
```

## RS485 / Modbus RTU

The controller writes FC 0x06 (Write Single Register) to slave 2, register 10,
with a value that increments by 1 every second.

**Timing at 9600 baud**

| Parameter | Value |
|---|---|
| Character time (11 bits) | 1146 µs |
| 8-byte frame TX time | 9168 µs |
| Modbus inter-frame gap (3.5 chars) | 4011 µs |

**DE/RE direction switching**

BCM4 drives DE and RE together (tied on the transceiver):

- `HIGH` → driver enabled, receiver disabled (transmit mode)
- `LOW` → driver disabled, receiver enabled (receive mode)

`tcdrain()` is used to wait for the PL011 UART TX FIFO and shift register to
empty before dropping DE.  No extra sleep is added after `tcdrain()` — on the
Pi's PL011 UART, `tcdrain()` blocks until the last stop bit has left the
shift register.  Dropping DE immediately after minimises the window during
which the Pi's driver holds the bus HIGH while a slave begins its response,
preventing bus contention.

## Project layout

```
src/
  main.cpp      Control loop, E-stop, heartbeat LED, RS485 worker thread
  rs485.cpp     Modbus RTU master transport (all standard FCs)
  gpio.cpp      /dev/gpiomem memory-mapped GPIO
  i2c.cpp       I2C via ioctl
  spi.cpp       SPI via ioctl
  uart.cpp      Raw UART
  pwm.cpp       Hardware PWM via sysfs
include/        Public headers for each driver
scripts/
  build.sh      Docker cross-compile (aarch64-linux-gnu-g++)
  deploy.sh     Build + SSH upload + atomic swap + health check
  install-service.sh  Install systemd unit
  rollback.sh   Restore previous binary
  setup-pi.sh   First-time Pi hardware config
  setup-rt-kernel.sh  PREEMPT_RT kernel install
  tune-rt.sh    OS-level RT tuning
systemd/
  cook.service  Systemd unit (CPUAffinity=3, OOMScoreAdjust=-900)
docker/
  Dockerfile.build  aarch64 cross-compiler image
```

## Real-time configuration

The control loop runs at 100 Hz on core 3:

- `SCHED_FIFO` priority 90 (enforced by both `pthread_setschedparam` and `CPUAffinity=3` in the service unit)
- `mlockall(MCL_CURRENT | MCL_FUTURE)` prevents page faults
- `isolcpus=3 nohz_full=3 rcu_nocbs=3` in kernel cmdline removes OS jitter from core 3
- RS485 I/O runs on a separate `SCHED_OTHER` worker thread so it never blocks the control loop

Typical observed jitter: **< 200 µs**.
