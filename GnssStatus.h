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

constexpr uint8_t MAX_NAV_PVT_EPOCH_RATE = 127;

struct GnssStatusSnapshot {
  uint32_t receivedMillis = 0;
  uint32_t timeAccuracyNanoseconds = 0;
  int32_t nanoseconds = 0;
  uint16_t year = 0;
  uint8_t month = 0;
  uint8_t day = 0;
  uint8_t hour = 0;
  uint8_t minute = 0;
  uint8_t second = 0;
  uint8_t fixType = 0;
  uint8_t satellitesUsed = 0;
  bool fixOk = false;
  bool validDate = false;
  bool validTime = false;
  bool fullyResolved = false;
};

class GnssStatusCache {
public:
  void clear();
  void update(const GnssStatusSnapshot& snapshot);
  bool get(GnssStatusSnapshot* snapshot) const;
  bool isFresh(uint32_t currentMillis, uint32_t maximumAgeMillis) const;

private:
  bool hasSnapshot_ = false;
  GnssStatusSnapshot snapshot_ = {};
};

uint8_t navPvtEpochRateForStatusFrequency(uint32_t statusFrequencyMillis,
                                          uint32_t navigationEpochMillis = 1000);
uint32_t navPvtReportPeriodMillis(uint8_t epochRate,
                                  uint32_t navigationEpochMillis = 1000);
uint32_t gnssStatusFreshnessLimitMillis(uint8_t epochRate,
                                        uint32_t navigationEpochMillis = 1000);
const char* gnssFixTypeName(bool fixOk, uint8_t fixType);
