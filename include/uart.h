#pragma once
#include <cstdint>
#include <cstddef>
#include <string>

// UART via Linux termios.
// Default device on RPi 3B+: /dev/ttyS0 (mini-UART, BCM14 TX / BCM15 RX).
// For full UART disable Bluetooth and use /dev/ttyAMA0.
class UART {
public:
    explicit UART(const std::string& device = "/dev/ttyS0", uint32_t baud = 115200);
    ~UART();

    bool isOpen() const { return fd_ >= 0; }

    // Returns bytes written, or -1 on error.
    ssize_t write(const uint8_t* buf, size_t len);
    ssize_t write(const std::string& str);

    // Returns bytes read, or -1 on error.  Non-blocking.
    ssize_t read(uint8_t* buf, size_t maxLen);

    // Read one complete line (terminated by '\n'), blocking up to timeoutMs.
    std::string readLine(int timeoutMs = 1000);

    void flush();

private:
    int fd_ = -1;
};
