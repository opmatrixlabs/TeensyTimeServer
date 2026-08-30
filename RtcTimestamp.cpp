/*
 * Copyright (c) 2026. Andrew Kevin Bailey
 * This code, firmware, and software is released under the MIT License (http://opensource.org/licenses/MIT).
 */

#include "RtcTimestamp.h"

namespace {

constexpr uint8_t HUNDREDTHS_INDEX = 0;
constexpr uint8_t SECONDS_INDEX = 1;
constexpr uint8_t MINUTES_INDEX = 2;
constexpr uint8_t HOURS_INDEX = 3;
constexpr uint8_t DATE_INDEX = 4;
constexpr uint8_t MONTH_INDEX = 5;
constexpr uint8_t YEAR_INDEX = 6;

bool decodeBcdInRange(const uint8_t raw,
                      const uint8_t minimum,
                      const uint8_t maximum,
                      uint8_t* value) {
  if (value == nullptr)
    return false;

  const uint8_t tens = static_cast<uint8_t>(raw >> 4);
  const uint8_t ones = static_cast<uint8_t>(raw & 0x0F);
  if (tens > 9 || ones > 9)
    return false;

  const uint8_t decoded = static_cast<uint8_t>(tens * 10 + ones);
  if (decoded < minimum || decoded > maximum)
    return false;

  *value = decoded;
  return true;
}

bool isLeapYear(const uint16_t year) {
  return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

uint8_t daysInMonth(const uint8_t month, const uint16_t year) {
  switch (month) {
    case 1: case 3: case 5: case 7: case 8: case 10: case 12:
      return 31;
    case 4: case 6: case 9: case 11:
      return 30;
    case 2:
      return isLeapYear(year) ? 29 : 28;
    default:
      return 0;
  }
}

} // namespace

bool decodeRv1805Timestamp(const uint8_t* registers,
                           const std::size_t registerCount,
                           RtcDateTime* timestamp) {
  if (registers == nullptr || timestamp == nullptr ||
      registerCount != RV1805_TIMESTAMP_REGISTER_COUNT)
    return false;

  RtcDateTime decoded = {};
  uint8_t twoDigitYear = 0;

  // Seconds, minutes, hours, date, and month contain general-purpose bits
  // above their BCD time fields. The clock is configured for 24-hour mode.
  if (!decodeBcdInRange(registers[HUNDREDTHS_INDEX], 0, 99, &decoded.hundredths) ||
      !decodeBcdInRange(registers[SECONDS_INDEX] & 0x7F, 0, 59, &decoded.second) ||
      !decodeBcdInRange(registers[MINUTES_INDEX] & 0x7F, 0, 59, &decoded.minute) ||
      !decodeBcdInRange(registers[HOURS_INDEX] & 0x3F, 0, 23, &decoded.hour) ||
      !decodeBcdInRange(registers[DATE_INDEX] & 0x3F, 1, 31, &decoded.day) ||
      !decodeBcdInRange(registers[MONTH_INDEX] & 0x1F, 1, 12, &decoded.month) ||
      !decodeBcdInRange(registers[YEAR_INDEX], 0, 99, &twoDigitYear))
    return false;

  decoded.year = static_cast<uint16_t>(2000 + twoDigitYear);
  if (decoded.day > daysInMonth(decoded.month, decoded.year))
    return false;

  *timestamp = decoded;
  return true;
}
