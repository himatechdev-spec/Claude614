#include "gpio.h"
#include <linux/gpio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <unordered_map>

static constexpr const char* CHIP = "/dev/gpiochip0";
static int s_chip = -1;

struct LineState {
    int      fd    = -1;
    uint64_t flags = GPIO_V2_LINE_FLAG_INPUT;
};
static std::unordered_map<uint8_t, LineState> s_lines;

static void closeLine(uint8_t pin) {
    auto it = s_lines.find(pin);
    if (it != s_lines.end() && it->second.fd >= 0) {
        close(it->second.fd);
        it->second.fd = -1;
    }
}

static bool openLine(uint8_t pin, uint64_t flags) {
    closeLine(pin);
    struct gpio_v2_line_request req{};
    req.offsets[0]       = pin;
    req.num_lines        = 1;
    req.config.flags     = flags;
    req.config.num_attrs = 0;
    snprintf(req.consumer, sizeof(req.consumer), "cook");
    if (ioctl(s_chip, GPIO_V2_GET_LINE_IOCTL, &req) < 0) return false;
    s_lines[pin] = {req.fd, flags};
    return true;
}

namespace GPIO {

bool init() {
    s_chip = open(CHIP, O_RDWR | O_CLOEXEC);
    return s_chip >= 0;
}

void cleanup() {
    for (auto& [pin, st] : s_lines)
        if (st.fd >= 0) close(st.fd);
    s_lines.clear();
    if (s_chip >= 0) { close(s_chip); s_chip = -1; }
}

void setMode(uint8_t pin, PinMode mode) {
    uint64_t cur = s_lines.count(pin) ? s_lines[pin].flags : 0ULL;
    cur &= ~(uint64_t)(GPIO_V2_LINE_FLAG_INPUT | GPIO_V2_LINE_FLAG_OUTPUT);
    cur |= (mode == PinMode::Output) ? GPIO_V2_LINE_FLAG_OUTPUT : GPIO_V2_LINE_FLAG_INPUT;
    openLine(pin, cur);
}

void setPull(uint8_t pin, PullMode pull) {
    uint64_t cur = s_lines.count(pin) ? s_lines[pin].flags : static_cast<uint64_t>(GPIO_V2_LINE_FLAG_INPUT);
    cur &= ~(uint64_t)(GPIO_V2_LINE_FLAG_BIAS_PULL_UP |
                       GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN |
                       GPIO_V2_LINE_FLAG_BIAS_DISABLED);
    switch (pull) {
        case PullMode::Up:   cur |= GPIO_V2_LINE_FLAG_BIAS_PULL_UP;   break;
        case PullMode::Down: cur |= GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN; break;
        case PullMode::Off:  cur |= GPIO_V2_LINE_FLAG_BIAS_DISABLED;  break;
    }
    openLine(pin, cur);
}

void write(uint8_t pin, PinLevel level) {
    auto it = s_lines.find(pin);
    if (it == s_lines.end() || it->second.fd < 0) return;
    struct gpio_v2_line_values v{};
    v.bits = (level == PinLevel::High) ? 1ULL : 0ULL;
    v.mask = 1ULL;
    ioctl(it->second.fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &v);
}

void toggle(uint8_t pin) {
    auto it = s_lines.find(pin);
    if (it == s_lines.end() || it->second.fd < 0) return;
    struct gpio_v2_line_values v{};
    v.mask = 1ULL;
    if (ioctl(it->second.fd, GPIO_V2_LINE_GET_VALUES_IOCTL, &v) < 0) return;
    v.bits ^= 1ULL;
    ioctl(it->second.fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &v);
}

PinLevel read(uint8_t pin) {
    auto it = s_lines.find(pin);
    if (it == s_lines.end() || it->second.fd < 0) return PinLevel::Low;
    struct gpio_v2_line_values v{};
    v.mask = 1ULL;
    if (ioctl(it->second.fd, GPIO_V2_LINE_GET_VALUES_IOCTL, &v) < 0) return PinLevel::Low;
    return (v.bits & 1ULL) ? PinLevel::High : PinLevel::Low;
}

} // namespace GPIO
