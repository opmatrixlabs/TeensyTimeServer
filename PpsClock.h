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

#pragma once

#include <stdint.h>

#include "NtpTimestamp.h"

class PpsClock {
public:
  static constexpr uint32_t MIN_PULSE_INTERVAL_MICROS = 999000;
  static constexpr uint32_t MAX_PULSE_INTERVAL_MICROS = 1001000;
  static constexpr uint32_t MAX_ADVANCE_PULSES = 3600;
  static constexpr uint32_t MAX_INTERPOLATION_MICROS = 2500000;

  void reset();
  bool setAnchor(uint32_t pulseCount,
                 uint32_t edgeMicros,
                 const NormalizedTimestamp& utcAtEdge);
  bool setLabelledPulse(uint32_t pulseCount,
                        uint32_t edgeMicros,
                        uint32_t intervalMicros,
                        const NormalizedTimestamp& utcAtEdge);
  bool advanceToPulse(uint32_t pulseCount,
                      uint32_t edgeMicros,
                      uint32_t intervalMicros);
  bool timestampAt(uint32_t captureMicros, NormalizedTimestamp* timestamp) const;
  bool getAnchor(uint32_t* pulseCount,
                 uint32_t* edgeMicros,
                 NormalizedTimestamp* utcAtEdge) const;
  bool isAnchored() const;

  static bool isExpectedPulseInterval(uint32_t intervalMicros);

private:
  bool anchored_ = false;
  bool intervalDisciplined_ = false;
  uint32_t pulseCount_ = 0;
  uint32_t edgeMicros_ = 0;
  uint32_t disciplinedIntervalMicros_ = 1000000;
  NormalizedTimestamp utcAtEdge_ = {};
};
