#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

// Wraps bcm2835 I2C on /dev/i2c-1 (SDA=BCM2, SCL=BCM3).
namespace I2C {
    bool begin(uint32_t baudrate = 100000);
    void end();

    void setAddress(uint8_t addr);

    // Returns true on ACK.
    bool write(const uint8_t* buf, size_t len);
    bool write(uint8_t reg, uint8_t value);
    bool read(uint8_t* buf, size_t len);

    // Write register address then read back — the typical sensor pattern.
    bool readRegister(uint8_t reg, uint8_t* buf, size_t len);
}
