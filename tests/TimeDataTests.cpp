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

#include "../TimeData.h"

#include <assert.h>
#include <string>

namespace {
void assertStringEquals(const String& actual, const char* expected) {
  assert(std::string(actual.c_str()) == expected);
}

void testNanosecondsRetainTheirNineDigitScale() {
  TimeData time(2026, 8, 26, 12, 34, 56, 30000000);
  assertStringEquals(time.getISO8601Time(6), "2026-08-26T12:34:56.030000");

  time.setSubSec(0);
  assertStringEquals(time.getISO8601Time(6), "2026-08-26T12:34:56.000000");

  time.setSubSec(1);
  assertStringEquals(time.getISO8601Time(9), "2026-08-26T12:34:56.000000001");
  assertStringEquals(time.getISO8601Time(6), "2026-08-26T12:34:56.000000");

  time.setSubSec(123456789);
  assertStringEquals(time.getISO8601Time(3), "2026-08-26T12:34:56.123");
  assertStringEquals(time.getISO8601Time(6), "2026-08-26T12:34:56.123456");
  assertStringEquals(time.getISO8601Time(9), "2026-08-26T12:34:56.123456789");

  time.setSubSec(999999999);
  assertStringEquals(time.getISO8601Time(9), "2026-08-26T12:34:56.999999999");
}

void testRtcHundredthsFormattingIsUnchanged() {
  assertStringEquals(TimeData::toISO8601Time(2026, 8, 26, 12, 34, 56, 3, 2),
                     "2026-08-26T12:34:56.03");
  assertStringEquals(TimeData::toISO8601Time(2026, 8, 26, 12, 34, 56, 30, 2),
                     "2026-08-26T12:34:56.30");
}

void testPpsLogPrecisionUsesTwoDigits() {
  TimeData time(2026, 8, 30, 18, 20, 55, 123456789);
  assertStringEquals(time.getISO8601Time(2), "2026-08-30T18:20:55.12");

  time.setSubSec(0);
  assertStringEquals(time.getISO8601Time(2), "2026-08-30T18:20:55.00");
}

void testSecondsSince1900RoundTrip() {
  const TimeData samples[] = {
      TimeData(1900, 1, 1, 0, 0, 0, 0),
      TimeData(1999, 12, 31, 23, 59, 59, 0),
      TimeData(2000, 2, 29, 12, 34, 56, 0),
      TimeData(2026, 8, 26, 19, 20, 21, 0),
      TimeData(2099, 12, 31, 23, 59, 59, 0),
  };

  for (TimeData sample : samples) {
    const uint64_t seconds = sample.secondsSince1900();
    TimeData restored;
    assert(restored.setSecondsSince1900(seconds));
    assert(restored.secondsSince1900() == seconds);
    assertStringEquals(restored.getISO8601Time(6), sample.getISO8601Time(6).c_str());
  }
}
}

int main() {
  testNanosecondsRetainTheirNineDigitScale();
  testRtcHundredthsFormattingIsUnchanged();
  testPpsLogPrecisionUsesTwoDigits();
  testSecondsSince1900RoundTrip();
  return 0;
}
