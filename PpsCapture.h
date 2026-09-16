#pragma once

#include <stdint.h>

// PPS capture on the existing Teensy MicroMod digital pin 0 (GPIO_AD_B0_03).
// Owns FlexPWM1 submodule 1 while running; do not use analogWrite/FreqMeasure
// on that submodule, change CPU/bus clocks, or enter clock-stopping sleep.
namespace PpsCapture {

struct Snapshot {
  uint32_t pulseCount;
  uint32_t edgeTicks;
  uint32_t intervalTicks;
  uint32_t edgeMicros;
  uint32_t intervalMicros;
  uint32_t invalidPulseCount;
  bool hasPulse;
  bool timerHealthy;
};

// Initializes PPS capture on pin 0 using its dedicated FlexPWM submodule.
// Returns false if this is not an integral CPU/bus clock ratio or the timer
// has an active PWM output, interrupt, or DMA user. An unhealthy running
// driver must be explicitly ended and restarted after its UTC anchor is reset.
bool begin();
// Stops the capture timer and restores the pin configuration saved during initialization.
void end();
// Returns the capture timer's configured frequency in ticks per second.
uint32_t ticksPerSecond();

// Reads a validated extended timestamp from the running capture timer.
// All tick values wrap modulo 2^32 (28.6 seconds at 150 MHz). Subtract them
// unsigned and never interpolate across an interval approaching that wrap.
// No pulse is needed to read the running timer. Returns false and writes zero
// when stopped, clock configuration changes, or timer continuity is uncertain.
bool readTicks(uint32_t* ticks);

// Copies the latest pulse state and reports whether the captured edge and timer are valid.
// Always fills a non-null output. Returns true only for a healthy timer with
// at least one unambiguous captured edge; inspect intervalTicks separately.
bool snapshot(Snapshot* result);

namespace detail {
// Reconstructs elapsed timer ticks from its low 16 bits and the CPU counter.
// The CPU counter disambiguates coalesced reload interrupts; the timer itself
// supplies the returned fine timing. Exposed for host-side boundary tests.
bool elapsedTicks(uint32_t elapsedCpuCycles,
                  uint32_t cpuCyclesPerTick,
                  uint16_t previousCounter,
                  uint16_t currentCounter,
                  uint32_t* elapsed);
// Extends a captured counter value only when exactly one timer epoch fits its arrival window.
bool capturedTicks(uint32_t currentTicks,
                   uint16_t currentCounter,
                   uint16_t capturedCounter,
                   uint32_t ticksSinceService,
                   uint8_t queuedCaptures,
                   uint32_t* edge);
}

}  // namespace PpsCapture
