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

// Clears the PPS anchor and restores the default pulse-interval estimate.
void PpsClock::reset() {
  anchored_ = false;
  intervalDisciplined_ = false;
  pulseCount_ = 0;
  edgeMicros_ = 0;
  disciplinedIntervalMicros_ = 1000000;
  utcAtEdge_ = {};
}

// Associates a captured PPS edge with its corresponding normalized UTC timestamp.
bool PpsClock::setAnchor(const uint32_t pulseCount,
                         const uint32_t edgeMicros,
                         const NormalizedTimestamp& utcAtEdge) {
  if (utcAtEdge.secondsSince1900 <= 0)
    return false;

  pulseCount_ = pulseCount;
  edgeMicros_ = edgeMicros;
  utcAtEdge_ = normalizeTimestamp(utcAtEdge.secondsSince1900, utcAtEdge.nanoseconds);
  anchored_ = true;
  return true;
}

// Validates a labeled PPS observation and uses it to establish or refresh the UTC anchor.
bool PpsClock::setLabelledPulse(const uint32_t pulseCount,
                                const uint32_t edgeMicros,
                                const uint32_t intervalMicros,
                                const NormalizedTimestamp& utcAtEdge) {
  if (!isExpectedPulseInterval(intervalMicros)) {
    if (anchored_)
      reset();
    return false;
  }

  if (anchored_) {
    if (!advanceToPulse(pulseCount, edgeMicros, intervalMicros))
      return false;
  }
  else {
    disciplinedIntervalMicros_ = intervalMicros;
    intervalDisciplined_ = true;
  }
  return setAnchor(pulseCount, edgeMicros, utcAtEdge);
}

// Advances the UTC anchor to a later valid PPS edge while refining the measured interval.
bool PpsClock::advanceToPulse(const uint32_t pulseCount,
                              const uint32_t edgeMicros,
                              const uint32_t intervalMicros) {
  if (!anchored_)
    return false;

  const uint32_t elapsedPulses = pulseCount - pulseCount_;
  if (elapsedPulses == 0)
    return true;

  if (elapsedPulses > MAX_ADVANCE_PULSES || !isExpectedPulseInterval(intervalMicros)) {
    reset();
    return false;
  }

  const uint32_t elapsedMicros = edgeMicros - edgeMicros_;
  const uint64_t minimumElapsedMicros =
      static_cast<uint64_t>(elapsedPulses) * MIN_PULSE_INTERVAL_MICROS;
  const uint64_t maximumElapsedMicros =
      static_cast<uint64_t>(elapsedPulses) * MAX_PULSE_INTERVAL_MICROS;
  if (elapsedMicros < minimumElapsedMicros || elapsedMicros > maximumElapsedMicros) {
    reset();
    return false;
  }

  const uint32_t averageIntervalMicros = elapsedMicros / elapsedPulses;
  if (!intervalDisciplined_) {
    disciplinedIntervalMicros_ = averageIntervalMicros;
    intervalDisciplined_ = true;
  }
  else {
    // Smooth PPS-capture quantization while tracking the Teensy's oscillator.
    disciplinedIntervalMicros_ =
        (disciplinedIntervalMicros_ * 7U + averageIntervalMicros + 4U) / 8U;
  }

  utcAtEdge_ = normalizeTimestamp(utcAtEdge_.secondsSince1900 + elapsedPulses,
                                  utcAtEdge_.nanoseconds);
  pulseCount_ = pulseCount;
  edgeMicros_ = edgeMicros;
  return true;
}

// Interpolates a normalized UTC timestamp for a microsecond capture near the current PPS anchor.
bool PpsClock::timestampAt(const uint32_t captureMicros, NormalizedTimestamp* timestamp) const {
  if (!anchored_ || timestamp == nullptr)
    return false;

  const uint32_t elapsedMicros = captureMicros - edgeMicros_;
  if (elapsedMicros > MAX_INTERPOLATION_MICROS)
    return false;

  const uint64_t elapsedNanoseconds =
      (static_cast<uint64_t>(elapsedMicros) * NANOSECONDS_PER_SECOND +
       disciplinedIntervalMicros_ / 2U) /
      disciplinedIntervalMicros_;
  *timestamp = normalizeTimestamp(
      utcAtEdge_.secondsSince1900,
      static_cast<int64_t>(utcAtEdge_.nanoseconds) + static_cast<int64_t>(elapsedNanoseconds));
  return true;
}

// Copies the current PPS anchor details to the caller when an anchor is available.
bool PpsClock::getAnchor(uint32_t* pulseCount,
                         uint32_t* edgeMicros,
                         NormalizedTimestamp* utcAtEdge) const {
  if (!anchored_ || pulseCount == nullptr || edgeMicros == nullptr || utcAtEdge == nullptr)
    return false;

  *pulseCount = pulseCount_;
  *edgeMicros = edgeMicros_;
  *utcAtEdge = utcAtEdge_;
  return true;
}

// Reports whether the clock currently has a valid PPS-to-UTC anchor.
bool PpsClock::isAnchored() const {
  return anchored_;
}

// Reports whether a measured pulse interval falls within the accepted PPS timing range.
bool PpsClock::isExpectedPulseInterval(const uint32_t intervalMicros) {
  return intervalMicros >= MIN_PULSE_INTERVAL_MICROS &&
         intervalMicros <= MAX_PULSE_INTERVAL_MICROS;
}
