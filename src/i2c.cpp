#include "i2c.h"
#include <linux/i2c-dev.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

// Baudrate is configured by the device tree overlay in setup-pi.sh — ignored here.
static int s_fd = -1;

namespace I2C {

bool begin(uint32_t /*baudrate*/) {
    s_fd = open("/dev/i2c-1", O_RDWR);
    return s_fd >= 0;
}

void end() {
    if (s_fd >= 0) { close(s_fd); s_fd = -1; }
}

void setAddress(uint8_t addr) {
    if (s_fd >= 0) ioctl(s_fd, I2C_SLAVE, addr);
}

bool write(const uint8_t* buf, size_t len) {
    return s_fd >= 0 && ::write(s_fd, buf, len) == static_cast<ssize_t>(len);
}

bool write(uint8_t reg, uint8_t value) {
    uint8_t buf[2] = {reg, value};
    return write(buf, 2);
}

bool read(uint8_t* buf, size_t len) {
    return s_fd >= 0 && ::read(s_fd, buf, len) == static_cast<ssize_t>(len);
}

bool readRegister(uint8_t reg, uint8_t* buf, size_t len) {
    if (!write(&reg, 1)) return false;
    return read(buf, len);
}

} // namespace I2C
