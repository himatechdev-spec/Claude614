#include "uart.h"
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <poll.h>
#include <cstring>

static speed_t baudToSpeed(uint32_t baud) {
    switch (baud) {
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        case 460800: return B460800;
        case 921600: return B921600;
        default:     return B115200;
    }
}

UART::UART(const std::string& device, uint32_t baud) {
    fd_ = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) return;

    struct termios tty{};
    tcgetattr(fd_, &tty);

    speed_t spd = baudToSpeed(baud);
    cfsetispeed(&tty, spd);
    cfsetospeed(&tty, spd);

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS);
    tty.c_iflag  = IGNPAR;
    tty.c_oflag  = 0;
    tty.c_lflag  = 0;
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;

    tcsetattr(fd_, TCSANOW, &tty);
    tcflush(fd_, TCIOFLUSH);
}

UART::~UART() {
    if (fd_ >= 0) ::close(fd_);
}

ssize_t UART::write(const uint8_t* buf, size_t len) {
    if (fd_ < 0) return -1;
    return ::write(fd_, buf, len);
}

ssize_t UART::write(const std::string& str) {
    return write(reinterpret_cast<const uint8_t*>(str.data()), str.size());
}

ssize_t UART::read(uint8_t* buf, size_t maxLen) {
    if (fd_ < 0) return -1;
    return ::read(fd_, buf, maxLen);
}

std::string UART::readLine(int timeoutMs) {
    std::string line;
    struct pollfd pfd{ fd_, POLLIN, 0 };
    char ch;
    while (true) {
        int ret = poll(&pfd, 1, timeoutMs);
        if (ret <= 0) break;
        if (::read(fd_, &ch, 1) != 1) break;
        if (ch == '\n') break;
        if (ch != '\r') line += ch;
    }
    return line;
}

void UART::flush() {
    if (fd_ >= 0) tcflush(fd_, TCIOFLUSH);
}
