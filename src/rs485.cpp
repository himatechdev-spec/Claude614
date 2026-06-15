#include "rs485.h"
#include "gpio.h"
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <poll.h>
#include <cstring>
#include <algorithm>
#include <chrono>

// ── Internal state ────────────────────────────────────────────────────────────

static int      s_fd            = -1;
static uint8_t  s_dePin         = 0xFF;
static uint32_t s_charTime_us   = 0;    // µs per character (11 bits)
static uint32_t s_interframe_us = 0;    // inter-frame gap (3.5 char times, ≥1750 µs)

static std::chrono::steady_clock::time_point s_lastTx;  // end of last transmission

static constexpr size_t MAX_FRAME       = 260;  // max Modbus RTU frame incl. CRC
static constexpr int    INTERCHAR_MS    = 5;    // min inter-character gap for receive

// ── Utilities ─────────────────────────────────────────────────────────────────

static speed_t baudToSpeed(uint32_t baud) {
    switch (baud) {
        case 1200:   return B1200;
        case 2400:   return B2400;
        case 4800:   return B4800;
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        default:     return B9600;
    }
}

// CRC-16/Modbus (poly 0xA001, init 0xFFFF, LSB first)
static uint16_t crc16(const uint8_t* d, size_t n) {
    uint16_t crc = 0xFFFF;
    while (n--) {
        crc ^= *d++;
        for (int i = 0; i < 8; ++i)
            crc = (crc & 1u) ? ((crc >> 1) ^ 0xA001u) : (crc >> 1);
    }
    return crc;
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

bool RS485::begin(const std::string& device, uint32_t baud, uint8_t dePin) {
    s_fd = open(device.c_str(), O_RDWR | O_NOCTTY);
    if (s_fd < 0) return false;

    struct termios tty{};
    tcgetattr(s_fd, &tty);
    speed_t spd = baudToSpeed(baud);
    cfsetispeed(&tty, spd);
    cfsetospeed(&tty, spd);
    // 8N1, no flow control, raw
    tty.c_cflag  = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS);
    tty.c_iflag  = 0;
    tty.c_oflag  = 0;
    tty.c_lflag  = 0;
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;
    tcsetattr(s_fd, TCSANOW, &tty);
    tcflush(s_fd, TCIOFLUSH);

    // Timing constants (11 bits/char: 1 start + 8 data + 1 parity + 1 stop)
    s_charTime_us   = (11u * 1000000u + baud - 1u) / baud;   // round up
    s_interframe_us = std::max(1750u, (s_charTime_us * 35u) / 10u);  // 3.5 chars, ≥1750 µs

    s_dePin  = dePin;
    s_lastTx = std::chrono::steady_clock::now() - std::chrono::seconds(1);

    if (dePin != 0xFF) {
        GPIO::setMode(dePin, PinMode::Output);
        GPIO::write(dePin, PinLevel::Low);
    }
    return true;
}

void RS485::end() {
    if (s_dePin != 0xFF) GPIO::write(s_dePin, PinLevel::Low);
    if (s_fd >= 0) { close(s_fd); s_fd = -1; }
}

// ── Raw transport ─────────────────────────────────────────────────────────────

bool RS485::sendRaw(const uint8_t* data, size_t len) {
    if (s_fd < 0 || len == 0) return false;

    // Enforce inter-frame gap between consecutive transmissions.
    auto now = std::chrono::steady_clock::now();
    auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                          now - s_lastTx).count();
    if (elapsed_us < (int64_t)s_interframe_us)
        usleep(s_interframe_us - (uint32_t)elapsed_us);

    tcflush(s_fd, TCIFLUSH);               // discard stale RX bytes

    if (s_dePin != 0xFF) {
        GPIO::write(s_dePin, PinLevel::High);  // enable driver (TX mode)
        usleep(50);                             // transceiver enable settling
    }

    ssize_t written = write(s_fd, data, len);
    tcdrain(s_fd);                          // wait for TX FIFO + shift register to drain

    if (s_dePin != 0xFF) {
        GPIO::write(s_dePin, PinLevel::Low);   // enable receiver (RX mode) immediately after last bit
    }
    s_lastTx = std::chrono::steady_clock::now();

    return written == (ssize_t)len;
}

size_t RS485::receiveRaw(uint8_t* buf, size_t maxLen, int timeoutMs) {
    if (s_fd < 0 || maxLen == 0) return 0;

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeoutMs);
    size_t total  = 0;

    while (total < maxLen) {
        int left_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(
                          deadline - std::chrono::steady_clock::now()).count();
        if (left_ms <= 0) break;

        // After the first byte arrives, switch to a short inter-character timeout
        // so we detect frame end when the bus goes silent (Modbus: 3.5 char times).
        int wait_ms = (total == 0) ? left_ms : std::min(left_ms, INTERCHAR_MS);

        struct pollfd pfd{ s_fd, POLLIN, 0 };
        if (poll(&pfd, 1, wait_ms) <= 0) break;  // timeout → frame ended

        ssize_t n = read(s_fd, buf + total, maxLen - total);
        if (n <= 0) break;
        total += (size_t)n;
    }
    return total;
}

// ── Internal Modbus helpers ───────────────────────────────────────────────────

// Build a Modbus RTU frame (PDU + CRC) and send it.
static bool modbusRequest(const uint8_t* pdu, size_t pduLen) {
    if (pduLen + 2 > MAX_FRAME) return false;
    uint8_t frame[MAX_FRAME];
    memcpy(frame, pdu, pduLen);
    uint16_t crc       = crc16(pdu, pduLen);
    frame[pduLen]      = crc & 0xFF;
    frame[pduLen + 1]  = (uint8_t)(crc >> 8);
    return RS485::sendRaw(frame, pduLen + 2);
}

// Receive a Modbus RTU response of known length (excluding CRC), validate CRC,
// detect exception responses, and return a populated Response.
// expectedLen = byte count of the normal response excluding CRC
//               (i.e. addr + fc + payload bytes).
static RS485::Response modbusResponse(size_t expectedLen, int timeoutMs) {
    RS485::Response r{false, RS485::Error::Timeout, {}};

    uint8_t buf[MAX_FRAME];
    // Try to read the expected frame.  Exception responses are shorter (5 bytes total)
    // but receiveRaw will detect bus silence and return early.
    size_t n = RS485::receiveRaw(buf, expectedLen + 2, timeoutMs);

    if (n < 4) return r;   // too short to be any valid Modbus frame

    // Exception response: FC has bit 7 set (FC | 0x80)
    if (n >= 5 && (buf[1] & 0x80u)) {
        uint16_t crc = crc16(buf, 3);
        if ((crc & 0xFF) == buf[3] && (crc >> 8) == buf[4])
            r.err = static_cast<RS485::Error>(buf[2]);
        else
            r.err = RS485::Error::CRCMismatch;
        return r;
    }

    if (n != expectedLen + 2) {
        r.err = RS485::Error::FrameError;
        return r;
    }

    // Verify CRC
    uint16_t crc = crc16(buf, expectedLen);
    if ((crc & 0xFF) != buf[expectedLen] || (crc >> 8) != buf[expectedLen + 1]) {
        r.err = RS485::Error::CRCMismatch;
        return r;
    }

    r.ok   = true;
    r.err  = RS485::Error::None;
    r.data = std::vector<uint8_t>(buf + 2, buf + expectedLen);  // strip addr + FC
    return r;
}

// ── FC 0x01 — Read Coils ─────────────────────────────────────────────────────

RS485::Response RS485::readCoils(uint8_t slave, uint16_t start, uint16_t count, int tms) {
    uint8_t pdu[6] = { slave, 0x01,
                        (uint8_t)(start >> 8), (uint8_t)start,
                        (uint8_t)(count >> 8), (uint8_t)count };
    if (!modbusRequest(pdu, 6)) return {false, Error::FrameError, {}};
    return modbusResponse(3 + (count + 7u) / 8u, tms);
}

// ── FC 0x02 — Read Discrete Inputs ───────────────────────────────────────────

RS485::Response RS485::readDiscreteInputs(uint8_t slave, uint16_t start, uint16_t count, int tms) {
    uint8_t pdu[6] = { slave, 0x02,
                        (uint8_t)(start >> 8), (uint8_t)start,
                        (uint8_t)(count >> 8), (uint8_t)count };
    if (!modbusRequest(pdu, 6)) return {false, Error::FrameError, {}};
    return modbusResponse(3 + (count + 7u) / 8u, tms);
}

// ── FC 0x03 — Read Holding Registers ─────────────────────────────────────────

RS485::Response RS485::readHoldingRegisters(uint8_t slave, uint16_t start, uint16_t count, int tms) {
    uint8_t pdu[6] = { slave, 0x03,
                        (uint8_t)(start >> 8), (uint8_t)start,
                        (uint8_t)(count >> 8), (uint8_t)count };
    if (!modbusRequest(pdu, 6)) return {false, Error::FrameError, {}};
    return modbusResponse(3u + count * 2u, tms);
}

// ── FC 0x04 — Read Input Registers ───────────────────────────────────────────

RS485::Response RS485::readInputRegisters(uint8_t slave, uint16_t start, uint16_t count, int tms) {
    uint8_t pdu[6] = { slave, 0x04,
                        (uint8_t)(start >> 8), (uint8_t)start,
                        (uint8_t)(count >> 8), (uint8_t)count };
    if (!modbusRequest(pdu, 6)) return {false, Error::FrameError, {}};
    return modbusResponse(3u + count * 2u, tms);
}

// ── FC 0x05 — Write Single Coil ──────────────────────────────────────────────

RS485::Response RS485::writeCoil(uint8_t slave, uint16_t coil, bool on, int tms) {
    uint8_t pdu[6] = { slave, 0x05,
                        (uint8_t)(coil >> 8), (uint8_t)coil,
                        on ? (uint8_t)0xFF : (uint8_t)0x00, 0x00 };
    if (!modbusRequest(pdu, 6)) return {false, Error::FrameError, {}};
    return modbusResponse(6, tms);  // slave echoes the request
}

// ── FC 0x06 — Write Single Register ──────────────────────────────────────────

RS485::Response RS485::writeRegister(uint8_t slave, uint16_t reg, uint16_t value, int tms) {
    uint8_t pdu[6] = { slave, 0x06,
                        (uint8_t)(reg >> 8), (uint8_t)reg,
                        (uint8_t)(value >> 8), (uint8_t)value };
    if (!modbusRequest(pdu, 6)) return {false, Error::FrameError, {}};
    return modbusResponse(6, tms);
}

// ── FC 0x0F — Write Multiple Coils ───────────────────────────────────────────

RS485::Response RS485::writeCoils(uint8_t slave, uint16_t start,
                                   const std::vector<bool>& values, int tms) {
    uint16_t count     = (uint16_t)values.size();
    uint8_t  byteCount = (uint8_t)((count + 7u) / 8u);
    size_t   pduLen    = 7u + byteCount;
    if (pduLen > MAX_FRAME - 2) return {false, Error::FrameError, {}};

    uint8_t pdu[MAX_FRAME];
    pdu[0] = slave;  pdu[1] = 0x0F;
    pdu[2] = (uint8_t)(start >> 8);   pdu[3] = (uint8_t)start;
    pdu[4] = (uint8_t)(count >> 8);   pdu[5] = (uint8_t)count;
    pdu[6] = byteCount;
    memset(pdu + 7, 0, byteCount);
    for (size_t i = 0; i < values.size(); ++i)
        if (values[i]) pdu[7 + i / 8] |= (uint8_t)(1u << (i % 8));

    if (!modbusRequest(pdu, pduLen)) return {false, Error::FrameError, {}};
    return modbusResponse(6, tms);  // slave echoes addr, FC, start, count
}

// ── FC 0x10 — Write Multiple Registers ───────────────────────────────────────

RS485::Response RS485::writeRegisters(uint8_t slave, uint16_t start,
                                       const std::vector<uint16_t>& values, int tms) {
    uint16_t count     = (uint16_t)values.size();
    uint8_t  byteCount = (uint8_t)(count * 2u);
    size_t   pduLen    = 7u + byteCount;
    if (pduLen > MAX_FRAME - 2) return {false, Error::FrameError, {}};

    uint8_t pdu[MAX_FRAME];
    pdu[0] = slave;  pdu[1] = 0x10;
    pdu[2] = (uint8_t)(start >> 8);   pdu[3] = (uint8_t)start;
    pdu[4] = (uint8_t)(count >> 8);   pdu[5] = (uint8_t)count;
    pdu[6] = byteCount;
    for (size_t i = 0; i < values.size(); ++i) {
        pdu[7 + i * 2]     = (uint8_t)(values[i] >> 8);
        pdu[7 + i * 2 + 1] = (uint8_t) values[i];
    }

    if (!modbusRequest(pdu, pduLen)) return {false, Error::FrameError, {}};
    return modbusResponse(6, tms);
}

// ── Broadcast ─────────────────────────────────────────────────────────────────

bool RS485::broadcast(uint8_t fc, const std::vector<uint8_t>& pdu_data) {
    size_t pduLen = 2u + pdu_data.size();
    if (pduLen > MAX_FRAME - 2) return false;

    uint8_t pdu[MAX_FRAME];
    pdu[0] = 0x00;   // broadcast address — all slaves receive, none reply
    pdu[1] = fc;
    memcpy(pdu + 2, pdu_data.data(), pdu_data.size());
    return modbusRequest(pdu, pduLen);
    // No response wait — Modbus slaves never reply to address 0x00.
}

// ── Response helpers ──────────────────────────────────────────────────────────

std::vector<uint16_t> RS485::toRegisters(const Response& r) {
    if (!r.ok || r.data.empty()) return {};
    uint8_t byteCount = r.data[0];
    if (r.data.size() < 1u + byteCount || byteCount % 2 != 0) return {};
    std::vector<uint16_t> regs(byteCount / 2);
    for (size_t i = 0; i < regs.size(); ++i)
        regs[i] = ((uint16_t)r.data[1 + i * 2] << 8) | r.data[2 + i * 2];
    return regs;
}

uint16_t RS485::toRegister(const Response& r, size_t index) {
    auto regs = toRegisters(r);
    return index < regs.size() ? regs[index] : 0;
}

bool RS485::toBit(const Response& r, size_t bitIndex) {
    if (!r.ok || r.data.empty()) return false;
    uint8_t byteCount = r.data[0];
    size_t  byteIdx   = bitIndex / 8;
    if (byteIdx >= byteCount || 1 + byteIdx >= r.data.size()) return false;
    return (r.data[1 + byteIdx] >> (bitIndex % 8)) & 1u;
}
