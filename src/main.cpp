#include <signal.h>
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <iostream>
#include <cerrno>
#include <cstring>

#include "gpio.h"
#include "i2c.h"
#include "spi.h"
#include "uart.h"
#include "pwm.h"
#include "rs485.h"

// ---- Pin assignments (BCM numbering) --------------------------------
static constexpr uint8_t PIN_STATUS_LED = 17;   // BCM17 = header pin 11 → LED + 330Ω to GND
static constexpr uint8_t PIN_ESTOP      = 27;   // BCM27 = header pin 13 → button to GND (active-low)

// ---- Control loop period --------------------------------------------
static constexpr auto LOOP_PERIOD = std::chrono::milliseconds(10); // 100 Hz

// ---- Signal handling ------------------------------------------------
static std::atomic<bool> g_running{true};

static void onSignal(int) {
    g_running = false;
}

// ---- Real-time scheduling -------------------------------------------
static bool applyRealtimeScheduling() {
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        std::cerr << "[cook] mlockall: " << strerror(errno) << "\n";
        return false;
    }

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(3, &cpuset);
    if (pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset) != 0) {
        std::cerr << "[cook] CPU affinity: " << strerror(errno) << "\n";
        return false;
    }

    struct sched_param sp{};
    sp.sched_priority = 90;
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) != 0) {
        std::cerr << "[cook] SCHED_FIFO: " << strerror(errno)
                  << "\n       Is the PREEMPT_RT kernel active? Run: uname -a\n";
        return false;
    }

    std::cout << "[cook] RT scheduling active (SCHED_FIFO/90, core 3)\n";
    return true;
}

// ---- Peripheral init ------------------------------------------------
static bool initPeripherals() {
    if (!GPIO::init()) {
        std::cerr << "[cook] GPIO init failed\n";
        return false;
    }

    GPIO::setMode(PIN_STATUS_LED, PinMode::Output);
    GPIO::write(PIN_STATUS_LED, PinLevel::Low);

    GPIO::setMode(PIN_ESTOP, PinMode::Input);
    GPIO::setPull(PIN_ESTOP, PullMode::Up);

    return true;
}

static void cleanupPeripherals() {
    GPIO::write(PIN_STATUS_LED, PinLevel::Low);
    GPIO::cleanup();
}

// ---- Heartbeat LED state machine ------------------------------------
// Double-blink pattern every 2 s (200 loops at 100 Hz):
//   ON 100 ms → OFF 100 ms → ON 100 ms → OFF 1700 ms
static PinLevel ledLevelForLoop(uint32_t loop) {
    uint32_t phase = loop % 200;
    if (phase < 10 || (phase >= 20 && phase < 30))
        return PinLevel::High;
    return PinLevel::Low;
}

// ---- Main control loop ----------------------------------------------
static void controlLoop() {
    using Clock  = std::chrono::steady_clock;
    using Micros = std::chrono::microseconds;

    auto next = Clock::now();

    uint32_t loop        = 0;
    int64_t  jitter_max  = 0;   // µs, worst wakeup lateness in current window
    bool     estop_prev  = false;
    PinLevel led_prev    = PinLevel::Low;

    // Status report every 5 s (500 loops)
    static constexpr uint32_t REPORT_EVERY = 500;

    while (g_running) {
        next += LOOP_PERIOD;
        std::this_thread::sleep_until(next);

        // Measure how late we actually woke up vs the scheduled instant.
        int64_t jitter = std::chrono::duration_cast<Micros>(Clock::now() - next).count();
        if (jitter > jitter_max) jitter_max = jitter;

        ++loop;

        // --- Read inputs ---
        bool estop = (GPIO::read(PIN_ESTOP) == PinLevel::Low);

        // --- LED output ---
        PinLevel led = estop ? PinLevel::High   // solid on during E-stop
                              : ledLevelForLoop(loop);

        // Only call the GPIO ioctl when state actually changes.
        if (led != led_prev) {
            GPIO::write(PIN_STATUS_LED, led);
            led_prev = led;
        }

        // Log E-stop transitions immediately.
        if (estop != estop_prev) {
            std::cout << "[cook] E-stop " << (estop ? "ACTIVE" : "cleared") << "\n";
            estop_prev = estop;
        }

        // --- Periodic status report ---
        if (loop % REPORT_EVERY == 0) {
            std::cout << "[cook] loop=" << loop
                      << "  jitter_max=" << jitter_max << "µs"
                      << "  estop=" << (estop ? "ACTIVE" : "OK") << "\n";
            jitter_max = 0;   // reset window
        }

        // TODO: add control logic here
    }
}

// ---- Entry point ----------------------------------------------------
int main() {
    // Unbuffered output so systemd journal captures messages immediately.
    std::cout.setf(std::ios::unitbuf);

    signal(SIGINT,  onSignal);
    signal(SIGTERM, onSignal);

    std::cout << "[cook] starting\n";

    if (!applyRealtimeScheduling()) return 1;
    if (!initPeripherals())         return 1;

    std::cout << "[cook] running — Ctrl-C or SIGTERM to stop\n";
    controlLoop();

    std::cout << "[cook] shutting down\n";
    cleanupPeripherals();
    return 0;
}
