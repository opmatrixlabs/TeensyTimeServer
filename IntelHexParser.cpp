/*
 * Copyright (c) 2026. Andrew Kevin Bailey
 * This code, firmware, and software is released under the MIT License (http://opensource.org/licenses/MIT).
 */

#include "IntelHexParser.h"

namespace {

int8_t hexDigit(const char value) {
  if (value >= '0' && value <= '9')
    return static_cast<int8_t>(value - '0');
  if (value >= 'a' && value <= 'f')
    return static_cast<int8_t>(value - 'a' + 10);
  if (value >= 'A' && value <= 'F')
    return static_cast<int8_t>(value - 'A' + 10);
  return -1;
}

bool decodeByte(const char* text, uint8_t* value) {
  const int8_t high = hexDigit(text[0]);
  const int8_t low = hexDigit(text[1]);
  if (high < 0 || low < 0)
    return false;
  *value = static_cast<uint8_t>((high << 4) | low);
  return true;
}

} // namespace

IntelHexParser::IntelHexParser() {
  reset();
}

void IntelHexParser::reset() {
  lineLength_ = 0;
  sawEndOfFile_ = false;
  failed_ = false;
  error_ = "";
}

IntelHexParseResult IntelHexParser::consume(const uint8_t value, IntelHexRecord* record) {
  if (failed_)
    return IntelHexParseResult::Error;
  if (record == nullptr)
    return fail("Intel HEX output record is missing");

  if (value == '\r' || value == '\n') {
    if (lineLength_ == 0)
      return IntelHexParseResult::NeedMoreData;
    return parseLine(record);
  }

  if (sawEndOfFile_) {
    if (value == ' ' || value == '\t')
      return IntelHexParseResult::NeedMoreData;
    return fail("data appears after the Intel HEX end-of-file record");
  }

  if (lineLength_ >= MAX_LINE_LENGTH)
    return fail("Intel HEX record is too long");
  line_[lineLength_++] = static_cast<char>(value);
  return IntelHexParseResult::NeedMoreData;
}

IntelHexParseResult IntelHexParser::finish(IntelHexRecord* record) {
  if (failed_)
    return IntelHexParseResult::Error;
  if (record == nullptr)
    return fail("Intel HEX output record is missing");
  if (lineLength_ != 0)
    return parseLine(record);
  if (!sawEndOfFile_)
    return fail("Intel HEX end-of-file record is missing");
  return IntelHexParseResult::NeedMoreData;
}

const char* IntelHexParser::error() const {
  return error_;
}

bool IntelHexParser::sawEndOfFile() const {
  return sawEndOfFile_;
}

IntelHexParseResult IntelHexParser::parseLine(IntelHexRecord* record) {
  line_[lineLength_] = '\0';
  const size_t length = lineLength_;
  lineLength_ = 0;

  if (sawEndOfFile_)
    return fail("data appears after the Intel HEX end-of-file record");
  if (length < 11 || line_[0] != ':')
    return fail("Intel HEX record has an invalid prefix or length");
  if (((length - 1) & 1U) != 0)
    return fail("Intel HEX record has an odd number of hexadecimal digits");

  uint8_t byteCount = 0;
  if (!decodeByte(line_ + 1, &byteCount))
    return fail("Intel HEX byte count is not hexadecimal");
  const size_t expectedLength = 11U + static_cast<size_t>(byteCount) * 2U;
  if (length != expectedLength)
    return fail("Intel HEX record length does not match its byte count");

  const size_t decodedLength = static_cast<size_t>(byteCount) + 5U;
  uint8_t decoded[260];
  for (size_t index = 0; index < decodedLength; ++index) {
    if (!decodeByte(line_ + 1U + index * 2U, decoded + index))
      return fail("Intel HEX record contains a non-hexadecimal digit");
  }

  uint8_t checksum = 0;
  for (size_t index = 0; index < decodedLength; ++index)
    checksum = static_cast<uint8_t>(checksum + decoded[index]);
  if (checksum != 0)
    return fail("Intel HEX record checksum is invalid");

  record->byteCount = decoded[0];
  record->address = static_cast<uint16_t>((static_cast<uint16_t>(decoded[1]) << 8) | decoded[2]);
  record->type = decoded[3];
  for (size_t index = 0; index < byteCount; ++index)
    record->data[index] = decoded[4U + index];

  if (record->type == 0x01)
    sawEndOfFile_ = true;
  return IntelHexParseResult::RecordReady;
}

IntelHexParseResult IntelHexParser::fail(const char* message) {
  failed_ = true;
  error_ = message;
  lineLength_ = 0;
  return IntelHexParseResult::Error;
}
