/*
 * Copyright (c) 2026. Andrew Kevin Bailey
 * This code, firmware, and software is released under the MIT License (http://opensource.org/licenses/MIT).
 */

// ReSharper disable CppUnusedIncludeDirective
#pragma once

#include <Arduino.h>

#include "FirmwareImageValidator.h"
#include "IntelHexParser.h"

enum class FirmwareUpdateFailure : uint8_t {
  None,
  InvalidUpload,
  FlashStorage,
  ServerState
};

// Streams a plain Teensy Intel HEX image into a fixed, non-executable staging
// bank. installAndReboot() is available only after every image check succeeds.
class FirmwareUpdater {
public:
  FirmwareUpdater();

  bool begin(uint32_t encodedLength);
  bool write(const uint8_t* data, std::size_t length);
  bool finish();
  void abort();

  const char* error() const;
  FirmwareUpdateFailure failure() const;
  uint32_t imageSize() const;
  uint32_t recordCount() const;
  bool readyToInstall() const;

  static constexpr uint32_t maxUploadBytes() {
    return (PROGRAM_FLASH_END - STAGING_BASE) * 4U;
  }

  [[noreturn]] void installAndReboot();

private:
  static constexpr uint32_t FLASH_BASE = 0x60000000UL;
  static constexpr uint32_t STAGING_BASE = 0x60800000UL;
  static constexpr uint32_t PROGRAM_FLASH_END = 0x60FC0000UL;
  static constexpr uint32_t FLASH_SECTOR_SIZE = 0x1000UL;
  static constexpr uint32_t FLASH_PAGE_SIZE = 0x100UL;
  static constexpr uint32_t STAGING_CAPACITY = PROGRAM_FLASH_END - STAGING_BASE;

  IntelHexParser parser_;
  uint32_t extendedAddress_ = 0;
  uint32_t expectedAddress_ = FLASH_BASE;
  uint32_t erasedThrough_ = STAGING_BASE;
  uint32_t imageSize_ = 0;
  uint32_t recordCount_ = 0;
  bool active_ = false;
  bool sawData_ = false;
  bool sawStartAddress_ = false;
  bool sawEndOfFile_ = false;
  bool readyToInstall_ = false;
  const char* error_ = "";
  FirmwareUpdateFailure failure_ = FirmwareUpdateFailure::None;

  bool processRecord(const IntelHexRecord& record);
  bool stageData(uint32_t imageAddress, const uint8_t* data, uint32_t length);
  bool eraseStagingThrough(uint32_t exclusiveEnd);
  bool validateImage();
  bool fail(FirmwareUpdateFailure failure, const char* message);
};
