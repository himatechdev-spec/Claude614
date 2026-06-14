#pragma once
#include <cstdint>

// Hardware PWM via bcm2835.
// Channel 0 → BCM18 (header pin 12)
// Channel 1 → BCM19 (header pin 35)
namespace PWM {
    enum class Channel { CH0 = 0, CH1 = 1 };

    // range sets the period tick count; frequency = core_clock / clockDiv / range.
    // With clockDiv=16 and 19.2 MHz oscillator: tick = ~13.3 ns.
    // Example: range=1000 → ~75 kHz.  range=10000 → ~7.5 kHz.
    bool begin(Channel ch, uint32_t range = 1000);
    void end(Channel ch);

    // dutyCycle in [0, range].
    void setDuty(Channel ch, uint32_t dutyCycle);

    // Convenience: dutyCyclePct in [0.0, 100.0].
    void setDutyPercent(Channel ch, float pct);
}
