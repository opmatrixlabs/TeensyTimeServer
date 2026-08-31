/*
 * Copyright (c) 2026. Andrew Kevin Bailey
 * This code, firmware, and software is released under the MIT License (http://opensource.org/licenses/MIT).
 *
 * The final RAM-resident flash replacement follows the approach used by
 * FlasherX (public domain), originally by Niels A. Moseley and modified for
 * Teensy 4.x by Jon Zeeff, Deb Hollenback, Paul Stoffregen, and Joe
 * Pasquariello. The implementation here uses the Teensy 1.62 MicroMod linker
 * limit (0x60FC0000), not FlasherX's obsolete 16 KB reserve.
 */

#include "FirmwareUpdater.h"

#include <algorithm>

#if !defined(__IMXRT1062__) || !defined(ARDUINO_TEENSY_MICROMOD)
#error FirmwareUpdater supports only the Teensy MicroMod target
#endif

extern "C" {
extern unsigned long flashImageLengthSymbol __asm__("_flashimagelen");
void eepromemu_flash_write(void* address, const void* data, uint32_t length);
void eepromemu_flash_erase_sector(void* address);
}

namespace {

// These strings are deliberately part of every OTA-capable image. They reject
// firmware built for another Teensy target or an unrelated MicroMod project.
const volatile char firmwareTargetId[] PROGMEM __attribute__((used)) = "fw_teensyMM";
const volatile char firmwareApplicationId[] PROGMEM __attribute__((used)) = "TeensyTimeServerFirmware";

constexpr uint32_t FLASH_BASE_ADDRESS = 0x60000000UL;
constexpr uint32_t STAGING_BASE_ADDRESS = 0x60800000UL;
constexpr uint32_t PROGRAM_FLASH_END_ADDRESS = 0x60FC0000UL;
constexpr uint32_t SECTOR_SIZE = 0x1000UL;
constexpr uint32_t PAGE_SIZE = 0x100UL;

#define FIRMWARE_RAM_FUNCTION __attribute__((section(".fastrun"), noinline, noclone, optimize("Os")))

// Maps a FlexSPI flash address to a typed memory-mapped pointer.
template <typename T>
__attribute__((always_inline)) inline T* memoryMappedFlashPointer(const uint32_t address) {
  // Form the pointer from the fixed hardware base, then retain it while
  // selecting an address within the FlexSPI memory-mapped flash window.
  auto* const flashBase = reinterpret_cast<uint8_t*>(0x60000000UL);
  return reinterpret_cast<T*>(flashBase + (address - FLASH_BASE_ADDRESS));
}

// Reports whether the flash sector containing the address has programmed data.
FIRMWARE_RAM_FUNCTION bool firmwareFlashSectorNotErased(const uint32_t address) {
  const volatile uint32_t* word = memoryMappedFlashPointer<const volatile uint32_t>(
      address & ~(SECTOR_SIZE - 1U));
  for (uint32_t index = 0; index < SECTOR_SIZE / sizeof(uint32_t); ++index) {
    if (word[index] != 0xFFFFFFFFUL)
      return true;
  }
  return false;
}

// This operation is intentionally non-atomic. The web UI warns that removing
// power during this final phase can require USB/BOOT-button recovery.
// Replaces the running firmware from staging, cleans obsolete sectors, and reboots.
[[noreturn]] FIRMWARE_RAM_FUNCTION void replaceFirmwareAndReboot(const uint32_t imageSize) {
  // BASEPRI remains set even though the Teensy flash primitives briefly clear
  // PRIMASK internally. This prevents ordinary peripheral/timer interrupts
  // from running after their flash-resident load image starts changing.
  const uint32_t basePriorityMask = 16;
  __asm__ volatile("msr basepri, %0" : : "r"(basePriorityMask) : "memory");
  __disable_irq();

  uint32_t copyBuffer[PAGE_SIZE / sizeof(uint32_t)];
  uint32_t sectorOffset = 0;
  while (sectorOffset < imageSize) {
    const uint32_t sectorBytes = std::min<uint32_t>(imageSize - sectorOffset, SECTOR_SIZE);

    bool sectorVerified = false;
    for (uint8_t attempt = 0; attempt < 2 && !sectorVerified; ++attempt) {
      const uint32_t destinationSector = FLASH_BASE_ADDRESS + sectorOffset;
      if (firmwareFlashSectorNotErased(destinationSector)) {
        eepromemu_flash_erase_sector(memoryMappedFlashPointer<void>(destinationSector));
        __disable_irq();
      }

      sectorVerified = true;
      uint32_t pageOffset = 0;
      while (pageOffset < sectorBytes) {
        const uint32_t chunk = std::min<uint32_t>(sectorBytes - pageOffset, PAGE_SIZE);
        const uint32_t destination = destinationSector + pageOffset;
        const volatile uint32_t* source = memoryMappedFlashPointer<const volatile uint32_t>(
            STAGING_BASE_ADDRESS + sectorOffset + pageOffset);
        for (uint32_t word = 0; word < chunk / sizeof(uint32_t); ++word)
          copyBuffer[word] = source[word];

        eepromemu_flash_write(memoryMappedFlashPointer<void>(destination), copyBuffer, chunk);
        __disable_irq();
        const volatile uint32_t* programmed = memoryMappedFlashPointer<const volatile uint32_t>(destination);
        for (uint32_t word = 0; word < chunk / sizeof(uint32_t); ++word) {
          if (programmed[word] != copyBuffer[word]) {
            sectorVerified = false;
            break;
          }
        }
        if (!sectorVerified)
          break;
        pageOffset += chunk;
      }
    }

    if (!sectorVerified) {
      // The source image is still intact in staging. Enter the Teensy
      // bootloader directly instead of cleaning staging or booting a known-bad
      // destination image. USB/BOOT-button recovery can then reprogram it.
      __asm__ volatile("bkpt #251");
      for (;;) {}
    }
    sectorOffset += sectorBytes;
  }

  // Remove obsolete application and staging sectors without entering the top
  // 256 KB reserved by the MicroMod linker and EEPROM emulation.
  uint32_t cleanupAddress = FLASH_BASE_ADDRESS + ((imageSize + SECTOR_SIZE - 1U) & ~(SECTOR_SIZE - 1U));
  while (cleanupAddress < PROGRAM_FLASH_END_ADDRESS) {
    if (firmwareFlashSectorNotErased(cleanupAddress)) {
      eepromemu_flash_erase_sector(memoryMappedFlashPointer<void>(cleanupAddress));
      __disable_irq();
    }
    cleanupAddress += SECTOR_SIZE;
  }

  SCB_AIRCR = 0x05FA0004;
  for (;;) {}
}

// Reports whether every byte in a staging flash sector is erased.
bool stagingSectorIsErased(const uint32_t address) {
  const volatile uint32_t* word = memoryMappedFlashPointer<const volatile uint32_t>(address);
  for (uint32_t index = 0; index < SECTOR_SIZE / sizeof(uint32_t); ++index) {
    if (word[index] != 0xFFFFFFFFUL)
      return false;
  }
  return true;
}

// Reads a big-endian 32-bit word from a four-byte buffer.
uint32_t bigEndianWord(const uint8_t* data) {
  return (static_cast<uint32_t>(data[0]) << 24) |
         (static_cast<uint32_t>(data[1]) << 16) |
         (static_cast<uint32_t>(data[2]) << 8) |
         static_cast<uint32_t>(data[3]);
}

} // namespace

// Initializes the firmware updater and resets its Intel HEX parser.
FirmwareUpdater::FirmwareUpdater() {
  parser_.reset();
}

// Starts a new firmware upload after validating the updater and image-size constraints.
bool FirmwareUpdater::begin(const uint32_t encodedLength) {
  // Preserve a validated staged image while the browser is being redirected
  // and installation is pending. In particular, do not call fail() here: it
  // intentionally clears readyToInstall_.
  if (active_) {
    failure_ = FirmwareUpdateFailure::ServerState;
    error_ = "a firmware upload is already active";
    return false;
  }
  if (readyToInstall_) {
    failure_ = FirmwareUpdateFailure::ServerState;
    error_ = "validated firmware is already awaiting installation";
    return false;
  }

  parser_.reset();
  extendedAddress_ = 0;
  expectedAddress_ = FLASH_BASE;
  erasedThrough_ = STAGING_BASE;
  imageSize_ = 0;
  recordCount_ = 0;
  active_ = false;
  sawData_ = false;
  sawStartAddress_ = false;
  sawEndOfFile_ = false;
  readyToInstall_ = false;
  error_ = "";
  failure_ = FirmwareUpdateFailure::None;

  // Volatile reads keep both identity markers in the linked firmware image.
  const char targetProbe = firmwareTargetId[0];
  const char applicationProbe = firmwareApplicationId[0];
  (void)targetProbe;
  (void)applicationProbe;

  if (encodedLength == 0 || encodedLength > maxUploadBytes())
    return fail(FirmwareUpdateFailure::InvalidUpload, "firmware upload length is outside the supported range");

  const uintptr_t runningImageSize = reinterpret_cast<uintptr_t>(&flashImageLengthSymbol);
  if (runningImageSize == 0 || runningImageSize > STAGING_BASE - FLASH_BASE)
    return fail(FirmwareUpdateFailure::ServerState, "the running firmware overlaps the reserved staging bank");

  active_ = true;
  return true;
}

// Parses and stages a block of uploaded Intel HEX data.
bool FirmwareUpdater::write(const uint8_t* data, const std::size_t length) {
  if (!active_ || readyToInstall_)
    return fail(FirmwareUpdateFailure::ServerState, "firmware upload is not active");
  if (data == nullptr && length != 0)
    return fail(FirmwareUpdateFailure::InvalidUpload, "firmware upload buffer is missing");

  IntelHexRecord record = {};
  for (std::size_t index = 0; index < length; ++index) {
    const IntelHexParseResult result = parser_.consume(data[index], &record);
    if (result == IntelHexParseResult::Error)
      return fail(FirmwareUpdateFailure::InvalidUpload, parser_.error());
    if (result == IntelHexParseResult::RecordReady && !processRecord(record))
      return false;
  }
  return true;
}

// Finalizes parsing, validates the staged image, and marks it ready for installation.
bool FirmwareUpdater::finish() {
  if (!active_ || readyToInstall_)
    return fail(FirmwareUpdateFailure::ServerState, "firmware upload is not active");

  IntelHexRecord record = {};
  const IntelHexParseResult result = parser_.finish(&record);
  if (result == IntelHexParseResult::Error)
    return fail(FirmwareUpdateFailure::InvalidUpload, parser_.error());
  if (result == IntelHexParseResult::RecordReady && !processRecord(record))
    return false;
  if (!parser_.sawEndOfFile() || !sawEndOfFile_)
    return fail(FirmwareUpdateFailure::InvalidUpload, "Intel HEX end-of-file record is missing");

  imageSize_ = expectedAddress_ - FLASH_BASE;
  if (!validateImage())
    return false;

  active_ = false;
  readyToInstall_ = true;
  return true;
}

// Cancels the active upload and clears its parser and installation state.
void FirmwareUpdater::abort() {
  active_ = false;
  readyToInstall_ = false;
  parser_.reset();
  // Staged bytes are never executable and are erased lazily by the next upload.
  // Avoid a long blocking erase after a network disconnect or invalid file.
}

// Returns the most recent firmware-update error message.
const char* FirmwareUpdater::error() const {
  return error_;
}

// Returns the category of the most recent firmware-update failure.
FirmwareUpdateFailure FirmwareUpdater::failure() const {
  return failure_;
}

// Returns the validated firmware image size in bytes.
uint32_t FirmwareUpdater::imageSize() const {
  return imageSize_;
}

// Returns the number of firmware data records processed.
uint32_t FirmwareUpdater::recordCount() const {
  return recordCount_;
}

// Reports whether a validated staged image is ready to install.
bool FirmwareUpdater::readyToInstall() const {
  return readyToInstall_;
}

// Installs the validated staged image and reboots the processor.
[[noreturn]] void FirmwareUpdater::installAndReboot() {
  if (!readyToInstall_ || imageSize_ == 0) {
    SCB_AIRCR = 0x05FA0004;
    for (;;) {}
  }
  replaceFirmwareAndReboot(imageSize_);
}

// Validates and processes one parsed Intel HEX record.
bool FirmwareUpdater::processRecord(const IntelHexRecord& record) {
  if (sawEndOfFile_)
    return fail(FirmwareUpdateFailure::InvalidUpload, "record appears after the Intel HEX end-of-file record");

  switch (record.type) {
    case 0x00: {
      if (sawStartAddress_)
        return fail(FirmwareUpdateFailure::InvalidUpload, "firmware data appears after the start-address record");
      if (record.byteCount == 0 || (record.address & 3U) != 0 || (record.byteCount & 3U) != 0)
        return fail(FirmwareUpdateFailure::InvalidUpload, "firmware data records must be nonempty and four-byte aligned");

      const uint64_t address = static_cast<uint64_t>(extendedAddress_) + record.address;
      const uint64_t end = address + record.byteCount;
      if (address != expectedAddress_)
        return fail(FirmwareUpdateFailure::InvalidUpload, "firmware data is not contiguous and strictly ordered");
      if (address < FLASH_BASE || end > static_cast<uint64_t>(FLASH_BASE) + STAGING_CAPACITY || end > PROGRAM_FLASH_END)
        return fail(FirmwareUpdateFailure::InvalidUpload, "firmware data is outside the safe MicroMod application range");
      if (!stageData(static_cast<uint32_t>(address), record.data, record.byteCount))
        return false;

      expectedAddress_ = static_cast<uint32_t>(end);
      sawData_ = true;
      ++recordCount_;
      return true;
    }

    case 0x01:
      if (record.byteCount != 0 || record.address != 0)
        return fail(FirmwareUpdateFailure::InvalidUpload, "Intel HEX end-of-file record is malformed");
      if (!sawData_ || !sawStartAddress_)
        return fail(FirmwareUpdateFailure::InvalidUpload, "firmware data or start-address record is missing");
      sawEndOfFile_ = true;
      return true;

    case 0x04:
      if (sawStartAddress_ || record.byteCount != 2 || record.address != 0)
        return fail(FirmwareUpdateFailure::InvalidUpload, "Intel HEX extended-address record is malformed or out of order");
      extendedAddress_ = (static_cast<uint32_t>(record.data[0]) << 24) |
                         (static_cast<uint32_t>(record.data[1]) << 16);
      return true;

    case 0x05:
      if (sawStartAddress_ || !sawData_ || record.byteCount != 4 || record.address != 0)
        return fail(FirmwareUpdateFailure::InvalidUpload, "Intel HEX start-address record is malformed or out of order");
      if (bigEndianWord(record.data) != 0x60001000UL)
        return fail(FirmwareUpdateFailure::InvalidUpload, "Intel HEX start address is not valid for Teensy MicroMod");
      sawStartAddress_ = true;
      return true;

    default:
      return fail(FirmwareUpdateFailure::InvalidUpload, "Intel HEX record type is not supported");
  }
}

// Writes one contiguous firmware data record into staging flash and verifies it.
bool FirmwareUpdater::stageData(const uint32_t imageAddress, const uint8_t* data, const uint32_t length) {
  const uint32_t imageOffset = imageAddress - FLASH_BASE;
  const uint32_t stagingAddress = STAGING_BASE + imageOffset;
  const uint32_t stagingEnd = stagingAddress + length;
  if (stagingAddress < STAGING_BASE || stagingEnd < stagingAddress || stagingEnd > PROGRAM_FLASH_END)
    return fail(FirmwareUpdateFailure::InvalidUpload, "firmware image exceeds the staging bank");
  if (!eraseStagingThrough(stagingEnd))
    return false;

  uint32_t written = 0;
  while (written < length) {
    const uint32_t address = stagingAddress + written;
    const uint32_t pageRemaining = FLASH_PAGE_SIZE - (address & (FLASH_PAGE_SIZE - 1U));
    const uint32_t chunk = std::min<uint32_t>(length - written, pageRemaining);
    eepromemu_flash_write(memoryMappedFlashPointer<void>(address), data + written, chunk);

    const volatile uint8_t* staged = memoryMappedFlashPointer<const volatile uint8_t>(address);
    for (uint32_t index = 0; index < chunk; ++index) {
      if (staged[index] != data[written + index])
        return fail(FirmwareUpdateFailure::FlashStorage, "firmware staging flash verification failed");
    }
    written += chunk;
  }
  return true;
}

// Erases staging sectors as needed through the requested exclusive end address.
bool FirmwareUpdater::eraseStagingThrough(const uint32_t exclusiveEnd) {
  while (erasedThrough_ < exclusiveEnd) {
    if (erasedThrough_ < STAGING_BASE || erasedThrough_ >= PROGRAM_FLASH_END)
      return fail(FirmwareUpdateFailure::ServerState, "firmware staging erase crossed its protected boundary");
    if (!stagingSectorIsErased(erasedThrough_))
      eepromemu_flash_erase_sector(memoryMappedFlashPointer<void>(erasedThrough_));
    if (!stagingSectorIsErased(erasedThrough_))
      return fail(FirmwareUpdateFailure::FlashStorage, "firmware staging sector could not be erased");
    erasedThrough_ += FLASH_SECTOR_SIZE;
  }
  return true;
}

// Validates the complete firmware image stored in staging flash.
bool FirmwareUpdater::validateImage() {
  const FirmwareImageValidation validation = validateTeensyTimeServerImage(
      memoryMappedFlashPointer<const volatile uint8_t>(STAGING_BASE), imageSize_);
  if (!validation.valid)
    return fail(FirmwareUpdateFailure::InvalidUpload, validation.error);
  return true;
}

// Records a firmware-update failure and clears active installation state.
bool FirmwareUpdater::fail(const FirmwareUpdateFailure failure, const char* message) {
  active_ = false;
  readyToInstall_ = false;
  failure_ = failure;
  error_ = message;
  return false;
}
