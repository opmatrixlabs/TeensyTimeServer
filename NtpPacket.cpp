/*
 * Copyright (c) 2026. Andrew Kevin Bailey
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

#include "NtpPacket.h"

#include <string.h>

namespace {
constexpr uint8_t NTP_VERSION_1 = 1;
constexpr uint8_t NTP_VERSION_4 = 4;
constexpr uint8_t NTP_CLIENT_MODE = 3;
constexpr uint8_t NTP_SERVER_MODE = 4;
constexpr uint8_t NTP_STRATUM_GPS = 1;
constexpr uint8_t NTP_LEAP_ALARM = 3;
constexpr uint8_t NTP_PRECISION_MINUS_9 = 0xF7;
constexpr uint8_t NTP_ROOT_DISPERSION_LOW_BYTE = 0x50;
}

// Validates an NTP request's length, client mode, and protocol version.
NtpResponseStatus validateNtpRequest(const uint8_t* request, const std::size_t requestLength) {
  if (request == nullptr || requestLength != NTP_PACKET_SIZE)
    return NtpResponseStatus::InvalidLength;

  const uint8_t mode = request[0] & 0x07;
  if (mode != NTP_CLIENT_MODE)
    return NtpResponseStatus::InvalidMode;

  const uint8_t version = (request[0] >> 3) & 0x07;
  if (version < NTP_VERSION_1 || version > NTP_VERSION_4)
    return NtpResponseStatus::UnsupportedVersion;

  return NtpResponseStatus::Ready;
}

// Builds a synchronized or explicitly unsynchronized NTP response for a valid client request.
NtpResponseStatus createNtpResponse(const uint8_t* request,
                                    const std::size_t requestLength,
                                    const NormalizedTimestamp& referenceTime,
                                    const NormalizedTimestamp& receiveTime,
                                    const NormalizedTimestamp& transmitTime,
                                    const bool timeAvailable,
                                    uint8_t* response,
                                    const std::size_t responseCapacity) {
  const NtpResponseStatus requestStatus = validateNtpRequest(request, requestLength);
  if (requestStatus != NtpResponseStatus::Ready)
    return requestStatus;

  if (response == nullptr || responseCapacity < NTP_PACKET_SIZE)
    return NtpResponseStatus::ResponseBufferTooSmall;

  uint8_t packet[NTP_PACKET_SIZE] = {};
  const uint8_t requestVersion = (request[0] >> 3) & 0x07;

  if (!timeAvailable) {
    // A valid request still receives an explicit "clock unsynchronized" reply.
    // Clients can distinguish loss of synchronization from an unreachable server
    // without ever accepting a fallback or stale timestamp.
    packet[0] = static_cast<uint8_t>((NTP_LEAP_ALARM << 6) |
                                     (requestVersion << 3) |
                                     NTP_SERVER_MODE);
    packet[2] = request[2];
    memcpy(packet + 24, request + 40, 8);
    memcpy(response, packet, NTP_PACKET_SIZE);
    return NtpResponseStatus::Ready;
  }

  packet[0] = static_cast<uint8_t>((requestVersion << 3) | NTP_SERVER_MODE);
  packet[1] = NTP_STRATUM_GPS;
  packet[2] = request[2];
  packet[3] = NTP_PRECISION_MINUS_9;
  packet[11] = NTP_ROOT_DISPERSION_LOW_BYTE;
  packet[12] = 'G';
  packet[13] = 'P';
  packet[14] = 'S';

  // The server Originate Timestamp is the client's Transmit Timestamp (T1).
  memcpy(packet + 24, request + 40, 8);
  writeNtpTimestamp(packet + 16, toNtpTimestamp(referenceTime));
  writeNtpTimestamp(packet + 32, toNtpTimestamp(receiveTime));
  writeNtpTimestamp(packet + 40, toNtpTimestamp(transmitTime));

  memcpy(response, packet, NTP_PACKET_SIZE);
  return NtpResponseStatus::Ready;
}
