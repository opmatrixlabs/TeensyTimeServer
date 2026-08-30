/*
 * Copyright (c) 2026. Andrew Kevin Bailey
 * This code, firmware, and software is released under the MIT License (http://opensource.org/licenses/MIT).
 */

#include "FirmwareImageValidator.h"

namespace {

constexpr uint32_t FLASH_BASE = 0x60000000UL;
constexpr uint32_t MAXIMUM_IMAGE_SIZE = 0x007C0000UL;
constexpr uint32_t MINIMUM_IMAGE_SIZE = 0x00001C00UL;
constexpr uint32_t FCB_MAGIC = 0x42464346UL;
constexpr uint32_t MICROMOD_FLASH_SIZE = 0x01000000UL;
constexpr uint32_t IVT_MAGIC = 0x432000D1UL;
constexpr uint32_t IVT_OFFSET = 0x1000UL;
constexpr uint32_t EXECUTABLE_PAYLOAD_OFFSET = 0x1400UL;
constexpr uint32_t BOOT_DATA_ADDRESS = FLASH_BASE + 0x1020UL;
constexpr uint32_t CSF_SIZE = 0xC00UL;
constexpr char TARGET_MARKER[] = "fw_teensyMM";
constexpr char APPLICATION_MARKER[] = "TeensyTimeServerFirmware";

uint32_t readWord(const volatile uint8_t* image, const uint32_t offset) {
  return static_cast<uint32_t>(image[offset]) |
         (static_cast<uint32_t>(image[offset + 1]) << 8) |
         (static_cast<uint32_t>(image[offset + 2]) << 16) |
         (static_cast<uint32_t>(image[offset + 3]) << 24);
}

bool containsMarker(const volatile uint8_t* image,
                    const uint32_t imageSize,
                    const char* marker,
                    const std::size_t markerLength) {
  if (markerLength == 0 || markerLength > imageSize)
    return false;
  for (uint32_t offset = 0; offset <= imageSize - markerLength; ++offset) {
    std::size_t index = 0;
    while (index < markerLength && image[offset + index] == static_cast<uint8_t>(marker[index]))
      ++index;
    if (index == markerLength)
      return true;
  }
  return false;
}

FirmwareImageValidation invalid(const char* error) {
  return { false, error };
}

} // namespace

FirmwareImageValidation validateTeensyTimeServerImage(const volatile uint8_t* image,
                                                      const uint32_t imageSize) {
  if (image == nullptr || imageSize < MINIMUM_IMAGE_SIZE || imageSize > MAXIMUM_IMAGE_SIZE)
    return invalid("firmware image size is invalid");
  if (readWord(image, 0x0000) != FCB_MAGIC || readWord(image, 0x0050) != MICROMOD_FLASH_SIZE)
    return invalid("firmware flash configuration is not for a 16 MB Teensy MicroMod");

  const uint32_t ivtHeader = readWord(image, IVT_OFFSET + 0x00);
  const uint32_t entry = readWord(image, IVT_OFFSET + 0x04);
  const uint32_t reserved1 = readWord(image, IVT_OFFSET + 0x08);
  const uint32_t dcd = readWord(image, IVT_OFFSET + 0x0C);
  const uint32_t bootData = readWord(image, IVT_OFFSET + 0x10);
  const uint32_t self = readWord(image, IVT_OFFSET + 0x14);
  const uint32_t csf = readWord(image, IVT_OFFSET + 0x18);
  const uint32_t reserved2 = readWord(image, IVT_OFFSET + 0x1C);
  const uint32_t imageEnd = FLASH_BASE + imageSize;
  const uint32_t entryAddress = entry & ~1UL;
  if (ivtHeader != IVT_MAGIC || (entry & 1U) == 0 ||
      entryAddress < FLASH_BASE + EXECUTABLE_PAYLOAD_OFFSET || entryAddress >= csf ||
      reserved1 != 0 || dcd != 0 || bootData != BOOT_DATA_ADDRESS || self != FLASH_BASE + IVT_OFFSET ||
      reserved2 != 0 || csf != imageEnd - CSF_SIZE)
    return invalid("firmware image vector table is invalid");

  const uint32_t bootStart = readWord(image, 0x1020);
  const uint32_t bootLength = readWord(image, 0x1024);
  const uint32_t plugin = readWord(image, 0x1028);
  if (bootStart != FLASH_BASE || bootLength != imageSize || plugin != 0)
    return invalid("firmware boot data does not match the uploaded image");

  if (!containsMarker(image, imageSize, TARGET_MARKER, sizeof(TARGET_MARKER) - 1U))
    return invalid("firmware target marker for Teensy MicroMod is missing");
  if (!containsMarker(image, imageSize, APPLICATION_MARKER, sizeof(APPLICATION_MARKER) - 1U))
    return invalid("firmware application marker for TeensyTimeServer is missing");
  return { true, "" };
}
