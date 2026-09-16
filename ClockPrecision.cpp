#include "ClockPrecision.h"

// Selects a conservative NTP precision exponent from clock resolution and read duration.
int8_t precisionForClock(const uint32_t ticksPerSecond,
                        const uint32_t resolutionTicks,
                        const uint32_t readTicks) {
  if (ticksPerSecond == 0)
    return 32;

  uint64_t intervalTicks = resolutionTicks > readTicks ? resolutionTicks : readTicks;
  if (intervalTicks == 0)
    intervalTicks = 1;

  // Compare intervalTicks / ticksPerSecond <= 2^exponent using integers.
  // At most 20 fractional bits and 32 whole bits keep every shift in uint64_t.
  // Rounding up avoids advertising a precision finer than the measured clock.
  for (int exponent = NTP_TARGET_PRECISION; exponent <= 32; ++exponent) {
    const bool fits = exponent < 0
      ? (intervalTicks << -exponent) <= ticksPerSecond
      : intervalTicks <= (static_cast<uint64_t>(ticksPerSecond) << exponent);
    if (fits)
      return static_cast<int8_t>(exponent);
  }

  return 32;
}
