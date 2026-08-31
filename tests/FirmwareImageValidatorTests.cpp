/*
 * Copyright (c) 2026. Andrew Kevin Bailey
 * This code, firmware, and software is released under the MIT License (http://opensource.org/licenses/MIT).
 */

#include "../FirmwareImageValidator.h"
#include "../IntelHexParser.h"

#include <assert.h>
#include <fstream>
#include <iostream>
#include <string.h>
#include <vector>

namespace {

constexpr uint32_t FLASH_BASE = 0x60000000UL;

// Writes a 32-bit value into the synthetic firmware image in little-endian byte order.
void writeWord(std::vector<uint8_t>& image, const size_t offset, const uint32_t value) {
  image[offset] = static_cast<uint8_t>(value);
  image[offset + 1] = static_cast<uint8_t>(value >> 8);
  image[offset + 2] = static_cast<uint8_t>(value >> 16);
  image[offset + 3] = static_cast<uint8_t>(value >> 24);
}

// Copies a null-terminated text value into the synthetic firmware image at the specified offset.
void writeText(std::vector<uint8_t>& image, const size_t offset, const char* value) {
  memcpy(image.data() + offset, value, strlen(value));
}

// Constructs a synthetic Teensy firmware image containing valid metadata and vector entries.
std::vector<uint8_t> validSyntheticImage() {
  std::vector<uint8_t> image(0x2400, 0);
  writeWord(image, 0x0000, 0x42464346UL);
  writeWord(image, 0x0050, 0x01000000UL);
  writeWord(image, 0x1000, 0x432000D1UL);
  writeWord(image, 0x1004, FLASH_BASE + 0x1501UL);
  writeWord(image, 0x1010, FLASH_BASE + 0x1020UL);
  writeWord(image, 0x1014, FLASH_BASE + 0x1000UL);
  writeWord(image, 0x1018, FLASH_BASE + static_cast<uint32_t>(image.size()) - 0xC00UL);
  writeWord(image, 0x1020, FLASH_BASE);
  writeWord(image, 0x1024, static_cast<uint32_t>(image.size()));
  writeText(image, 0x1100, "fw_teensyMM");
  writeText(image, 0x1120, "TeensyTimeServerFirmware");
  return image;
}

// Verifies acceptance of valid firmware metadata and rejection of each tested corruption.
void testValidAndInvalidMetadata() {
  const std::vector<uint8_t> valid = validSyntheticImage();
  assert(validateTeensyTimeServerImage(valid.data(), static_cast<uint32_t>(valid.size())).valid);

  std::vector<uint8_t> changed = valid;
  changed[0] ^= 1;
  assert(!validateTeensyTimeServerImage(changed.data(), static_cast<uint32_t>(changed.size())).valid);

  changed = valid;
  writeWord(changed, 0x0050, 0x00800000UL);
  assert(!validateTeensyTimeServerImage(changed.data(), static_cast<uint32_t>(changed.size())).valid);

  changed = valid;
  changed[0x1004] &= 0xFE;
  assert(!validateTeensyTimeServerImage(changed.data(), static_cast<uint32_t>(changed.size())).valid);

  changed = valid;
  writeWord(changed, 0x1004, FLASH_BASE + 0x1201UL);
  assert(!validateTeensyTimeServerImage(changed.data(), static_cast<uint32_t>(changed.size())).valid);

  changed = valid;
  writeWord(changed, 0x1004, FLASH_BASE + 0x1801UL);
  assert(!validateTeensyTimeServerImage(changed.data(), static_cast<uint32_t>(changed.size())).valid);

  changed = valid;
  writeWord(changed, 0x1024, static_cast<uint32_t>(changed.size() - 4));
  assert(!validateTeensyTimeServerImage(changed.data(), static_cast<uint32_t>(changed.size())).valid);

  changed = valid;
  changed[0x1100] = 'x';
  assert(!validateTeensyTimeServerImage(changed.data(), static_cast<uint32_t>(changed.size())).valid);

  changed = valid;
  changed[0x1120] = 'x';
  assert(!validateTeensyTimeServerImage(changed.data(), static_cast<uint32_t>(changed.size())).valid);
}

// Applies one parsed Intel HEX record to the reconstructed firmware image and tracking state.
bool appendHexRecord(const IntelHexRecord& record,
                     uint32_t* extendedAddress,
                     uint32_t* expectedAddress,
                     bool* sawStart,
                     bool* sawEof,
                     std::vector<uint8_t>* image) {
  switch (record.type) {
    case 0x00: {
      if (*sawStart || record.byteCount == 0 || (record.byteCount & 3U) != 0 || (record.address & 3U) != 0)
        return false;
      const uint32_t address = *extendedAddress + record.address;
      if (address != *expectedAddress)
        return false;
      image->insert(image->end(), record.data, record.data + record.byteCount);
      *expectedAddress += record.byteCount;
      return true;
    }
    case 0x01:
      if (record.byteCount != 0 || record.address != 0 || !*sawStart)
        return false;
      *sawEof = true;
      return true;
    case 0x04:
      if (*sawStart || record.byteCount != 2 || record.address != 0)
        return false;
      *extendedAddress = (static_cast<uint32_t>(record.data[0]) << 24) |
                         (static_cast<uint32_t>(record.data[1]) << 16);
      return true;
    case 0x05:
      if (*sawStart || record.byteCount != 4 || record.address != 0 ||
          record.data[0] != 0x60 || record.data[1] != 0 || record.data[2] != 0x10 || record.data[3] != 0)
        return false;
      *sawStart = true;
      return true;
    default:
      return false;
  }
}

// Parses and validates a generated Intel HEX firmware artifact from disk.
void validateArtifact(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.good());

  IntelHexParser parser;
  IntelHexRecord record = {};
  std::vector<uint8_t> image;
  uint32_t extendedAddress = 0;
  uint32_t expectedAddress = FLASH_BASE;
  bool sawStart = false;
  bool sawEof = false;
  char value = 0;
  while (input.get(value)) {
    const IntelHexParseResult result = parser.consume(static_cast<uint8_t>(value), &record);
    assert(result != IntelHexParseResult::Error);
    if (result == IntelHexParseResult::RecordReady)
      assert(appendHexRecord(record, &extendedAddress, &expectedAddress, &sawStart, &sawEof, &image));
  }
  const IntelHexParseResult finalResult = parser.finish(&record);
  assert(finalResult != IntelHexParseResult::Error);
  if (finalResult == IntelHexParseResult::RecordReady)
    assert(appendHexRecord(record, &extendedAddress, &expectedAddress, &sawStart, &sawEof, &image));
  assert(sawEof && parser.sawEndOfFile());

  const FirmwareImageValidation validation = validateTeensyTimeServerImage(
      image.data(), static_cast<uint32_t>(image.size()));
  if (!validation.valid)
    std::cerr << validation.error << '\n';
  assert(validation.valid);
  std::cout << "Validated generated firmware image: " << image.size() << " bytes\n";
}

} // namespace

// Runs firmware image validation tests and optionally validates a supplied build artifact.
int main(const int argc, char** argv) {
  testValidAndInvalidMetadata();
  if (argc == 2)
    validateArtifact(argv[1]);
  return 0;
}
