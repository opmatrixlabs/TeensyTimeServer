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

// ReSharper disable CppUnusedIncludeDirective
#pragma once

#include <stdint.h>

#include "NtpTimestamp.h"

class PpsClock {
public:
  static constexpr uint32_t MIN_PULSE_INTERVAL_MICROS = 999000;
  static constexpr uint32_t MAX_PULSE_INTERVAL_MICROS = 1001000;
  static constexpr uint32_t MAX_ADVANCE_PULSES = 3600;
  static constexpr uint32_t MAX_INTERPOLATION_MICROS = 2500000;

  // Initializes clock interpolation and interval limits for the selected timer frequency.
  explicit PpsClock(uint32_t ticksPerSecond = 1000000);
  void reset();
  bool setAnchor(uint32_t pulseCount,
                 uint32_t edgeTicks,
                 const NormalizedTimestamp& utcAtEdge);
  bool setLabelledPulse(uint32_t pulseCount,
                        uint32_t edgeTicks,
                        uint32_t intervalTicks,
                        const NormalizedTimestamp& utcAtEdge);
  bool advanceToPulse(uint32_t pulseCount,
                      uint32_t edgeTicks,
                      uint32_t intervalTicks);
  // The caller must reject stale captures: a complete counter wrap is ambiguous.
  bool timestampAt(uint32_t captureTicks, NormalizedTimestamp* timestamp) const;
  bool getAnchor(uint32_t* pulseCount,
                 uint32_t* edgeTicks,
                 NormalizedTimestamp* utcAtEdge) const;
  bool isAnchored() const;

  // Checks whether a measured pulse interval is within the configured timer's allowed range.
  bool isExpectedTickInterval(uint32_t intervalTicks) const;
  static bool isExpectedPulseInterval(uint32_t intervalMicros);

private:
  // Recalculates the fixed-point tick-to-nanosecond scale from the disciplined pulse interval.
  void updateConversionScale();

  uint32_t ticksPerSecond_ = 1000000;
  uint32_t minimumIntervalTicks_ = MIN_PULSE_INTERVAL_MICROS;
  uint32_t maximumIntervalTicks_ = MAX_PULSE_INTERVAL_MICROS;
  uint32_t maximumInterpolationTicks_ = MAX_INTERPOLATION_MICROS;
  uint32_t maximumAdvancePulses_ = MAX_ADVANCE_PULSES;
  bool tickRateValid_ = false;
  bool anchored_ = false;
  bool intervalDisciplined_ = false;
  uint32_t pulseCount_ = 0;
  uint32_t edgeTicks_ = 0;
  uint64_t disciplinedIntervalTicksQ16_ = 0;
  uint64_t nanosecondsPerTickQ32_ = 0;
  NormalizedTimestamp utcAtEdge_ = {};
};
