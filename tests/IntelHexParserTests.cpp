/*
 * Copyright (c) 2026. Andrew Kevin Bailey
 * This code, firmware, and software is released under the MIT License (http://opensource.org/licenses/MIT).
 */

#include "../IntelHexParser.h"

#include <assert.h>
#include <string.h>

namespace {

// Feeds a text fragment into the parser and returns the most significant resulting parse state.
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

// Verifies parsing of valid Intel HEX records terminated by either CRLF or LF.
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

// Verifies that the parser accepts a complete final record without a trailing newline.
void testFinalRecordWithoutNewline() {
  IntelHexParser parser;
  IntelHexRecord record = {};
  assert(feed(parser, ":00000001FF", &record) == IntelHexParseResult::NeedMoreData);
  assert(parser.finish(&record) == IntelHexParseResult::RecordReady);
  assert(record.type == 1 && parser.sawEndOfFile());
}

// Verifies that a record with an invalid checksum is rejected with an explanatory error.
void testInvalidChecksum() {
  IntelHexParser parser;
  IntelHexRecord record = {};
  assert(feed(parser, ":0400100001020304E3\n", &record) == IntelHexParseResult::Error);
  assert(strstr(parser.error(), "checksum") != nullptr);
}

// Verifies rejection of records with mismatched lengths or non-hexadecimal characters.
void testMismatchedLengthAndNonHexDigit() {
  IntelHexParser parser;
  IntelHexRecord record = {};
  assert(feed(parser, ":04001000010203E6\n", &record) == IntelHexParseResult::Error);

  parser.reset();
  assert(feed(parser, ":0400100001020Z04E2\n", &record) == IntelHexParseResult::Error);
  assert(strstr(parser.error(), "non-hexadecimal") != nullptr);
}

// Verifies detection of a missing end-of-file record and data received after that record.
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

// Verifies that an Intel HEX record exceeding the parser limit is rejected.
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

// Runs all Intel HEX parser unit tests and reports success through the process exit code.
int main() {
  testValidRecordsAndCrLf();
  testFinalRecordWithoutNewline();
  testInvalidChecksum();
  testMismatchedLengthAndNonHexDigit();
  testMissingEofAndDataAfterEof();
  testOversizedRecord();
  return 0;
}
