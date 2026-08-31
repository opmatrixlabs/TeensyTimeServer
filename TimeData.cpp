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

#include "TimeData.h"

// Constructs a TimeData object with its default field values.
TimeData::TimeData() 
= default;

// Constructs a TimeData object from the supplied calendar time and nanoseconds.
TimeData::TimeData(uint16_t y, uint8_t m, uint8_t d, uint8_t h, uint8_t min, uint8_t s, int32_t ss) {
	setYear(y);
  setMonth(m);
  setDay(d);
  setHour(h);
  setMin(min);
  setSec(s);
  setSubSec(ss);
}

// Destroys the TimeData object.
TimeData::~TimeData()
= default;

// Sets the total leap-second count from a GPS-era leap-second value.
void TimeData::setLeapSecondsSince1980(int8_t gpsLeapSeconds) {
	totalLeapSeconds_ = gpsLeapSeconds + LEAP_SECONDS_1980;
}

// Sets the total leap-second count to the configured 2025-era value.
void TimeData::setLeapSecondsSince2025() {
  totalLeapSeconds_ = LEAP_SECONDS_2025;
}

// Returns the total number of leap seconds since 1900.
int8_t TimeData::getTotalLeapSeconds() {
	return totalLeapSeconds_;
}

// Sets the year when the supplied value is valid.
void TimeData::setYear(uint16_t y) {
  if (y > 0) year_ = y;
}

// Sets the month when the supplied value is valid.
void TimeData::setMonth(uint8_t m) {
  if (m > 0 && m <= 12) month_ = m;
}

// Sets the day when it is valid for the current month and year.
void TimeData::setDay(uint8_t d) {
  if (d > 0 && d <= daysInMonth(month_, year_)) day_ = d;
}

// Sets the hour when the supplied value is valid.
void TimeData::setHour(uint8_t h) {
  if (h >= 0 && h < 24) hour_ = h;
}

// Sets the minute when the supplied value is valid.
void TimeData::setMin(uint8_t m) {
  if (m >= 0 && m < 60) min_ = m;
}

// Sets the second when the supplied value is valid.
void TimeData::setSec(uint8_t s) {
  if (s >= 0 && s < 60) sec_ = s;
}

// Sets the nanosecond fraction when the supplied value is nonnegative.
void TimeData::setSubSec(int32_t ss) {
  if (ss >= 0) subSec_ = ss;
}

// Returns the stored year.
uint16_t TimeData::getYear() {
  return year_;
}

// Returns the stored month.
uint8_t TimeData::getMonth() {
  return month_;
}

// Returns the stored day of the month.
uint8_t TimeData::getDay() {
  return day_;
}

// Returns the stored hour.
uint8_t TimeData::getHour() {
  return hour_;
}

// Returns the stored minute.
uint8_t TimeData::getMin() {
  return min_;
}

// Returns the stored second.
uint8_t TimeData::getSec() {
  return sec_;
}

// Returns the stored nanosecond fraction.
int32_t TimeData::getSubSec() {
  return subSec_;
}

// Formats the stored time as an ISO 8601 timestamp at the requested precision.
String TimeData::getISO8601Time(uint8_t decimalPrecision) {
  char strISO8601Time[64] = {0};
  char strNanoseconds[10] = {0};
  const uint8_t precision = decimalPrecision > 9 ? 9 : decimalPrecision;

  // subSec_ is nanoseconds, so its decimal representation always has a
  // nine-digit scale. Preserve leading zeroes before truncating precision.
  snprintf(strNanoseconds, sizeof(strNanoseconds), "%09lu", static_cast<unsigned long>(subSec_));
  strNanoseconds[precision] = '\0';

  sprintf(strISO8601Time, "%04u-%02u-%02uT%02u:%02u:%02u.%s", year_, month_, day_, hour_, min_, sec_, strNanoseconds);
  return String(strISO8601Time);
}

// Formats the supplied calendar fields as an ISO 8601 timestamp.
String TimeData::toISO8601Time(uint16_t y, uint8_t m, uint8_t d, uint8_t h, uint8_t min, uint8_t s, int32_t ss, uint8_t decimalPrecision) {
  char strISO8601Time[64] = {0};
  String strDecimal = String(ss);

  // Take only the first 3 digits (truncate if longer, pad if shorter)
  if (strDecimal.length() > decimalPrecision) {
    strDecimal = strDecimal.substring(0, decimalPrecision);
  } 
  else {
    for (long i = 0; i < long(decimalPrecision - strDecimal.length()); i++) {
      strDecimal = '0' + strDecimal; 
    }
  }

  sprintf(strISO8601Time, "%04u-%02u-%02uT%02u:%02u:%02u.%s", y, m, d, h, min, s, strDecimal.c_str());
  return String(strISO8601Time);
}

// Applies a nanosecond correction and carries a negative correction into the prior second.
void TimeData::calculateCorrectedTime(const int32_t nanoCorrection) {
    if (nanoCorrection < 0) {
        subSec_ = 1000000000 + nanoCorrection; // adding a negative value to nanos in a second
        if (static_cast<int>(sec_) - 1 < 0) {
            sec_ = 59;
            if (static_cast<int>(min_) - 1 < 0) {
                min_ = 59;
                if (static_cast<int>(hour_) - 1 < 0) {
                    hour_ = 23;
                    if (static_cast<int>(day_) - 1 < 1) {
                        // We have to move to the previous month to get the last day
                        if (static_cast<int>(month_) - 1 > 0) {
                            day_ = daysInMonth(static_cast<int>(month_) - 1, year_);
                            month_ -= 1;
                        }
                        else {
                            year_ -= 1;
                            month_ = 12;
                            day_ = 31;
                        }
                    }
                    else {
                      day_ -= 1;
                    }
                }
                else {
                  hour_ -= 1;
                }
            }
            else {
              min_ -= 1;
            }
        }
        else { 
          sec_ -= 1;
        }
    }
    else {
        subSec_ = nanoCorrection;
    }
}

// Reports whether the supplied year is a Gregorian leap year.
bool TimeData::isLeapYear(const uint16_t year) {
    return ((year % 4 == 0) && ((year % 100 != 0) || (year % 400 == 0)));
}

// Returns the number of days in the supplied month and year.
uint8_t TimeData::daysInMonth(const uint8_t month, const uint16_t year) {
    uint8_t daysInMonth = 0;
    switch (month) {
    case 1: case 3: case 5: case 7: case 8: case 10: case 12:
        daysInMonth = 31;
        break;
    case 4: case 6: case 9: case 11:
        daysInMonth = 30;
        break;
    case 2:
        if (isLeapYear(year))
            daysInMonth = 29; // Leap year
        else
            daysInMonth = 28;
        break;
    default:
        return -1;
    }
    return daysInMonth;
}

// Converts the stored calendar time into whole seconds since 1900-01-01.
uint64_t TimeData::secondsSince1900() {
     // Calculate total days since 1900-01-01
    int32_t year = year_;
    int32_t month = month_;
    int32_t day = day_;

    // Calculate days for all previous years
    uint64_t days = 0;
    for(int y = 1900; y < year; y++) {
        days += isLeapYear(y) ? 366 : 365;
    }
    // Days for previous months of current year
    for(int m = 1; m < month; m++) {
        days += daysInMonth(m, year);
    }
    // Days in current month (subtract 1 since current day not completed)
    days += (day - 1);

    // Calculate total seconds
    uint64_t seconds = days * 86400ULL +
                       hour_ * 3600ULL +
                       min_ * 60ULL +
                       sec_;
    return seconds;
}

// Populates the stored calendar fields from whole seconds since 1900-01-01.
bool TimeData::setSecondsSince1900(uint64_t seconds) {
  uint64_t days = seconds / 86400ULL;
  uint32_t secondsOfDay = static_cast<uint32_t>(seconds % 86400ULL);
  uint16_t year = 1900;

  // TimeData stores a four-digit year. Reject values which cannot be
  // represented instead of allowing the year counter to wrap.
  while (year < 9999) {
    const uint16_t daysInYear = isLeapYear(year) ? 366 : 365;
    if (days < daysInYear)
      break;
    days -= daysInYear;
    ++year;
  }
  if (year >= 9999)
    return false;

  uint8_t month = 1;
  while (month <= 12) {
    const uint8_t monthDays = daysInMonth(month, year);
    if (days < monthDays)
      break;
    days -= monthDays;
    ++month;
  }
  if (month > 12)
    return false;

  year_ = year;
  month_ = month;
  day_ = static_cast<uint8_t>(days + 1);
  hour_ = static_cast<uint8_t>(secondsOfDay / 3600U);
  secondsOfDay %= 3600U;
  min_ = static_cast<uint8_t>(secondsOfDay / 60U);
  sec_ = static_cast<uint8_t>(secondsOfDay % 60U);
  subSec_ = 0;
  return true;
}

// Records whether the stored GPS time is valid.
void TimeData::validGpsTime(bool valid) {
  isValidGpsTime_ = valid;
}

// Reports whether the stored GPS time is valid.
bool TimeData::isValidGpsTime() {
  return isValidGpsTime_;
}
