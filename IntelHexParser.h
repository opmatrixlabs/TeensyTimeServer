/*
 * Copyright (c) 2026. Andrew Kevin Bailey
 * This code, firmware, and software is released under the MIT License (http://opensource.org/licenses/MIT).
 */

#pragma once

// ReSharper disable CppUnusedIncludeDirective

#include <cstddef>
#include <cstdint>

struct IntelHexRecord {
  uint8_t byteCount;
  uint16_t address;
  uint8_t type;
  uint8_t data[255];
};

enum class IntelHexParseResult : uint8_t {
  NeedMoreData,
  RecordReady,
  Error
};

// Incremental Intel HEX parser. It keeps at most one record in RAM and is
// independent of Arduino so the upload parser can be host tested.
class IntelHexParser {
public:
  IntelHexParser();

  void reset();
  IntelHexParseResult consume(uint8_t value, IntelHexRecord* record);
  IntelHexParseResult finish(IntelHexRecord* record);
  const char* error() const;
  bool sawEndOfFile() const;

private:
  static constexpr std::size_t MAX_LINE_LENGTH = 521;

  char line_[MAX_LINE_LENGTH + 1];
  std::size_t lineLength_ = 0;
  bool sawEndOfFile_ = false;
  bool failed_ = false;
  const char* error_ = "";

  IntelHexParseResult parseLine(IntelHexRecord* record);
  IntelHexParseResult fail(const char* message);
};
