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

#include "../GnssStatus.h"

#include <assert.h>
#include <string.h>

namespace {
// Verifies conversion from status refresh frequency to the corresponding NAV-PVT epoch rate.
void testStatusFrequencyConversion() {
  assert(navPvtEpochRateForStatusFrequency(0) == 1);
  assert(navPvtEpochRateForStatusFrequency(1) == 1);
  assert(navPvtEpochRateForStatusFrequency(999) == 1);
  assert(navPvtEpochRateForStatusFrequency(1000) == 1);
  assert(navPvtEpochRateForStatusFrequency(1500) == 1);
  assert(navPvtEpochRateForStatusFrequency(1999) == 1);
  assert(navPvtEpochRateForStatusFrequency(2000) == 2);
  assert(navPvtEpochRateForStatusFrequency(60000) == 60);
  assert(navPvtEpochRateForStatusFrequency(65535) == 65);
  assert(navPvtEpochRateForStatusFrequency(200000) == MAX_NAV_PVT_EPOCH_RATE);
}

// Verifies calculation of NAV-PVT reporting periods and GNSS status freshness limits.
void testReportAndFreshnessPeriods() {
  assert(navPvtReportPeriodMillis(0) == 1000);
  assert(navPvtReportPeriodMillis(60) == 60000);
  assert(gnssStatusFreshnessLimitMillis(1) == 3000);
  assert(gnssStatusFreshnessLimitMillis(60) == 121000);
}

// Verifies that GNSS status snapshots can be stored, retrieved, aged, and cleared correctly.
void testSnapshotAndFreshness() {
  GnssStatusCache cache;
  GnssStatusSnapshot snapshot = {};
  assert(!cache.get(&snapshot));
  assert(!cache.isFresh(1000, 3000));

  snapshot.receivedMillis = 1000;
  snapshot.fixOk = true;
  snapshot.fixType = 3;
  snapshot.satellitesUsed = 14;
  snapshot.timeAccuracyNanoseconds = 20;
  cache.update(snapshot);

  GnssStatusSnapshot restored = {};
  assert(cache.get(&restored));
  assert(restored.fixOk);
  assert(restored.fixType == 3);
  assert(restored.satellitesUsed == 14);
  assert(restored.timeAccuracyNanoseconds == 20);
  assert(cache.isFresh(4000, 3000));
  assert(!cache.isFresh(4001, 3000));

  cache.clear();
  assert(!cache.get(&restored));
}

// Verifies GNSS status freshness calculations across the millisecond counter rollover.
void testMillisRolloverFreshness() {
  GnssStatusCache cache;
  GnssStatusSnapshot snapshot = {};
  snapshot.receivedMillis = 0xFFFFFFF0UL;
  cache.update(snapshot);
  assert(cache.isFresh(0x00000010UL, 32));
  assert(!cache.isFresh(0x00000011UL, 32));
}

// Verifies the human-readable names assigned to every supported GNSS fix type.
void testFixNames() {
  assert(strcmp(gnssFixTypeName(false, 3), "No Fix") == 0);
  assert(strcmp(gnssFixTypeName(true, 0), "No Fix") == 0);
  assert(strcmp(gnssFixTypeName(true, 1), "Dead Reckoning") == 0);
  assert(strcmp(gnssFixTypeName(true, 2), "2D Fix") == 0);
  assert(strcmp(gnssFixTypeName(true, 3), "3D Fix") == 0);
  assert(strcmp(gnssFixTypeName(true, 4), "GNSS + DR") == 0);
  assert(strcmp(gnssFixTypeName(true, 5), "Time Fix") == 0);
  assert(strcmp(gnssFixTypeName(true, 6), "Unknown") == 0);
}
}

// Runs all GNSS status unit tests and reports success through the process exit code.
int main() {
  testStatusFrequencyConversion();
  testReportAndFreshnessPeriods();
  testSnapshotAndFreshness();
  testMillisRolloverFreshness();
  testFixNames();
  return 0;
}
