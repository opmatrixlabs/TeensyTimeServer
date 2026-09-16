// ReSharper disable CppUnusedIncludeDirective
#pragma once

#include <cstdint>

// This firmware's target clock precision is 2^-20 seconds (about 954 ns).
constexpr int NTP_TARGET_PRECISION = -20;

// Returns the smallest exponent no finer than the target that contains both
// clock quantization and the measured clock-read time. All inputs use the same
// counter. Zero resolution/read durations mean one tick; a zero counter rate
// returns the conservative maximum supported exponent (+32).
int8_t precisionForClock(uint32_t ticksPerSecond,
                        uint32_t resolutionTicks,
                        uint32_t readTicks);
