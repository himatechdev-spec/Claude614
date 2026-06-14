#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>

// RS485 Modbus RTU master over the Pi's hardware UART.
//
// ── Wiring ────────────────────────────────────────────────────────────────────
//  Use a half-duplex RS485 transceiver (MAX485 / SN75176 / SP3485 / MAX3485).
//
//  Pi header pin  │ BCM  │ Transceiver pin
//  ───────────────┼──────┼──────────────────────────────
//  Pin  8 (TX)    │ 14   │ DI  (Driver Input)
//  Pin 10 (RX)    │ 15   │ RO  (Receiver Output)
//  Pin  7 (GPIO)  │  4   │ DE + RE tied together
//                         │   HIGH → transmit, LOW → receive
//
//  Transceiver A (non-inverting) and B (inverting) → RS485 bus.
//  Terminate both ends of a long bus with 120 Ω between A and B.
//
// ── UART device ───────────────────────────────────────────────────────────────
//  The default device is /dev/ttyAMA0 (full PL011 UART).
//  On Pi 3B+ the PL011 is claimed by Bluetooth.  Free it by adding:
//      dtoverlay=disable-bt
//  to /boot/firmware/config.txt and rebooting once.
//  Fallback: /dev/ttyS0 (mini-UART) works but is less stable above 9600 baud.
//
// ── Prerequisites ─────────────────────────────────────────────────────────────
//  GPIO::init() must be called before RS485::begin().

namespace RS485 {

// Error codes.  Values 0x01–0x06 match Modbus exception codes from the slave.
// Values 0xE0+ are internal transport errors detected by the master.
enum class Error : uint8_t {
    None            = 0x00,
    // Modbus exception codes (returned by slave device)
    IllegalFunction = 0x01,
    IllegalAddress  = 0x02,
    IllegalValue    = 0x03,
    DeviceFailure   = 0x04,
    Acknowledge     = 0x05,
    DeviceBusy      = 0x06,
    // Internal master-side errors
    Timeout         = 0xE0,   // no response within timeoutMs
    CRCMismatch     = 0xE1,   // response CRC did not match
    FrameError      = 0xE2,   // unexpected response length or content
};

struct Response {
    bool     ok;    // true = valid response with matching CRC
    Error    err;   // reason when ok == false
    // Payload bytes stripped of address, function code, and CRC.
    // Read functions:  [byteCount, data...]  — use toRegisters() / toBit()
    // Write functions: [start_hi, start_lo, count_hi, count_lo]
    std::vector<uint8_t> data;
};

// ── Lifecycle ────────────────────────────────────────────────────────────────

// Open the UART and configure the DE/RE direction GPIO.
// dePin: BCM pin driving the transceiver DE + RE lines.
bool begin(const std::string& device = "/dev/ttyAMA0",
           uint32_t baud = 9600,
           uint8_t dePin = 4);
void end();

// ── Modbus RTU master — all standard function codes ──────────────────────────

// FC 0x01 — Read Coils (digital outputs the slave controls).
// response.data: [byteCount, packed_bits…]  — use toBit() to extract.
Response readCoils(uint8_t slave, uint16_t startCoil, uint16_t count,
                   int timeoutMs = 300);

// FC 0x02 — Read Discrete Inputs (read-only digital inputs on slave).
Response readDiscreteInputs(uint8_t slave, uint16_t startInput, uint16_t count,
                             int timeoutMs = 300);

// FC 0x03 — Read Holding Registers (read-write 16-bit registers).
// response.data: [byteCount, reg0_hi, reg0_lo, reg1_hi, reg1_lo, …]
// Use toRegisters() / toRegister() to extract values.
Response readHoldingRegisters(uint8_t slave, uint16_t startReg, uint16_t count,
                               int timeoutMs = 300);

// FC 0x04 — Read Input Registers (read-only 16-bit registers).
Response readInputRegisters(uint8_t slave, uint16_t startReg, uint16_t count,
                             int timeoutMs = 300);

// FC 0x05 — Write Single Coil (on = true → 0xFF00, false → 0x0000).
Response writeCoil(uint8_t slave, uint16_t coil, bool on,
                   int timeoutMs = 300);

// FC 0x06 — Write Single Holding Register.
Response writeRegister(uint8_t slave, uint16_t reg, uint16_t value,
                        int timeoutMs = 300);

// FC 0x0F — Write Multiple Coils.
Response writeCoils(uint8_t slave, uint16_t startCoil,
                    const std::vector<bool>& values, int timeoutMs = 300);

// FC 0x10 — Write Multiple Holding Registers.
Response writeRegisters(uint8_t slave, uint16_t startReg,
                         const std::vector<uint16_t>& values, int timeoutMs = 300);

// Broadcast (address 0x00) — sends to all slaves, no response expected.
// pdu: function code byte followed by FC-specific data bytes (no addr, no CRC).
bool broadcast(uint8_t fc, const std::vector<uint8_t>& pdu);

// ── Response helpers ─────────────────────────────────────────────────────────

// Extract uint16 values from readHoldingRegisters / readInputRegisters response.
// Returns empty vector when !ok or data is malformed.
std::vector<uint16_t> toRegisters(const Response& r);

// Get one register by index (0-based); returns 0 if !ok or out of range.
uint16_t toRegister(const Response& r, size_t index = 0);

// Get one coil/discrete-input bit (0-based); returns false if !ok or out of range.
bool toBit(const Response& r, size_t bitIndex = 0);

// ── Raw transport ────────────────────────────────────────────────────────────
// For custom frame formats on top of the RS485 physical layer.

// Send raw bytes with DE/RE direction switching and inter-frame gap enforcement.
bool sendRaw(const uint8_t* data, size_t len);

// Receive raw bytes (up to maxLen) within timeoutMs.  Returns bytes read.
// Uses an adaptive inter-character timeout after the first byte arrives.
size_t receiveRaw(uint8_t* buf, size_t maxLen, int timeoutMs);

} // namespace RS485
