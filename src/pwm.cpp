#include "pwm.h"
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>

// Hardware PWM via /sys/class/pwm/pwmchip0/ (pwm_bcm2835 kernel module).
// Channel 0 → BCM18 (header pin 12), Channel 1 → BCM19 (header pin 35).
// Requires dtoverlay=pwm-2chan in /boot/firmware/config.txt.

static constexpr const char* CHIP = "/sys/class/pwm/pwmchip0";
static uint32_t s_period_ns[2] = {1000000u, 1000000u};

static bool sysfsWrite(const char* path, const char* val) {
    int fd = open(path, O_WRONLY);
    if (fd < 0) return false;
    ssize_t n = static_cast<ssize_t>(strlen(val));
    bool ok = ::write(fd, val, n) == n;
    close(fd);
    return ok;
}

static bool sysfsWriteU64(const char* path, uint64_t val) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(val));
    return sysfsWrite(path, buf);
}

namespace PWM {

bool begin(Channel ch, uint32_t range) {
    int n = static_cast<int>(ch);
    // 1 µs per tick: period_ns = range × 1000
    uint32_t period_ns = range * 1000u;
    s_period_ns[n] = period_ns;

    char path[128], num[4];
    snprintf(num, sizeof(num), "%d", n);

    snprintf(path, sizeof(path), "%s/export", CHIP);
    sysfsWrite(path, num);  // ignore error: may already be exported

    // Must zero duty_cycle before changing period to satisfy kernel invariant.
    snprintf(path, sizeof(path), "%s/pwm%d/duty_cycle", CHIP, n);
    sysfsWriteU64(path, 0);

    snprintf(path, sizeof(path), "%s/pwm%d/period", CHIP, n);
    if (!sysfsWriteU64(path, period_ns)) return false;

    snprintf(path, sizeof(path), "%s/pwm%d/enable", CHIP, n);
    return sysfsWrite(path, "1");
}

void end(Channel ch) {
    int n = static_cast<int>(ch);
    char path[128];
    snprintf(path, sizeof(path), "%s/pwm%d/enable", CHIP, n);
    sysfsWrite(path, "0");
}

void setDuty(Channel ch, uint32_t duty) {
    int n = static_cast<int>(ch);
    uint32_t maxDuty = s_period_ns[n] / 1000u;
    if (duty > maxDuty) duty = maxDuty;
    char path[128];
    snprintf(path, sizeof(path), "%s/pwm%d/duty_cycle", CHIP, n);
    sysfsWriteU64(path, static_cast<uint64_t>(duty) * 1000u);
}

void setDutyPercent(Channel ch, float pct) {
    int n = static_cast<int>(ch);
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    char path[128];
    snprintf(path, sizeof(path), "%s/pwm%d/duty_cycle", CHIP, n);
    sysfsWriteU64(path, static_cast<uint64_t>(pct / 100.0f * s_period_ns[n]));
}

} // namespace PWM
