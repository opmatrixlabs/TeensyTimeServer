/*
 * Copyright (c) 2026. Andrew Kevin Bailey
 * This code, firmware, and software is released under the MIT License (http://opensource.org/licenses/MIT).
 */

#include "../RtcTimestamp.h"

#include <assert.h>
#include <iterator>

namespace {

// Verifies that a valid RV-1805 register burst decodes into the expected timestamp.
void testValidTimestamp() {
  const uint8_t registers[RV1805_TIMESTAMP_REGISTER_COUNT] = {
      0x75, 0x55, 0x20, 0x18, 0x30, 0x08, 0x26, 0x00
  };
  RtcDateTime timestamp = {};

  assert(decodeRv1805Timestamp(registers, std::size(registers), &timestamp));
  assert(timestamp.year == 2026);
  assert(timestamp.month == 8);
  assert(timestamp.day == 30);
  assert(timestamp.hour == 18);
  assert(timestamp.minute == 20);
  assert(timestamp.second == 55);
  assert(timestamp.hundredths == 75);
}

// Verifies that general-purpose register bits do not alter decoded date and time fields.
void testGeneralPurposeBitsAreIgnored() {
  const uint8_t registers[RV1805_TIMESTAMP_REGISTER_COUNT] = {
      0x03, 0xC4, 0xD3, 0xD2, 0xE9, 0xE2, 0x24, 0xF8
  };
  RtcDateTime timestamp = {};

  assert(decodeRv1805Timestamp(registers, std::size(registers), &timestamp));
  assert(timestamp.year == 2024);
  assert(timestamp.month == 2);
  assert(timestamp.day == 29);
  assert(timestamp.hour == 12);
  assert(timestamp.minute == 53);
  assert(timestamp.second == 44);
  assert(timestamp.hundredths == 3);
}

// Verifies that incomplete or missing RTC register data is rejected.
void testShortAndMissingReadsAreRejected() {
  const uint8_t registers[RV1805_TIMESTAMP_REGISTER_COUNT] = {
      0x75, 0x55, 0x20, 0x18, 0x30, 0x61, 0xFF, 0xFF
  };
  RtcDateTime timestamp = {};

  assert(!decodeRv1805Timestamp(registers, 6, &timestamp));
  assert(!decodeRv1805Timestamp(registers, std::size(registers), &timestamp));
}

// Verifies that an observed corrupt RTC register burst is rejected.
void testObservedCorruptBurstIsRejected() {
  const uint8_t registers[RV1805_TIMESTAMP_REGISTER_COUNT] = {
      0x79, 0x40, 0x65, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
  };
  RtcDateTime timestamp = {};

  assert(!decodeRv1805Timestamp(registers, std::size(registers), &timestamp));
}

// Verifies that invalid BCD values and impossible calendar dates are rejected.
void testInvalidBcdAndCalendarDatesAreRejected() {
  RtcDateTime timestamp = {};
  uint8_t registers[RV1805_TIMESTAMP_REGISTER_COUNT] = {
      0x00, 0x00, 0x00, 0x00, 0x29, 0x02, 0x25, 0x00
  };
  assert(!decodeRv1805Timestamp(registers, std::size(registers), &timestamp));

  registers[6] = 0x24;
  assert(decodeRv1805Timestamp(registers, std::size(registers), &timestamp));

  registers[2] = 0x5A;
  assert(!decodeRv1805Timestamp(registers, std::size(registers), &timestamp));
}

} // namespace

// Runs all RTC timestamp unit tests and reports success through the process exit code.
int main() {
  testValidTimestamp();
  testGeneralPurposeBitsAreIgnored();
  testShortAndMissingReadsAreRejected();
  testObservedCorruptBurstIsRejected();
  testInvalidBcdAndCalendarDatesAreRejected();
  return 0;
}
