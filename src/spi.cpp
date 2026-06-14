#include "spi.h"
#include <linux/spi/spidev.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <cstdint>

static int s_fd = -1;

namespace SPI {

bool begin(uint32_t clockHz, Mode mode, ChipSelect cs) {
    const char* dev = (cs == ChipSelect::CS0) ? "/dev/spidev0.0" : "/dev/spidev0.1";
    s_fd = open(dev, O_RDWR);
    if (s_fd < 0) return false;

    uint8_t  spi_mode = static_cast<uint8_t>(mode);
    uint8_t  bpw      = 8;
    uint32_t speed    = clockHz;

    ioctl(s_fd, SPI_IOC_WR_MODE,          &spi_mode);
    ioctl(s_fd, SPI_IOC_WR_BITS_PER_WORD, &bpw);
    ioctl(s_fd, SPI_IOC_WR_MAX_SPEED_HZ,  &speed);
    return true;
}

void end() {
    if (s_fd >= 0) { close(s_fd); s_fd = -1; }
}

uint8_t transfer(uint8_t byte) {
    uint8_t rx = 0;
    struct spi_ioc_transfer tr{};
    tr.tx_buf        = (unsigned long)&byte;
    tr.rx_buf        = (unsigned long)&rx;
    tr.len           = 1;
    tr.bits_per_word = 8;
    ioctl(s_fd, SPI_IOC_MESSAGE(1), &tr);
    return rx;
}

void transferBuf(uint8_t* buf, size_t len) {
    struct spi_ioc_transfer tr{};
    tr.tx_buf        = (unsigned long)buf;
    tr.rx_buf        = (unsigned long)buf;
    tr.len           = static_cast<uint32_t>(len);
    tr.bits_per_word = 8;
    ioctl(s_fd, SPI_IOC_MESSAGE(1), &tr);
}

void write(const uint8_t* buf, size_t len) {
    struct spi_ioc_transfer tr{};
    tr.tx_buf        = (unsigned long)buf;
    tr.rx_buf        = 0;
    tr.len           = static_cast<uint32_t>(len);
    tr.bits_per_word = 8;
    ioctl(s_fd, SPI_IOC_MESSAGE(1), &tr);
}

} // namespace SPI
