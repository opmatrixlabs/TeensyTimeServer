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

#include "NtpTimestamp.h"

namespace {
constexpr int64_t NANOSECONDS_PER_SECOND = 1000000000LL;
constexpr uint64_t NTP_FRACTION_SCALE = 1ULL << 32;
}

NormalizedTimestamp normalizeTimestamp(const int64_t secondsSince1900, const int64_t nanoseconds) {
  NormalizedTimestamp timestamp = {};
  timestamp.secondsSince1900 = secondsSince1900 + nanoseconds / NANOSECONDS_PER_SECOND;

  int64_t normalizedNanoseconds = nanoseconds % NANOSECONDS_PER_SECOND;
  if (normalizedNanoseconds < 0) {
    normalizedNanoseconds += NANOSECONDS_PER_SECOND;
    timestamp.secondsSince1900 -= 1;
  }

  timestamp.nanoseconds = static_cast<uint32_t>(normalizedNanoseconds);
  return timestamp;
}

NtpTimestamp toNtpTimestamp(const NormalizedTimestamp& timestamp) {
  NtpTimestamp ntpTimestamp = {};
  ntpTimestamp.seconds = static_cast<uint32_t>(timestamp.secondsSince1900);
  ntpTimestamp.fraction = static_cast<uint32_t>(
      (static_cast<uint64_t>(timestamp.nanoseconds) * NTP_FRACTION_SCALE) / NANOSECONDS_PER_SECOND);
  return ntpTimestamp;
}

void writeUint32BigEndian(uint8_t* destination, const uint32_t value) {
  destination[0] = static_cast<uint8_t>((value >> 24) & 0xFF);
  destination[1] = static_cast<uint8_t>((value >> 16) & 0xFF);
  destination[2] = static_cast<uint8_t>((value >> 8) & 0xFF);
  destination[3] = static_cast<uint8_t>(value & 0xFF);
}

void writeNtpTimestamp(uint8_t* destination, const NtpTimestamp& timestamp) {
  writeUint32BigEndian(destination, timestamp.seconds);
  writeUint32BigEndian(destination + 4, timestamp.fraction);
}
