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
#include "GnssStatus.h"

#include <limits.h>

// Clears the cached GNSS status snapshot.
void GnssStatusCache::clear() {
  hasSnapshot_ = false;
  snapshot_ = {};
}

// Stores a new GNSS status snapshot in the cache.
void GnssStatusCache::update(const GnssStatusSnapshot& snapshot) {
  snapshot_ = snapshot;
  hasSnapshot_ = true;
}

// Copies the cached GNSS status snapshot to the caller when one is available.
bool GnssStatusCache::get(GnssStatusSnapshot* snapshot) const {
  if (!hasSnapshot_ || snapshot == nullptr)
    return false;

  *snapshot = snapshot_;
  return true;
}

// Reports whether the cached snapshot is present and within the permitted age.
bool GnssStatusCache::isFresh(const uint32_t currentMillis,
                              const uint32_t maximumAgeMillis) const {
  return hasSnapshot_ &&
         static_cast<uint32_t>(currentMillis - snapshot_.receivedMillis) <= maximumAgeMillis;
}

// Converts the status refresh interval into a bounded NAV-PVT epoch rate.
uint8_t navPvtEpochRateForStatusFrequency(const uint32_t statusFrequencyMillis,
                                          uint32_t navigationEpochMillis) {
  if (navigationEpochMillis == 0)
    navigationEpochMillis = 1;

  uint32_t epochRate = statusFrequencyMillis / navigationEpochMillis;
  if (epochRate == 0)
    epochRate = 1;
  if (epochRate > MAX_NAV_PVT_EPOCH_RATE)
    epochRate = MAX_NAV_PVT_EPOCH_RATE;
  return static_cast<uint8_t>(epochRate);
}

// Calculates the elapsed time between NAV-PVT reports for an epoch rate.
uint32_t navPvtReportPeriodMillis(uint8_t epochRate,
                                  uint32_t navigationEpochMillis) {
  if (epochRate == 0)
    epochRate = 1;
  if (navigationEpochMillis == 0)
    navigationEpochMillis = 1;

  const uint64_t periodMillis =
      static_cast<uint64_t>(epochRate) * navigationEpochMillis;
  return periodMillis > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(periodMillis);
}

// Calculates how long a GNSS status snapshot remains fresh.
uint32_t gnssStatusFreshnessLimitMillis(const uint8_t epochRate,
                                        uint32_t navigationEpochMillis) {
  if (navigationEpochMillis == 0)
    navigationEpochMillis = 1;

  const uint64_t reportPeriodMillis =
      navPvtReportPeriodMillis(epochRate, navigationEpochMillis);
  const uint64_t freshnessLimitMillis = reportPeriodMillis * 2ULL + navigationEpochMillis;
  return freshnessLimitMillis > UINT32_MAX
             ? UINT32_MAX
             : static_cast<uint32_t>(freshnessLimitMillis);
}

// Returns a readable name for the GNSS fix state and type.
const char* gnssFixTypeName(const bool fixOk, const uint8_t fixType) {
  if (!fixOk)
    return "No Fix";

  switch (fixType) {
    case 0: return "No Fix";
    case 1: return "Dead Reckoning";
    case 2: return "2D Fix";
    case 3: return "3D Fix";
    case 4: return "GNSS + DR";
    case 5: return "Time Fix";
    default: return "Unknown";
  }
}
