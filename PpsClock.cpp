/*
 * Copyright (c) 2026. Andrew Kevin Bailey
 * This code, firmware, and software is released under the MIT License (http://opensource.org/licenses/MIT).
 *
 * The MIT License (MIT)
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or significant portions of
 * the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "PpsClock.h"

namespace {
constexpr uint64_t NANOSECONDS_PER_SECOND = 1000000000ULL;
}

// Accepts counter rates whose interpolation window fits within one 32-bit wrap.
PpsClock::PpsClock(const uint32_t ticksPerSecond) : ticksPerSecond_(ticksPerSecond) {
  const uint64_t maximumInterpolationTicks =
      static_cast<uint64_t>(ticksPerSecond) * MAX_INTERPOLATION_MICROS / 1000000U;
  tickRateValid_ = ticksPerSecond != 0 && maximumInterpolationTicks <= UINT32_MAX;
  if (tickRateValid_) {
    minimumIntervalTicks_ = static_cast<uint32_t>(
        (static_cast<uint64_t>(ticksPerSecond) * 999U + 999U) / 1000U);
    maximumIntervalTicks_ = static_cast<uint32_t>(
        static_cast<uint64_t>(ticksPerSecond) * 1001U / 1000U);
    maximumInterpolationTicks_ = static_cast<uint32_t>(maximumInterpolationTicks);
    maximumAdvancePulses_ = UINT32_MAX / maximumIntervalTicks_;
    if (maximumAdvancePulses_ > MAX_ADVANCE_PULSES)
      maximumAdvancePulses_ = MAX_ADVANCE_PULSES;
  }
  reset();
}

// Clears the PPS anchor and restores the default pulse-interval estimate.
void PpsClock::reset() {
  anchored_ = false;
  intervalDisciplined_ = false;
  pulseCount_ = 0;
  edgeTicks_ = 0;
  disciplinedIntervalTicksQ16_ = static_cast<uint64_t>(ticksPerSecond_) << 16;
  updateConversionScale();
  utcAtEdge_ = {};
}

// Associates a captured PPS edge with its corresponding normalized UTC timestamp.
bool PpsClock::setAnchor(const uint32_t pulseCount,
                         const uint32_t edgeTicks,
                         const NormalizedTimestamp& utcAtEdge) {
  if (!tickRateValid_ || utcAtEdge.secondsSince1900 <= 0)
    return false;

  pulseCount_ = pulseCount;
  edgeTicks_ = edgeTicks;
  utcAtEdge_ = normalizeTimestamp(utcAtEdge.secondsSince1900, utcAtEdge.nanoseconds);
  anchored_ = true;
  return true;
}

// Validates a labeled PPS observation and uses it to establish or refresh the UTC anchor.
bool PpsClock::setLabelledPulse(const uint32_t pulseCount,
                                const uint32_t edgeTicks,
                                const uint32_t intervalTicks,
                                const NormalizedTimestamp& utcAtEdge) {
  if (!isExpectedTickInterval(intervalTicks)) {
    if (anchored_)
      reset();
    return false;
  }

  if (anchored_) {
    if (!advanceToPulse(pulseCount, edgeTicks, intervalTicks))
      return false;
  }
  else {
    disciplinedIntervalTicksQ16_ = static_cast<uint64_t>(intervalTicks) << 16;
    intervalDisciplined_ = true;
    updateConversionScale();
  }
  return setAnchor(pulseCount, edgeTicks, utcAtEdge);
}

// Advances the UTC anchor to a later valid PPS edge while refining the measured interval.
bool PpsClock::advanceToPulse(const uint32_t pulseCount,
                              const uint32_t edgeTicks,
                              const uint32_t intervalTicks) {
  if (!anchored_)
    return false;

  const uint32_t elapsedPulses = pulseCount - pulseCount_;
  if (elapsedPulses == 0)
    return true;

  if (elapsedPulses > maximumAdvancePulses_ || !isExpectedTickInterval(intervalTicks)) {
    reset();
    return false;
  }

  const uint32_t elapsedTicks = edgeTicks - edgeTicks_;
  const uint64_t minimumElapsedTicks =
      static_cast<uint64_t>(elapsedPulses) * minimumIntervalTicks_;
  const uint64_t maximumElapsedTicks =
      static_cast<uint64_t>(elapsedPulses) * maximumIntervalTicks_;
  if (elapsedTicks < minimumElapsedTicks || elapsedTicks > maximumElapsedTicks) {
    reset();
    return false;
  }

  const uint64_t averageIntervalTicksQ16 =
      ((static_cast<uint64_t>(elapsedTicks) << 16) + elapsedPulses / 2U) / elapsedPulses;
  if (!intervalDisciplined_) {
    disciplinedIntervalTicksQ16_ = averageIntervalTicksQ16;
    intervalDisciplined_ = true;
  }
  else {
    // Preserve fractional ticks so small frequency changes survive the 1/8 filter.
    disciplinedIntervalTicksQ16_ =
        (disciplinedIntervalTicksQ16_ * 7U + averageIntervalTicksQ16 + 4U) / 8U;
  }
  updateConversionScale();

  utcAtEdge_.secondsSince1900 += elapsedPulses;
  pulseCount_ = pulseCount;
  edgeTicks_ = edgeTicks;
  return true;
}

// Interpolates and normalizes with multiplication, shifts, and bounded subtraction.
bool PpsClock::timestampAt(const uint32_t captureTicks, NormalizedTimestamp* timestamp) const {
  if (!anchored_ || timestamp == nullptr)
    return false;

  const uint32_t elapsedTicks = captureTicks - edgeTicks_;
  if (elapsedTicks > maximumInterpolationTicks_)
    return false;

  const uint32_t elapsedNanoseconds = static_cast<uint32_t>(
      (static_cast<uint64_t>(elapsedTicks) * nanosecondsPerTickQ32_ + (1ULL << 31)) >> 32);
  timestamp->secondsSince1900 = utcAtEdge_.secondsSince1900;
  // The accepted 2.5-second window and 1000 ppm tolerance keep this below 2^32.
  uint32_t nanoseconds = utcAtEdge_.nanoseconds + elapsedNanoseconds;
  if (nanoseconds >= 2000000000U) {
    nanoseconds -= 2000000000U;
    timestamp->secondsSince1900 += 2;
  }
  if (nanoseconds >= 1000000000U) {
    nanoseconds -= 1000000000U;
    ++timestamp->secondsSince1900;
  }
  timestamp->nanoseconds = nanoseconds;
  return true;
}

// Copies the current PPS anchor details to the caller when an anchor is available.
bool PpsClock::getAnchor(uint32_t* pulseCount,
                         uint32_t* edgeTicks,
                         NormalizedTimestamp* utcAtEdge) const {
  if (!anchored_ || pulseCount == nullptr || edgeTicks == nullptr || utcAtEdge == nullptr)
    return false;

  *pulseCount = pulseCount_;
  *edgeTicks = edgeTicks_;
  *utcAtEdge = utcAtEdge_;
  return true;
}

// Reports whether the clock currently has a valid PPS-to-UTC anchor.
bool PpsClock::isAnchored() const {
  return anchored_;
}

// Computes a rounded Q32 nanosecond scale without requiring 128-bit arithmetic.
void PpsClock::updateConversionScale() {
  if (!tickRateValid_) {
    nanosecondsPerTickQ32_ = 0;
    return;
  }
  const uint64_t numerator = NANOSECONDS_PER_SECOND << 32;
  const uint64_t whole = numerator / disciplinedIntervalTicksQ16_;
  const uint64_t remainder = numerator % disciplinedIntervalTicksQ16_;
  nanosecondsPerTickQ32_ = (whole << 16) +
      ((remainder << 16) + disciplinedIntervalTicksQ16_ / 2U) /
          disciplinedIntervalTicksQ16_;
}

// Tests a measured interval in the configured hardware counter's tick units.
bool PpsClock::isExpectedTickInterval(const uint32_t intervalTicks) const {
  return tickRateValid_ && intervalTicks >= minimumIntervalTicks_ &&
         intervalTicks <= maximumIntervalTicks_;
}

// Reports whether a measured pulse interval falls within the accepted PPS timing range.
bool PpsClock::isExpectedPulseInterval(const uint32_t intervalMicros) {
  return intervalMicros >= MIN_PULSE_INTERVAL_MICROS &&
         intervalMicros <= MAX_PULSE_INTERVAL_MICROS;
}
