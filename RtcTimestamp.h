/*
 * Copyright (c) 2026. Andrew Kevin Bailey
 * This code, firmware, and software is released under the MIT License (http://opensource.org/licenses/MIT).
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

constexpr uint8_t RV1805_TIMESTAMP_REGISTER_COUNT = 8;

struct RtcDateTime {
  uint16_t year = 0;
  uint8_t month = 0;
  uint8_t day = 0;
  uint8_t hour = 0;
  uint8_t minute = 0;
  uint8_t second = 0;
  uint8_t hundredths = 0;
};

// Decode one complete RV-1805 time-register burst. General-purpose bits which
// share the time registers are masked according to the RV-1805 register map.
bool decodeRv1805Timestamp(const uint8_t* registers,
                           size_t registerCount,
                           RtcDateTime* timestamp);
