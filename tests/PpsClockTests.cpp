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
  return 0;
}
