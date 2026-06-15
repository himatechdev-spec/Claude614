#include <signal.h>
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
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

// ---- RS485 worker thread --------------------------------------------
// The RT control loop enqueues write requests here; a separate low-priority
// thread drains the queue so RS485 I/O never blocks the control loop.

struct RS485Request {
    uint8_t  slave;
    uint16_t reg;
    uint16_t value;
};

static constexpr size_t RS485_QUEUE_MAX = 16;  // drop oldest if overrun

static std::queue<RS485Request> s_rs485_queue;
static std::mutex               s_rs485_mutex;
static std::condition_variable  s_rs485_cv;
static std::thread              s_rs485_thread;
static std::atomic<bool>        s_rs485_stop{false};

static bool     s_rs485_ok = false;
static uint16_t s_rs485_i  = 1;   // incrementing value sent to slave

// Called from the worker thread — never from the RT loop.
static void rs485Worker() {
    // Low priority: SCHED_OTHER with nice +10, not real-time.
    struct sched_param sp{};
    pthread_setschedparam(pthread_self(), SCHED_OTHER, &sp);

    while (true) {
        RS485Request req{};
        {
            std::unique_lock<std::mutex> lk(s_rs485_mutex);
            s_rs485_cv.wait(lk, [] {
                return s_rs485_stop.load() || !s_rs485_queue.empty();
            });
            if (s_rs485_stop && s_rs485_queue.empty()) break;
            req = s_rs485_queue.front();
            s_rs485_queue.pop();
        }

        auto r = RS485::writeRegister(req.slave, req.reg, req.value);
        if (r.ok) {
            std::cout << "[rs485] writeRegister(slave=" << (int)req.slave
                      << ", reg=" << req.reg << ", val=" << req.value << ")  →  OK\n";
        } else {
            std::cout << "[rs485] writeRegister(slave=" << (int)req.slave
                      << ", reg=" << req.reg << ", val=" << req.value
                      << ")  →  FAIL err=0x" << std::hex << (int)r.err << std::dec << "\n";
        }
    }
}

// Called from the RT control loop — non-blocking.
// Returns false and drops the request if the queue is at capacity.
static bool enqueueRS485(uint8_t slave, uint16_t reg, uint16_t value) {
    {
        std::lock_guard<std::mutex> lk(s_rs485_mutex);
        if (s_rs485_queue.size() >= RS485_QUEUE_MAX) return false;
        s_rs485_queue.push({slave, reg, value});
    }
    s_rs485_cv.notify_one();
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

    // RS485 — /dev/ttyAMA0 requires 'dtoverlay=disable-bt' in config.txt.
    // Non-fatal: heartbeat and E-stop still work if RS485 is unavailable.
    s_rs485_ok = RS485::begin("/dev/ttyAMA0", 9600, /*dePin=*/4);
    if (s_rs485_ok) {
        s_rs485_stop = false;
        s_rs485_thread = std::thread(rs485Worker);
        std::cout << "[cook] RS485 ready  (/dev/ttyAMA0, 9600 baud, DE=BCM4)\n";
    } else {
        std::cerr << "[cook] RS485 unavailable — check dtoverlay=disable-bt and BCM4 wiring\n";
    }

    return true;
}

static void cleanupPeripherals() {
    GPIO::write(PIN_STATUS_LED, PinLevel::Low);

    if (s_rs485_ok) {
        // Signal worker to drain remaining queue then exit.
        {
            std::lock_guard<std::mutex> lk(s_rs485_mutex);
            s_rs485_stop = true;
        }
        s_rs485_cv.notify_one();
        if (s_rs485_thread.joinable()) s_rs485_thread.join();
        RS485::end();
    }

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

    // Status report every 5 s (500 loops); RS485 write every 1 s (100 loops)
    static constexpr uint32_t REPORT_EVERY  = 500;
    static constexpr uint32_t RS485_EVERY   = 100;

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

        // --- Periodic status report (every 5 s) ---
        if (loop % REPORT_EVERY == 0) {
            std::cout << "[cook] loop=" << loop
                      << "  jitter_max=" << jitter_max << "µs"
                      << "  estop=" << (estop ? "ACTIVE" : "OK") << "\n";
            jitter_max = 0;   // reset window
        }

        // --- RS485 write every 1 s: slave=2, reg=10, value increments each second ---
        if (s_rs485_ok && loop % RS485_EVERY == 0) {
            if (!enqueueRS485(2, 10, s_rs485_i))
                std::cout << "[cook] RS485 queue full — request dropped\n";
            ++s_rs485_i;
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
