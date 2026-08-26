/*
 * Copyright (c) 2025. Andrew Kevin Bailey
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

#include "../NtpTimestamp.h"

#include <assert.h>

void testNormalization() {
  NormalizedTimestamp timestamp = normalizeTimestamp(100, 1000000000LL);
  assert(timestamp.secondsSince1900 == 101);
  assert(timestamp.nanoseconds == 0);

  timestamp = normalizeTimestamp(100, -1);
  assert(timestamp.secondsSince1900 == 99);
  assert(timestamp.nanoseconds == 999999999);

  timestamp = normalizeTimestamp(100, -5000000);
  assert(timestamp.secondsSince1900 == 99);
  assert(timestamp.nanoseconds == 995000000);

  timestamp = normalizeTimestamp(100, 2500000000LL);
  assert(timestamp.secondsSince1900 == 102);
  assert(timestamp.nanoseconds == 500000000);
}

void testNtpFractionConversion() {
  assert(toNtpTimestamp(normalizeTimestamp(0, 0)).fraction == 0x00000000UL);
  assert(toNtpTimestamp(normalizeTimestamp(0, 1)).fraction == 0x00000004UL);
  assert(toNtpTimestamp(normalizeTimestamp(0, 2)).fraction == 0x00000008UL);
  assert(toNtpTimestamp(normalizeTimestamp(0, 125000000)).fraction == 0x20000000UL);
  assert(toNtpTimestamp(normalizeTimestamp(0, 250000000)).fraction == 0x40000000UL);
  assert(toNtpTimestamp(normalizeTimestamp(0, 499999999)).fraction == 0x7FFFFFFBUL);
  assert(toNtpTimestamp(normalizeTimestamp(0, 500000000)).fraction == 0x80000000UL);
  assert(toNtpTimestamp(normalizeTimestamp(0, 500000001)).fraction == 0x80000004UL);
  assert(toNtpTimestamp(normalizeTimestamp(0, 750000000)).fraction == 0xC0000000UL);
  assert(toNtpTimestamp(normalizeTimestamp(0, 999999999)).fraction == 0xFFFFFFFBUL);
}

void testNtpEraOffsetConversion() {
  assert(toNtpTimestamp(normalizeTimestamp(4294967295LL, 0)).seconds == 0xFFFFFFFFUL);
  assert(toNtpTimestamp(normalizeTimestamp(4294967296LL, 0)).seconds == 0x00000000UL);
  assert(toNtpTimestamp(normalizeTimestamp(4294967297LL, 0)).seconds == 0x00000001UL);
}

void testBigEndianSerialization() {
  uint8_t valueBytes[4] = {};
  writeUint32BigEndian(valueBytes, 0x12345678UL);
  assert(valueBytes[0] == 0x12);
  assert(valueBytes[1] == 0x34);
  assert(valueBytes[2] == 0x56);
  assert(valueBytes[3] == 0x78);

  const NtpTimestamp timestamp = {0x01020304UL, 0x80000000UL};
  uint8_t timestampBytes[10] = {0xA5, 0, 0, 0, 0, 0, 0, 0, 0, 0xA5};
  writeNtpTimestamp(timestampBytes + 1, timestamp);
  assert(timestampBytes[0] == 0xA5);
  assert(timestampBytes[1] == 0x01);
  assert(timestampBytes[2] == 0x02);
  assert(timestampBytes[3] == 0x03);
  assert(timestampBytes[4] == 0x04);
  assert(timestampBytes[5] == 0x80);
  assert(timestampBytes[6] == 0x00);
  assert(timestampBytes[7] == 0x00);
  assert(timestampBytes[8] == 0x00);
  assert(timestampBytes[9] == 0xA5);
}

int main() {
  testNormalization();
  testNtpFractionConversion();
  testNtpEraOffsetConversion();
  testBigEndianSerialization();
  return 0;
}
