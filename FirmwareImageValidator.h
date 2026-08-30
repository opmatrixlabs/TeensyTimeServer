/*
 * Copyright (c) 2026. Andrew Kevin Bailey
 * This code, firmware, and software is released under the MIT License (http://opensource.org/licenses/MIT).
 */

#pragma once

 // ReSharper disable CppUnusedIncludeDirective
#include <cstdint>

struct FirmwareImageValidation {
  bool valid;
  const char* error;
};

FirmwareImageValidation validateTeensyTimeServerImage(const volatile uint8_t* image,
                                                      uint32_t imageSize);
