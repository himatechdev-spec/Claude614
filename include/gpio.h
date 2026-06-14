#pragma once
#include <cstdint>

enum class PinMode  { Input, Output };
enum class PinLevel { Low = 0, High = 1 };
enum class PullMode { Off, Down, Up };

// BCM GPIO pin numbers (not physical header pins).
// RPi 3B+ header reference:
//   BCM 2/3 = I2C1 SDA/SCL   BCM 14/15 = UART TX/RX
//   BCM 7-11 = SPI0           BCM 18/19 = PWM0/PWM1
namespace GPIO {
    bool init();
    void cleanup();

    void setMode(uint8_t bcmPin, PinMode mode);
    void setPull(uint8_t bcmPin, PullMode pull);
    void write(uint8_t bcmPin, PinLevel level);
    void toggle(uint8_t bcmPin);
    PinLevel read(uint8_t bcmPin);
}
