/*
 * Copyright (c) 2026. Andrew Kevin Bailey
 * This code, firmware, and software is released under the MIT License (http://opensource.org/licenses/MIT).
 */

#include "../IntelHexParser.h"

#include <assert.h>
#include <string.h>

namespace {

IntelHexParseResult feed(IntelHexParser& parser, const char* text, IntelHexRecord* record) {
  IntelHexParseResult result = IntelHexParseResult::NeedMoreData;
  bool recordReady = false;
  while (*text != '\0') {
    result = parser.consume(static_cast<uint8_t>(*text++), record);
    if (result == IntelHexParseResult::Error)
      return result;
    if (result == IntelHexParseResult::RecordReady)
      recordReady = true;
  }
  return recordReady ? IntelHexParseResult::RecordReady : result;
}

void testValidRecordsAndCrLf() {
  IntelHexParser parser;
  IntelHexRecord record = {};
  assert(feed(parser, ":0200000460009A\r\n", &record) == IntelHexParseResult::RecordReady);
  assert(record.byteCount == 2 && record.address == 0 && record.type == 4);
  assert(record.data[0] == 0x60 && record.data[1] == 0x00);

  assert(feed(parser, ":0400100001020304E2\n", &record) == IntelHexParseResult::RecordReady);
  assert(record.byteCount == 4 && record.address == 0x10 && record.type == 0);
  assert(record.data[0] == 1 && record.data[3] == 4);

  assert(feed(parser, ":040000056000100087\r\n", &record) == IntelHexParseResult::RecordReady);
  assert(record.type == 5);
  assert(feed(parser, ":00000001FF\n", &record) == IntelHexParseResult::RecordReady);
  assert(parser.sawEndOfFile());
  assert(parser.finish(&record) == IntelHexParseResult::NeedMoreData);
}

void testFinalRecordWithoutNewline() {
  IntelHexParser parser;
  IntelHexRecord record = {};
  assert(feed(parser, ":00000001FF", &record) == IntelHexParseResult::NeedMoreData);
  assert(parser.finish(&record) == IntelHexParseResult::RecordReady);
  assert(record.type == 1 && parser.sawEndOfFile());
}

void testInvalidChecksum() {
  IntelHexParser parser;
  IntelHexRecord record = {};
  assert(feed(parser, ":0400100001020304E3\n", &record) == IntelHexParseResult::Error);
  assert(strstr(parser.error(), "checksum") != nullptr);
}

void testMismatchedLengthAndNonHexDigit() {
  IntelHexParser parser;
  IntelHexRecord record = {};
  assert(feed(parser, ":04001000010203E6\n", &record) == IntelHexParseResult::Error);

  parser.reset();
  assert(feed(parser, ":0400100001020Z04E2\n", &record) == IntelHexParseResult::Error);
  assert(strstr(parser.error(), "non-hexadecimal") != nullptr);
}

void testMissingEofAndDataAfterEof() {
  IntelHexParser parser;
  IntelHexRecord record = {};
  assert(feed(parser, ":0400100001020304E2\n", &record) == IntelHexParseResult::RecordReady);
  assert(parser.finish(&record) == IntelHexParseResult::Error);
  assert(strstr(parser.error(), "end-of-file") != nullptr);

  parser.reset();
  assert(feed(parser, ":00000001FF\n", &record) == IntelHexParseResult::RecordReady);
  assert(parser.consume(':', &record) == IntelHexParseResult::Error);
}

void testOversizedRecord() {
  IntelHexParser parser;
  IntelHexRecord record = {};
  assert(parser.consume(':', &record) == IntelHexParseResult::NeedMoreData);
  for (size_t index = 0; index < 520; ++index)
    assert(parser.consume('0', &record) == IntelHexParseResult::NeedMoreData);
  assert(parser.consume('0', &record) == IntelHexParseResult::Error);
  assert(strstr(parser.error(), "too long") != nullptr);
}

} // namespace

int main() {
  testValidRecordsAndCrLf();
  testFinalRecordWithoutNewline();
  testInvalidChecksum();
  testMismatchedLengthAndNonHexDigit();
  testMissingEofAndDataAfterEof();
  testOversizedRecord();
  return 0;
}
