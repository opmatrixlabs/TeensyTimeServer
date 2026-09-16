/*
 * Copyright (c) 2026. Andrew Kevin Bailey
 * This code, firmware, and software is released under the MIT License (http://opensource.org/licenses/MIT).
 */

// Run through RunPpsTimestampTests.ps1 to exercise the actual sketch functions.
#include "PpsClock.h"
#include "PpsCapture.h"

#include <cassert>
#include <cstdint>
#include <cstdio>

// @PPS_TIMESTAMP_DECLARATIONS@

uint64_t fakeMicroseconds = 0;
// Returns the simulated millisecond counter with normal 32-bit wraparound.
uint32_t millis() { return static_cast<uint32_t>(fakeMicroseconds / 1000ULL); }
// Returns the simulated microsecond counter with normal 32-bit wraparound.
uint32_t micros() { return static_cast<uint32_t>(fakeMicroseconds); }

PpsClock ppsClock(150000000);
bool ppsClockConfirmed = true;
bool validTimTpSeen = true;
uint32_t lastValidTimTpMillis = 0;
uint32_t observedInvalidIntervalCount = 0;

PpsCapture::Snapshot fakeCapture = {};
PpsCapture::Snapshot nextCapture = {};
uint32_t fakeReadTicks = 0;
uint32_t snapshotCalls = 0;
bool snapshotAvailable = true;
bool readAvailable = true;
bool pulseDuringRead = false;

namespace PpsCapture {
// Supplies simulated timer ticks and optionally captures a pulse during the read.
bool readTicks(uint32_t* ticks) {
  assert(ticks != nullptr);
  *ticks = readAvailable ? fakeReadTicks : 0;
  if (pulseDuringRead) {
    fakeCapture = nextCapture;
    pulseDuringRead = false;
  }
  return readAvailable;
}

// Copies the simulated capture state and records each snapshot request.
bool snapshot(Snapshot* result) {
  assert(result != nullptr);
  ++snapshotCalls;
  *result = fakeCapture;
  return snapshotAvailable;
}
} // namespace PpsCapture

// @PPS_TIMESTAMP_SOURCE@

namespace {
constexpr uint32_t TIMER_HZ = 150000000;
constexpr uint32_t ORIGINAL_EDGE_TICKS = 0xFFF00000U;
constexpr int64_t ORIGINAL_UTC_SECONDS = 4000000000LL;

// Restores a healthy captured pulse and its UTC anchor before each test.
void resetFixture() {
  fakeMicroseconds = 10250000;
  ppsClock = PpsClock(TIMER_HZ);
  ppsClockConfirmed = validTimTpSeen = true;
  lastValidTimTpMillis = millis();
  observedInvalidIntervalCount = 0;
  fakeCapture = {10, ORIGINAL_EDGE_TICKS, TIMER_HZ, 10000000, 1000000, 0, true, true};
  nextCapture = {};
  fakeReadTicks = ORIGINAL_EDGE_TICKS + TIMER_HZ / 4;
  snapshotCalls = 0;
  snapshotAvailable = readAvailable = true;
  pulseDuringRead = false;
  assert(ppsClock.setLabelledPulse(fakeCapture.pulseCount, fakeCapture.edgeTicks,
                                  fakeCapture.intervalTicks,
                                  normalizeTimestamp(ORIGINAL_UTC_SECONDS, 0)));
}

// Fine ticks cross their wrap while coarse freshness clocks remain far from wrap.
void testFineTimerWrapUsesTicksForTimestamp() {
  resetFixture();
  assert(fakeReadTicks < fakeCapture.edgeTicks);
  NormalizedTimestamp timestamp = {};
  assert(readPpsTimestamp(&timestamp));
  assert(timestamp.secondsSince1900 == ORIGINAL_UTC_SECONDS);
  assert(timestamp.nanoseconds == 250000000);
}

// A new captured PPS must not reinterpret a previously sampled T2 as a future tick.
void testPpsArrivesBetweenTickReadAndValidation() {
  resetFixture();
  fakeMicroseconds = 11000001;
  fakeReadTicks = ORIGINAL_EDGE_TICKS + TIMER_HZ - 1;
  nextCapture = fakeCapture;
  ++nextCapture.pulseCount;
  nextCapture.edgeTicks += TIMER_HZ;
  nextCapture.edgeMicros += 1000000;
  pulseDuringRead = true;
  NormalizedTimestamp before = {};
  assert(readPpsTimestamp(&before));
  assert(before.secondsSince1900 == ORIGINAL_UTC_SECONDS);
  assert(before.nanoseconds == 999999993);
  fakeReadTicks = nextCapture.edgeTicks + 1;
  NormalizedTimestamp after = {};
  assert(readPpsTimestamp(&after));
  assert(after.secondsSince1900 == ORIGINAL_UTC_SECONDS + 1);
  assert(after.nanoseconds == 7);
}

// Verifies freshness checks across microsecond and millisecond counter wraps.
void testFreshnessAcrossCoarseCounterWraps() {
  resetFixture();
  fakeMicroseconds = static_cast<uint64_t>(UINT32_MAX) + 250001ULL;
  fakeCapture.edgeMicros = static_cast<uint32_t>(fakeMicroseconds - 250000ULL);
  lastValidTimTpMillis = millis();
  NormalizedTimestamp timestamp = {};
  assert(readPpsTimestamp(&timestamp));
  assert(timestamp.nanoseconds == 250000000);

  fakeMicroseconds = (static_cast<uint64_t>(UINT32_MAX) + 1ULL) * 1000ULL + 250000ULL;
  fakeCapture.edgeMicros = static_cast<uint32_t>(fakeMicroseconds - 250000ULL);
  lastValidTimTpMillis = UINT32_MAX - 100U;
  assert(readPpsTimestamp(&timestamp));
  lastValidTimTpMillis = millis() - TIMTP_STALE_MILLIS - 1;
  assert(!readPpsTimestamp(&timestamp));
}

// Verifies that missing UTC labels and invalid capture states prevent timestamp generation.
void testUnavailableUtcAndCaptureAreRejected() {
  NormalizedTimestamp timestamp = {};
  resetFixture();
  assert(!getPpsTimestamp(fakeReadTicks, nullptr));
  assert(snapshotCalls == 0);
  ppsClockConfirmed = false;
  assert(!readPpsTimestamp(&timestamp));
  ppsClockConfirmed = true;
  validTimTpSeen = false;
  assert(!readPpsTimestamp(&timestamp));
  validTimTpSeen = true;
  readAvailable = false;
  assert(!readPpsTimestamp(&timestamp));
  assert(snapshotCalls == 0);
  readAvailable = true;
  snapshotAvailable = false;
  assert(!readPpsTimestamp(&timestamp));
  snapshotAvailable = true;
  fakeCapture.timerHealthy = false;
  assert(!readPpsTimestamp(&timestamp));
  fakeCapture.timerHealthy = true;
  fakeCapture.pulseCount = 1;
  assert(!readPpsTimestamp(&timestamp));
  fakeCapture.pulseCount = 10;
  fakeCapture.intervalMicros = PpsClock::MIN_PULSE_INTERVAL_MICROS - 1;
  assert(!readPpsTimestamp(&timestamp));
  fakeCapture.intervalMicros = 1000000;
  ++fakeCapture.invalidPulseCount;
  assert(!readPpsTimestamp(&timestamp));
  --fakeCapture.invalidPulseCount;
  ppsClock.reset();
  assert(!readPpsTimestamp(&timestamp));
}

// Verifies that freshness limits reject stale pulses even when fine timer values repeat.
void testFreshnessLimitsRejectCounterAliases() {
  NormalizedTimestamp timestamp = {};
  resetFixture();
  lastValidTimTpMillis = millis() - TIMTP_STALE_MILLIS;
  assert(readPpsTimestamp(&timestamp));
  --lastValidTimTpMillis;
  assert(!readPpsTimestamp(&timestamp));
  lastValidTimTpMillis = millis();
  fakeCapture.edgeMicros = micros() - TIME_PULSE_STALE_MICROS;
  assert(readPpsTimestamp(&timestamp));
  --fakeCapture.edgeMicros;
  assert(!readPpsTimestamp(&timestamp));

  resetFixture();
  // After a full fine-counter wrap an isolated tick subtraction looks fresh.
  fakeMicroseconds += ((1ULL << 32) * 1000000ULL) / TIMER_HZ;
  assert(ppsClock.timestampAt(fakeReadTicks, &timestamp));
  assert(!readPpsTimestamp(&timestamp));
  // Even an independently refreshed label cannot make the stale PPS acceptable.
  lastValidTimTpMillis = millis();
  assert(!readPpsTimestamp(&timestamp));
}

// Verifies that pulse status preserves microsecond and timer-tick units while exposing capture failures.
void testStatusPreservesCoarseAndFineUnits() {
  resetFixture();
  uint32_t pulse = 0, intervalMicros = 0, edgeMicros = 0;
  uint32_t invalid = 0, edgeTicks = 0, intervalTicks = 0;
  getTimePulseStatus(&pulse, &intervalMicros, &edgeMicros, &invalid,
                     &edgeTicks, &intervalTicks);
  assert(pulse == fakeCapture.pulseCount);
  assert(intervalMicros == 1000000 && intervalTicks == TIMER_HZ);
  assert(edgeMicros == 10000000 && edgeTicks == ORIGINAL_EDGE_TICKS);
  assert(invalid == 0);
  fakeCapture.timerHealthy = false;
  getTimePulseStatus(&pulse, &intervalMicros, &edgeMicros, &invalid,
                     &edgeTicks, &intervalTicks);
  assert(intervalMicros == 0 && intervalTicks == 0 && invalid == 1);
  assert(edgeMicros == 10000000 && edgeTicks == ORIGINAL_EDGE_TICKS);
  getTimePulseStatus(&pulse, &intervalMicros, nullptr, nullptr, nullptr, nullptr);
}
} // namespace

// Runs the production PPS timestamp integration scenarios and reports success.
int main() {
  testFineTimerWrapUsesTicksForTimestamp();
  testPpsArrivesBetweenTickReadAndValidation();
  testFreshnessAcrossCoarseCounterWraps();
  testUnavailableUtcAndCaptureAreRejected();
  testFreshnessLimitsRejectCounterAliases();
  testStatusPreservesCoarseAndFineUnits();
  std::puts("PPS timestamp integration tests passed (6 scenarios).");
  return 0;
}
