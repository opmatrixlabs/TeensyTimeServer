#include "../ClockPrecision.h"

#include <assert.h>
#include <stdint.h>

namespace {
// Verifies conservative exponent selection at timer and power-of-two boundaries.
void testTargetBoundaries() {
  assert(precisionForClock(600000000, 1, 572) == -20);
  assert(precisionForClock(600000000, 1, 573) == -19);
  assert(precisionForClock(600000000, 1, 1144) == -19);
  assert(precisionForClock(600000000, 1, 1145) == -18);
  assert(precisionForClock(150000000, 1, 143) == -20);
  assert(precisionForClock(150000000, 1, 144) == -19);

  // Exact powers of two fit; a duration just above the limit must round up.
  assert(precisionForClock(1U << 20, 1, 1) == -20);
  assert(precisionForClock((1U << 20) - 1, 1, 1) == -19);
  assert(precisionForClock(1U << 20, 1, 2) == -19);
  assert(precisionForClock(1U << 20, 1, 3) == -18);
}

// Verifies that clock resolution and read cost each constrain the reported precision.
void testResolutionAndReadCostBothConstrainPrecision() {
  assert(precisionForClock(600000000, 600, 100) == -19);
  assert(precisionForClock(600000000, 100, 600) == -19);
  // Whole-microsecond readings do not quite satisfy a 2^-20-second limit.
  assert(precisionForClock(1000000, 1, 1) == -19);
  // Faster counters never make this firmware advertise finer than its target.
  assert(precisionForClock(UINT32_MAX, 1, 1) == NTP_TARGET_PRECISION);
}

// Verifies safe precision selection for zero inputs and integer limits.
void testZeroAndLargeInputs() {
  assert(precisionForClock(0, 1, 1) == 32);
  assert(precisionForClock(0, 0, 0) == 32);
  assert(precisionForClock(600000000, 0, 0) == -20);
  assert(precisionForClock(1, 0, 0) == 0);
  assert(precisionForClock(1, 1, 1) == 0);
  assert(precisionForClock(1, 1, 2) == 1);
  assert(precisionForClock(1, 1, 3) == 2);
  assert(precisionForClock(1, UINT32_MAX, 1) == 32);
  assert(precisionForClock(UINT32_MAX, UINT32_MAX, UINT32_MAX) == 0);
  assert(precisionForClock(UINT32_MAX, UINT32_MAX / 2, 1) == -1);
  assert(precisionForClock(UINT32_MAX, UINT32_MAX / 2 + 1, 1) == 0);
}
}

// Runs all clock precision unit tests and reports success through the process exit code.
int main() {
  testTargetBoundaries();
  testResolutionAndReadCostBothConstrainPrecision();
  testZeroAndLargeInputs();
  return 0;
}
