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

#include <Arduino.h>

/*
Leap seconds date list:
1972-06-30, 1972-12-31, 1973-12-31, 1974-12-31, 1975-12-31, 1976-12-31, 1977-12-31,
1978-12-31, 1979-12-31, 1981-06-30, 1982-06-30, 1983-06-30, 1985-06-30, 1987-12-31,
1989-12-31, 1990-06-30, 1992-06-30, 1993-06-30, 1994-06-30, 1995-12-31, 1997-06-30,
1998-12-31, 2005-12-31, 2008-12-31, 2012-06-30, 2015-06-30, 2016-12-31
*/

// Number of leap seconds before January 6, 1980.
#define LEAP_SECONDS_1980 9
// Number of leap seconds before January 6, 2025.
#define LEAP_SECONDS_2025 27

class TimeData {
public:
  TimeData();
  TimeData(uint16_t y, uint8_t m, uint8_t d, uint8_t h, uint8_t min, uint8_t s, int32_t ss);
  ~TimeData();

  void setYear(uint16_t y);
  void setMonth(uint8_t m);
  void setDay(uint8_t d);
  void setHour(uint8_t h);
  void setMin(uint8_t m);
  void setSec(uint8_t s);
  void setSubSec(int32_t ss);
	uint16_t getYear();
  uint8_t getMonth();
  uint8_t getDay();
  uint8_t getHour();
  uint8_t getMin();
  uint8_t getSec();
  int32_t getSubSec();
  void getTimeString(char* timeString);

  void calculateCorrectedTime(int32_t nanoCorrection);
  static uint8_t daysInMonth(uint8_t month, uint16_t year); // January = 1, December = 12
	uint64_t secondsSince1900();
	int8_t getTotalLeapSeconds();
	// Get the number of leap seconds since January 6 1980 from GPS (UBX-NAV-TIMELS)
	void setLeapSecondsSince1980(int8_t gpsLeapSeconds);
  void setLeapSecondsSince2025();
  bool isValidGpsTime();
  void validGpsTime(bool valid);
  String getISO8601Time(uint8_t decimalPrecision);
  static String toISO8601Time(uint16_t y, uint8_t m, uint8_t d, uint8_t h, uint8_t min, uint8_t s, int32_t ss, uint8_t decimalPrecision);

private:
  static bool isLeapYear(uint16_t year);

  int8_t totalLeapSeconds_ = 0;
  bool isValidGpsTime_ = false;
  
  uint16_t year_ = 0; // Year (UTC)
  uint8_t month_ = 0; // Month, range 1..12 (UTC)
  uint8_t day_ = 0; // Day of month, range 1..31 (UTC)
  uint8_t hour_ = 0; // Hour of day, range 0..23 (UTC)
  uint8_t min_ = 0; // Minute of hour, range 0..59 (UTC)
  uint8_t sec_ = 0; // Seconds of minute, range 0..60 (UTC)
  int32_t subSec_ = 0; // nanoseconds
};
