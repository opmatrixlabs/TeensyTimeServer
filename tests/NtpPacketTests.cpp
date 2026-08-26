/*
 * Copyright (c) 2025. Andrew Kevin Bailey
 * This code, firmware, and software is released under the MIT License (http://opensource.org/licenses/MIT).
 *
 * The MIT License (MIT)
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or significant portions of
 * the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "../NtpPacket.h"

#include <assert.h>
#include <string.h>

namespace {
void initializeRequest(uint8_t* request, const uint8_t version, const uint8_t poll) {
  memset(request, 0, NTP_PACKET_SIZE);
  request[0] = static_cast<uint8_t>(0xC0 | (version << 3) | 3); // Client LI must not be copied to the response.
  request[2] = poll;
  for (size_t i = 0; i < 8; ++i)
    request[40 + i] = static_cast<uint8_t>(0xA0 + i);
}

void assertBytesEqual(const uint8_t* actual, const uint8_t* expected, const size_t length) {
  for (size_t i = 0; i < length; ++i)
    assert(actual[i] == expected[i]);
}

void testVersionPollOriginAndTimestamps() {
  const NormalizedTimestamp receiveTime = normalizeTimestamp(0x01020304LL, 500000000);
  const NormalizedTimestamp transmitTime = normalizeTimestamp(0x11121314LL, 250000000);

  for (uint8_t version = 3; version <= 4; ++version) {
    uint8_t request[NTP_PACKET_SIZE] = {};
    initializeRequest(request, version, 0xFA);

    uint8_t guardedResponse[NTP_PACKET_SIZE + 2];
    memset(guardedResponse, 0xA5, sizeof(guardedResponse));
    uint8_t* response = guardedResponse + 1;
    const NtpResponseStatus status = createNtpResponse(request,
                                                       NTP_PACKET_SIZE,
                                                       receiveTime,
                                                       transmitTime,
                                                       true,
                                                       response,
                                                       NTP_PACKET_SIZE);
    assert(status == NtpResponseStatus::Ready);
    assert(guardedResponse[0] == 0xA5);
    assert(guardedResponse[NTP_PACKET_SIZE + 1] == 0xA5);
    assert(response[0] == static_cast<uint8_t>((version << 3) | 4));
    assert(response[1] == 1);
    assert(response[2] == 0xFA);
    assert(response[3] == 0xF7);
    assert(response[11] == 0x50);
    assert(response[12] == 'G');
    assert(response[13] == 'P');
    assert(response[14] == 'S');
    assert(response[15] == 0);
    assertBytesEqual(response + 24, request + 40, 8);

    const uint8_t expectedReceive[8] = {0x01, 0x02, 0x03, 0x04, 0x80, 0, 0, 0};
    const uint8_t expectedTransmit[8] = {0x11, 0x12, 0x13, 0x14, 0x40, 0, 0, 0};
    assertBytesEqual(response + 32, expectedReceive, sizeof(expectedReceive));
    assertBytesEqual(response + 40, expectedTransmit, sizeof(expectedTransmit));
  }
}

void testInvalidRequestsDoNotModifyResponse() {
  uint8_t request[NTP_PACKET_SIZE] = {};
  initializeRequest(request, 4, 6);

  uint8_t response[NTP_PACKET_SIZE];
  memset(response, 0xA5, sizeof(response));

  const size_t invalidLengths[] = {0, 1, NTP_PACKET_SIZE - 1, NTP_PACKET_SIZE + 1};
  for (const size_t invalidLength : invalidLengths) {
    assert(createNtpResponse(request, invalidLength, {}, {}, true, response, sizeof(response)) ==
           NtpResponseStatus::InvalidLength);
  }
  assert(createNtpResponse(nullptr, NTP_PACKET_SIZE, {}, {}, true, response, sizeof(response)) ==
         NtpResponseStatus::InvalidLength);

  for (uint8_t mode = 0; mode <= 7; ++mode) {
    if (mode == 3)
      continue;
    request[0] = static_cast<uint8_t>((4 << 3) | mode);
    assert(createNtpResponse(request, NTP_PACKET_SIZE, {}, {}, true, response, sizeof(response)) ==
           NtpResponseStatus::InvalidMode);
  }

  for (uint8_t version = 0; version <= 7; ++version) {
    if (version == 3 || version == 4)
      continue;
    request[0] = static_cast<uint8_t>((version << 3) | 3);
    assert(createNtpResponse(request, NTP_PACKET_SIZE, {}, {}, true, response, sizeof(response)) ==
           NtpResponseStatus::UnsupportedVersion);
  }

  for (size_t i = 0; i < sizeof(response); ++i)
    assert(response[i] == 0xA5);
}

void testOutputAndTimeFailuresDoNotModifyResponse() {
  uint8_t request[NTP_PACKET_SIZE] = {};
  initializeRequest(request, 4, 6);

  uint8_t response[NTP_PACKET_SIZE];
  const NormalizedTimestamp timestamp = normalizeTimestamp(100, 500000000);
  assert(createNtpResponse(request,
                           NTP_PACKET_SIZE,
                           timestamp,
                           timestamp,
                           true,
                           response,
                           sizeof(response)) == NtpResponseStatus::Ready);
  uint8_t successfulResponse[NTP_PACKET_SIZE];
  memcpy(successfulResponse, response, sizeof(response));

  assert(createNtpResponse(request, NTP_PACKET_SIZE, {}, {}, true, response, NTP_PACKET_SIZE - 1) ==
         NtpResponseStatus::ResponseBufferTooSmall);
  assert(createNtpResponse(request, NTP_PACKET_SIZE, {}, {}, false, response, sizeof(response)) ==
         NtpResponseStatus::TimeUnavailable);

  assertBytesEqual(response, successfulResponse, sizeof(response));
}
}

int main() {
  testVersionPollOriginAndTimestamps();
  testInvalidRequestsDoNotModifyResponse();
  testOutputAndTimeFailuresDoNotModifyResponse();
  return 0;
}
