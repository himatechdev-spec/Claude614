#pragma once
#include <cstdint>
#include <cstddef>

// Wraps bcm2835 SPI0 (MOSI=BCM10, MISO=BCM9, SCLK=BCM11, CE0=BCM8, CE1=BCM7).
namespace SPI {
    enum class ChipSelect { CS0 = 0, CS1 = 1 };
    enum class Mode { Mode0 = 0, Mode1, Mode2, Mode3 };

    bool begin(uint32_t clockHz = 1000000, Mode mode = Mode::Mode0,
               ChipSelect cs = ChipSelect::CS0);
    void end();

    uint8_t transfer(uint8_t byte);
    void    transferBuf(uint8_t* buf, size_t len); // in-place: buf holds TX then RX
    void    write(const uint8_t* buf, size_t len);
}
