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

#pragma once

struct NormalizedTimestamp {
  int64_t secondsSince1900; // Full seconds retain the NTP era across the 2036 wire rollover.
  uint32_t nanoseconds;     // Normalized range: 0 through 999,999,999.
};

struct NtpTimestamp {
  uint32_t seconds;  // Low 32 bits of seconds since 1900 (the NTP era offset).
  uint32_t fraction; // Binary fraction in units of 2^-32 seconds.
};

NormalizedTimestamp normalizeTimestamp(int64_t secondsSince1900, int64_t nanoseconds);
NtpTimestamp toNtpTimestamp(const NormalizedTimestamp& timestamp);
void writeUint32BigEndian(uint8_t* destination, uint32_t value);
void writeNtpTimestamp(uint8_t* destination, const NtpTimestamp& timestamp);
