/*
 * Copyright (c) 2026. Andrew Kevin Bailey
 * This code, firmware, and software is released under the MIT License (http://opensource.org/licenses/MIT).
 */

// ReSharper disable CppUnusedIncludeDirective
#pragma once

#include <cstddef>
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

// Decodes one complete RV-1805 time-register burst while masking shared general-purpose bits.
bool decodeRv1805Timestamp(const uint8_t* registers,
                           std::size_t registerCount,
                           RtcDateTime* timestamp);
