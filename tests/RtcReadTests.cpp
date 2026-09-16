#include "RtcTimestamp.h"
#include <cassert>
#include <cstdint>
#include <deque>

constexpr uint8_t RV1805_ADDR = 0x69;
constexpr uint8_t RV1805_HUNDREDTHS = 0;

struct FakeWire {
  bool stopped = false;
  uint8_t addressStatus = 0;
  uint8_t responseLength = 8;
  std::deque<int> bytes;

  // Begins register selection for the RTC's I2C address.
  void beginTransmission(uint8_t address) {
    assert(address == RV1805_ADDR);
    stopped = false;
  }
  // Accepts the first calendar register address.
  std::size_t write(uint8_t address) {
    assert(address == RV1805_HUNDREDTHS);
    return 1;
  }
  // Records whether register selection terminates with the required STOP.
  uint8_t endTransmission(bool stop) {
    stopped = stop;
    return addressStatus;
  }
  // Supplies a known calendar burst only after a complete register-selection transaction.
  uint8_t requestFrom(uint8_t address, uint8_t count, uint8_t stop) {
    assert(address == RV1805_ADDR && count == 8 && stop);
    assert(stopped);
    bytes = {0x79, 0x16, 0x55, 0x05, 0x15, 0x09, 0x26, 0x02};
    while (bytes.size() > responseLength)
      bytes.pop_back();
    return responseLength;
  }
  // Reports the remaining simulated receive-buffer bytes.
  int available() const { return static_cast<int>(bytes.size()); }
  // Consumes one byte from the simulated receive buffer.
  int read() {
    if (bytes.empty()) return -1;
    const int value = bytes.front();
    bytes.pop_front();
    return value;
  }
} Wire;

// @RTC_READ_SOURCE@

// Checks the production RTC transaction, timestamp decoding, and short-read failure handling.
int main() {
  RtcDateTime time = {};
  assert(readRtcDateTime(&time) == RtcTimestampReadStatus::Success);
  assert(time.year == 2026 && time.month == 9 && time.day == 15);
  assert(time.hour == 5 && time.minute == 55 && time.second == 16);
  assert(time.hundredths == 79);
  Wire.responseLength = 7;
  assert(readRtcDateTime(&time) == RtcTimestampReadStatus::TransportFailure);
  assert(Wire.available() == 0);
  Wire.addressStatus = 2;
  assert(readRtcDateTime(&time) == RtcTimestampReadStatus::TransportFailure);
  assert(readRtcDateTime(nullptr) == RtcTimestampReadStatus::InvalidTimestamp);
}
