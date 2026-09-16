/*
 * Copyright (c) 2026. Andrew Kevin Bailey
 * This code, firmware, and software is released under the MIT License (http://opensource.org/licenses/MIT).
 */

#include "../PpsClock.h"

#include <assert.h>

namespace {
// Verifies that an anchored PPS clock interpolates time from elapsed microseconds.
void testAnchorAndInterpolation() {
  PpsClock clock;
  NormalizedTimestamp timestamp = {};

  assert(!clock.timestampAt(1000, &timestamp));
  assert(clock.setAnchor(10, 1000000, normalizeTimestamp(4000000000LL, 250)));
  assert(clock.timestampAt(1123456, &timestamp));
  assert(timestamp.secondsSince1900 == 4000000000LL);
  assert(timestamp.nanoseconds == 123456250UL);
}

// Verifies that advancing across pulses updates the stored anchor consistently.
void testPulseAdvancement() {
  PpsClock clock;
  assert(clock.setAnchor(10, 1000000, normalizeTimestamp(100, 500)));
  assert(clock.advanceToPulse(13, 4000000, 1000000));

  uint32_t pulseCount = 0;
  uint32_t edgeMicros = 0;
  NormalizedTimestamp anchor = {};
  assert(clock.getAnchor(&pulseCount, &edgeMicros, &anchor));
  assert(pulseCount == 13);
  assert(edgeMicros == 4000000);
  assert(anchor.secondsSince1900 == 103);
  assert(anchor.nanoseconds == 500);
}

// Verifies that interpolation remains correct when the microsecond counter wraps.
void testMicrosWrap() {
  PpsClock clock;
  NormalizedTimestamp timestamp = {};
  assert(clock.setAnchor(1, 0xFFFFFF00UL, normalizeTimestamp(200, 0)));
  assert(clock.timestampAt(0x00000100UL, &timestamp));
  assert(timestamp.secondsSince1900 == 200);
  assert(timestamp.nanoseconds == 512000);
}

// Verifies that invalid pulse timing clears the PPS clock anchor.
void testInvalidTimingResetsClock() {
  PpsClock clock;
  NormalizedTimestamp timestamp = {};
  assert(clock.setAnchor(1, 100, normalizeTimestamp(200, 0)));
  assert(!clock.advanceToPulse(2, 200, PpsClock::MIN_PULSE_INTERVAL_MICROS - 1));
  assert(!clock.isAnchored());
  assert(!clock.timestampAt(200, &timestamp));
}

// Verifies that elapsed pulse timing agrees with the reported pulse count.
void testMissedPulseTimingMustMatchPulseCount() {
  PpsClock clock;
  assert(clock.setAnchor(10, 1000000, normalizeTimestamp(200, 0)));
  assert(!clock.advanceToPulse(12, 2000000, 1000000));
  assert(!clock.isAnchored());
}

// Verifies that measured PPS intervals discipline the interpolation scale.
void testPpsDisciplinesInterpolationScale() {
  PpsClock clock;
  NormalizedTimestamp timestamp = {};
  assert(clock.setAnchor(1, 1000000, normalizeTimestamp(200, 0)));
  assert(clock.setLabelledPulse(2, 1999000, 999000, normalizeTimestamp(201, 0)));
  assert(clock.timestampAt(2498500, &timestamp));
  assert(timestamp.secondsSince1900 == 201);
  assert(timestamp.nanoseconds == 500000000UL);
}

// Verifies that the first labelled pulse establishes a disciplined interpolation scale.
void testInitialLabelledPulseDisciplinesInterpolationScale() {
  PpsClock clock;
  NormalizedTimestamp timestamp = {};
  assert(clock.setLabelledPulse(10, 1000000, 999000, normalizeTimestamp(200, 0)));
  assert(clock.timestampAt(1499500, &timestamp));
  assert(timestamp.secondsSince1900 == 200);
  assert(timestamp.nanoseconds == 500000000UL);
  assert(!clock.setLabelledPulse(11,
                                2000000,
                                PpsClock::MIN_PULSE_INTERVAL_MICROS - 1,
                                normalizeTimestamp(201, 0)));
  assert(!clock.isAnchored());
}

// Returns the signed nanosecond difference between two normalized timestamps.
int64_t nanosecondsSince(const NormalizedTimestamp& timestamp,
                        const NormalizedTimestamp& origin) {
  return (timestamp.secondsSince1900 - origin.secondsSince1900) * 1000000000LL +
         static_cast<int64_t>(timestamp.nanoseconds) - origin.nanoseconds;
}

// Verifies that a nanosecond measurement falls within the specified tolerance.
void assertWithinNanoseconds(const int64_t actual, const int64_t expected,
                             const int64_t tolerance) {
  assert(actual >= expected - tolerance);
  assert(actual <= expected + tolerance);
}

// Exercises reciprocal conversion across frequency tolerance, counter wrap, and UTC rollover.
void testHighResolutionInterpolationAccuracy() {
  const uint32_t rates[] = {1000000, 24000000, 150000000, 600000000};
  const int32_t offsetsPpm[] = {-1000, -317, -3, 0, 3, 731, 1000};
  for (const uint32_t rate : rates) {
    for (const int32_t offset : offsetsPpm) {
      const uint32_t interval = static_cast<uint32_t>(
          static_cast<int64_t>(rate) + static_cast<int64_t>(rate) * offset / 1000000);
      PpsClock clock(rate);
      const NormalizedTimestamp origin = normalizeTimestamp(4294967295LL, 999999999);
      const uint32_t edge = 0xFFFFFF00U;
      assert(clock.setLabelledPulse(1, edge, interval, origin));
      const uint32_t offsets[] = {0, 1, 7, rate / 10, rate - 1, rate,
                                 rate + 1, rate * 2, rate * 2 + rate / 2};
      for (const uint32_t elapsed : offsets) {
        NormalizedTimestamp timestamp = {};
        assert(clock.timestampAt(edge + elapsed, &timestamp));
        // Independent exact rational reference, rounded to the nearest nanosecond.
        const int64_t expected = static_cast<int64_t>(
            (static_cast<uint64_t>(elapsed) * 1000000000ULL + interval / 2U) / interval);
        assertWithinNanoseconds(nanosecondsSince(timestamp, origin), expected, 1);
        assert(timestamp.nanoseconds < 1000000000U);
      }
      NormalizedTimestamp timestamp = {};
      assert(!clock.timestampAt(edge + rate * 2 + rate / 2 + 1, &timestamp));
      assert(!clock.timestampAt(edge - 1, &timestamp));
    }
  }
}

// Retains sub-tick information when several PPS periods are averaged together.
void testFractionalAverageInterval() {
  PpsClock clock;
  assert(clock.setAnchor(1, 0, normalizeTimestamp(100, 0)));
  assert(clock.advanceToPulse(4, 3000001, 1000000));
  NormalizedTimestamp timestamp = {};
  assert(clock.timestampAt(4500001, &timestamp));
  assert(timestamp.secondsSince1900 == 104);
  assertWithinNanoseconds(timestamp.nanoseconds, 499999500, 1);
}

// Small oscillator changes must converge instead of sticking in the old integer deadband.
void testSmallFrequencyChangesConverge() {
  const uint32_t rates[] = {1000000, 150000000};
  const int32_t stepsPpm[] = {-4, -3, 3};
  for (const uint32_t rate : rates) {
    for (const int32_t step : stepsPpm) {
      PpsClock clock(rate);
      uint32_t edge = 0xFFFFFF00U;
      const uint32_t interval = static_cast<uint32_t>(
          static_cast<int64_t>(rate) + static_cast<int64_t>(rate) * step / 1000000);
      assert(clock.setLabelledPulse(1, edge, rate, normalizeTimestamp(100, 0)));
      for (uint32_t pulse = 2; pulse <= 129; ++pulse) {
        edge += interval;
        assert(clock.advanceToPulse(pulse, edge, interval));
      }
      NormalizedTimestamp timestamp = {};
      const uint32_t elapsed = rate * 2 + rate / 2;
      assert(clock.timestampAt(edge + elapsed, &timestamp));
      const int64_t expected = static_cast<int64_t>(
          (static_cast<uint64_t>(elapsed) * 1000000000ULL + interval / 2U) / interval);
      assertWithinNanoseconds(nanosecondsSince(timestamp, normalizeTimestamp(228, 0)),
                              expected, 1);
    }
  }
}

// Both the hardware counter and the software pulse count may wrap normally.
void testHighResolutionCounterAndPulseCountWrap() {
  PpsClock clock(150000000);
  const uint32_t edge = 0xFFFFFF00U;
  assert(clock.setLabelledPulse(UINT32_MAX, edge, 150000000,
                                normalizeTimestamp(4294967295LL, 500)));
  const uint32_t nextEdge = edge + 150000000U;
  assert(clock.advanceToPulse(0, nextEdge, 150000000));
  uint32_t pulse = 1;
  uint32_t anchorEdge = 0;
  NormalizedTimestamp anchor = {};
  assert(clock.getAnchor(&pulse, &anchorEdge, &anchor));
  assert(pulse == 0);
  assert(anchorEdge == nextEdge);
  assert(anchor.secondsSince1900 == 4294967296LL);
  assert(anchor.nanoseconds == 500);
  NormalizedTimestamp timestamp = {};
  assert(clock.timestampAt(nextEdge + 1, &timestamp));
  assert(timestamp.nanoseconds == 507);
}

// A skipped-pulse interval must fit entirely within one hardware counter wrap.
void testHighResolutionAdvanceLimit() {
  PpsClock clock(150000000);
  const uint32_t edge = 0xFFFFFF00U;
  assert(clock.setAnchor(1, edge, normalizeTimestamp(100, 0)));
  assert(clock.advanceToPulse(29, edge + 4200000000U, 150000000));
  assert(clock.setAnchor(1, edge, normalizeTimestamp(100, 0)));
  assert(!clock.advanceToPulse(30, edge + static_cast<uint32_t>(4350000000ULL), 150000000));
  assert(!clock.isAnchored());
}

// Validate tick-rate boundaries and ensure reset preserves the selected counter rate.
void testTickRateValidationAndReset() {
  PpsClock clock(150000000);
  assert(clock.isExpectedTickInterval(149850000));
  assert(clock.isExpectedTickInterval(150150000));
  assert(!clock.isExpectedTickInterval(149849999));
  assert(!clock.isExpectedTickInterval(150150001));
  assert(clock.setLabelledPulse(1, 0, 149850000, normalizeTimestamp(100, 0)));
  clock.reset();
  assert(!clock.isAnchored());
  assert(clock.setAnchor(1, 0, normalizeTimestamp(100, 0)));
  NormalizedTimestamp timestamp = {};
  assert(clock.timestampAt(75000000, &timestamp));
  assert(timestamp.nanoseconds == 500000000);
  assert(!clock.timestampAt(0, nullptr));
  PpsClock zeroRate(0);
  PpsClock excessiveRate(UINT32_MAX);
  assert(!zeroRate.setAnchor(1, 0, normalizeTimestamp(100, 0)));
  assert(!zeroRate.isExpectedTickInterval(0));
  assert(!excessiveRate.setAnchor(1, 0, normalizeTimestamp(100, 0)));
}
}

// Runs all PPS clock unit tests and reports success through the process exit code.
int main() {
  testAnchorAndInterpolation();
  testPulseAdvancement();
  testMicrosWrap();
  testInvalidTimingResetsClock();
  testMissedPulseTimingMustMatchPulseCount();
  testPpsDisciplinesInterpolationScale();
  testInitialLabelledPulseDisciplinesInterpolationScale();
  testHighResolutionInterpolationAccuracy();
  testFractionalAverageInterval();
  testSmallFrequencyChangesConverge();
  testHighResolutionCounterAndPulseCountWrap();
  testHighResolutionAdvanceLimit();
  testTickRateValidationAndReset();
  return 0;
}
