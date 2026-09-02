/*
 * Copyright (c) 2026. Andrew Kevin Bailey
 * This code, firmware, and software is released under the MIT License (http://opensource.org/licenses/MIT).
 */

// Run through RunRtcSyncTests.ps1 to test the actual functions extracted from the sketch.
#include "TimeData.h"
#include "RtcTimestamp.h"
#include "PpsClock.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <iterator>
#include <string>
#include <vector>

// @RTC_SYNC_DECLARATIONS@

uint64_t fakeMicroseconds = 0;

// Returns the simulated Arduino millisecond counter with its normal wraparound.
uint32_t millis() {
  return static_cast<uint32_t>(fakeMicroseconds / 1000ULL);
}

// Returns the simulated Arduino microsecond counter with its normal wraparound.
uint32_t micros() {
  return static_cast<uint32_t>(fakeMicroseconds);
}

// Advances simulated time without sleeping the test process.
void delay(uint32_t milliseconds) {
  fakeMicroseconds += static_cast<uint64_t>(milliseconds) * 1000ULL;
}

class FakeElapsedMillis {
public:
  // Measures elapsed milliseconds since the most recent assigned value.
  operator uint32_t() const {
    return static_cast<uint32_t>(millis() - baseline_);
  }

  // Resets the elapsed timer to the requested value at the current simulated time.
  FakeElapsedMillis& operator=(uint32_t value) {
    baseline_ = millis() - value;
    return *this;
  }

private:
  uint32_t baseline_ = 0;
};

struct FakeProperties {
  uint32_t frequency = 60000;

  // Returns the configured automatic RTC synchronization interval.
  uint32_t getRtcSetFrequency() const {
    return frequency;
  }
};

struct ReadResult {
  RtcTimestampReadStatus status;
  uint64_t hundredths = 0;
  uint32_t latencyMicroseconds = 100;
};

FakeProperties properties;
FakeElapsedMillis rtcSetTimerMs;
RtcSyncState rtcSyncState = RtcSyncState::Idle;
uint8_t rtcSyncAttempts = 0;
uint32_t rtcSyncStateStartedMillis = 0;
uint32_t rtcReferencePulseCount = 0;
uint32_t rtcWriteMicros = DEFAULT_RTC_WRITE_MICROS;
uint32_t lastRtcInitializationAttemptMillis = 0;
bool rtcSyncErrorReported = false;
bool rtcReadErrorReported = false;
bool rtcAvailable = true;
bool ppsClockConfirmed = true;
bool validTimTpSeen = true;
uint32_t lastValidTimTpMillis = 0;
PpsClock ppsClock;

uint32_t fakePulseCount = 10;
uint32_t fakePulseInterval = 1000000;
uint32_t fakePulseEdge = 0;
NormalizedTimestamp fakePulseUtc = {};
bool labelPulse = true;
bool writeAcknowledged = true;
uint32_t writeCalls = 0;
uint32_t readCalls = 0;
uint64_t storedHundredths = 0;
uint64_t storedAtMicroseconds = 0;
RtcTimestampReadStatus defaultReadStatus = RtcTimestampReadStatus::Success;
std::deque<ReadResult> reads;
std::vector<std::string> errors;
std::vector<std::string> logs;

// Concatenates the two Arduino string values required by production log messages.
String operator+(const String& left, const String& right) {
  return String(std::string(left.c_str()) + right.c_str());
}

// Converts a valid calendar timestamp into the RTC's hundredth-second scale.
uint64_t toHundredths(TimeData time) {
  return time.secondsSince1900() * 100ULL +
         static_cast<uint32_t>(time.getSubSec()) / 10000000U;
}

// Converts a hundredth-second timestamp into a simulated decoded RTC register burst.
RtcDateTime fromHundredths(uint64_t value) {
  TimeData time;
  assert(time.setSecondsSince1900(value / 100ULL));
  return {time.getYear(), time.getMonth(), time.getDay(), time.getHour(),
          time.getMin(), time.getSec(), static_cast<uint8_t>(value % 100ULL)};
}

struct FakeRtc {
  // Converts a decimal RTC field into the BCD representation used on the I2C bus.
  uint8_t DECtoBCD(uint8_t value) const {
    return static_cast<uint8_t>((value / 10U) * 16U + value % 10U);
  }

  // Records a production RTC write and optionally acknowledges the simulated I2C transfer.
  bool setTime(uint8_t* registers, uint8_t count) const {
    ++writeCalls;
    fakeMicroseconds += DEFAULT_RTC_WRITE_MICROS;
    if (!writeAcknowledged)
      return false;
    RtcDateTime timestamp = {};
    assert(decodeRv1805Timestamp(registers, count, &timestamp));
    TimeData time(timestamp.year, timestamp.month, timestamp.day, timestamp.hour,
                  timestamp.minute, timestamp.second, timestamp.hundredths * 10000000);
    storedHundredths = toHundredths(time);
    storedAtMicroseconds = fakeMicroseconds;
    return true;
  }
};

FakeRtc rtc;

// Supplies the latest simulated time-pulse capture to the production synchronization functions.
void getTimePulseStatus(uint32_t* count, uint32_t* interval, uint32_t* edge = nullptr) {
  *count = fakePulseCount;
  *interval = fakePulseInterval;
  if (edge != nullptr)
    *edge = fakePulseEdge;
}

// Labels the simulated capture using the real PPS clock when a UTC label is available.
void updatePpsClockFromPulse() {
  if (labelPulse)
    assert(ppsClock.setAnchor(fakePulseCount, fakePulseEdge, fakePulseUtc));
}

// Returns a queued RTC read result or advances the simulated RTC from its most recent write.
RtcTimestampReadStatus readRtcDateTime(RtcDateTime* timestamp) {
  ++readCalls;
  if (!reads.empty()) {
    const ReadResult next = reads.front();
    reads.pop_front();
    fakeMicroseconds += next.latencyMicroseconds;
    if (next.status == RtcTimestampReadStatus::Success)
      *timestamp = fromHundredths(next.hundredths);
    return next.status;
  }
  fakeMicroseconds += 100;
  if (defaultReadStatus == RtcTimestampReadStatus::Success) {
    *timestamp = fromHundredths(storedHundredths +
                                (fakeMicroseconds - storedAtMicroseconds) / 10000ULL);
  }
  return defaultReadStatus;
}

// Captures ordinary production errors without issuing additional simulated RTC reads.
void addError(const String& error) {
  errors.emplace_back(error.c_str());
}

// Captures fallback-timestamp production errors without touching the simulated RTC.
void recordRtcTimestampError(const String& error) {
  errors.emplace_back(error.c_str());
}

// Captures verified-success log messages for assertions about synchronization completion.
void appendTimestampedLog(const String& timestamp, const String& message) {
  logs.emplace_back(std::string(timestamp.c_str()) + " - " + message.c_str());
}

// @RTC_SYNC_SOURCE@

namespace {

// Restores a detected RTC and valid UTC-labelled pulse before each isolated test.
void resetFixture() {
  fakeMicroseconds = 10000000;
  properties.frequency = 60000;
  rtcSetTimerMs = 0;
  rtcSyncState = RtcSyncState::Idle;
  rtcSyncAttempts = 0;
  rtcSyncStateStartedMillis = 0;
  rtcReferencePulseCount = 0;
  rtcWriteMicros = DEFAULT_RTC_WRITE_MICROS;
  lastRtcInitializationAttemptMillis = 0;
  rtcSyncErrorReported = false;
  rtcReadErrorReported = false;
  rtcAvailable = true;
  ppsClockConfirmed = true;
  validTimTpSeen = true;
  lastValidTimTpMillis = millis();
  ppsClock.reset();
  fakePulseCount = 10;
  fakePulseInterval = 1000000;
  fakePulseEdge = micros();
  TimeData time(2026, 9, 2, 4, 32, 0, 0);
  fakePulseUtc = {static_cast<int64_t>(time.secondsSince1900()), 0};
  labelPulse = true;
  writeAcknowledged = true;
  writeCalls = 0;
  readCalls = 0;
  storedHundredths = toHundredths(time);
  storedAtMicroseconds = fakeMicroseconds;
  defaultReadStatus = RtcTimestampReadStatus::Success;
  reads.clear();
  errors.clear();
  logs.clear();
}

// Captures a fresh one-second pulse and keeps its simulated UTC message current.
void nextPulse() {
  delay(1000);
  ++fakePulseCount;
  ++fakePulseUtc.secondsSince1900;
  fakePulseEdge = micros();
  lastValidTimTpMillis = millis();
}

// Runs one automatic scheduling decision exactly as the main loop does.
void runAutomaticScheduler() {
  if (rtcSyncIntervalExpired())
    setRtc();
}

// Verifies that an interval-expired request remains pending while the RTC is temporarily absent.
void testPendingRequestSurvivesUnavailableRtc() {
  resetFixture();
  rtcSetTimerMs = properties.frequency;
  rtcAvailable = false;
  runAutomaticScheduler();
  assert(rtcSyncState == RtcSyncState::WaitForPulse);
  assert(static_cast<uint32_t>(rtcSetTimerMs) >= properties.frequency);
  for (unsigned int i = 0; i < 5; ++i) {
    nextPulse();
    serviceRtcSync();
    runAutomaticScheduler();
  }
  assert(rtcSyncState == RtcSyncState::WaitForPulse);
  assert(rtcSyncAttempts == 0 && writeCalls == 0);
  rtcAvailable = true;
  nextPulse();
  serviceRtcSync();
  assert(rtcSyncState == RtcSyncState::Idle);
  assert(writeCalls == 1 && logs.size() == 1);
}

// Verifies that invalid GNSS timing and missing UTC labels defer rather than discard a request.
void testPendingRequestWaitsForValidGpsAndLabel() {
  resetFixture();
  setRtc();
  ppsClockConfirmed = false;
  nextPulse();
  serviceRtcSync();
  assert(rtcSyncState == RtcSyncState::WaitForPulse && writeCalls == 0);
  ppsClockConfirmed = true;
  validTimTpSeen = false;
  nextPulse();
  serviceRtcSync();
  assert(writeCalls == 0);
  validTimTpSeen = true;
  nextPulse();
  lastValidTimTpMillis = millis() - TIMTP_STALE_MILLIS - 1;
  serviceRtcSync();
  assert(writeCalls == 0);
  labelPulse = false;
  nextPulse();
  serviceRtcSync();
  assert(rtcSyncState == RtcSyncState::WaitForPulse && writeCalls == 0);
  labelPulse = true;
  nextPulse();
  serviceRtcSync();
  assert(rtcSyncState == RtcSyncState::Idle && writeCalls == 1);
}

// Verifies that only a verified write starts the next full configured synchronization interval.
void testVerifiedSuccessResetsInterval() {
  resetFixture();
  rtcSetTimerMs = properties.frequency + 100;
  runAutomaticScheduler();
  assert(static_cast<uint32_t>(rtcSetTimerMs) >= properties.frequency);
  nextPulse();
  serviceRtcSync();
  assert(rtcSyncState == RtcSyncState::Idle);
  assert(writeCalls == 1 && readCalls == 1 && logs.size() == 1);
  assert(static_cast<uint32_t>(rtcSetTimerMs) == 0);
  delay(properties.frequency - 1);
  assert(!rtcSyncIntervalExpired());
  delay(1);
  assert(rtcSyncIntervalExpired());
}

// Verifies that a failed I2C write retries only after backoff and a subsequently captured pulse.
void testFailedWriteUsesDelayedFreshPulseRetry() {
  resetFixture();
  setRtc();
  writeAcknowledged = false;
  nextPulse();
  serviceRtcSync();
  assert(rtcSyncState == RtcSyncState::RetryBackoff);
  assert(rtcSyncAttempts == 1 && writeCalls == 1 && readCalls == 0);
  assert(!rtcAvailable && logs.empty());
  rtcAvailable = true;
  writeAcknowledged = true;
  delay(RTC_SYNC_RETRY_MILLIS - 1);
  serviceRtcSync();
  assert(rtcSyncState == RtcSyncState::RetryBackoff && writeCalls == 1);
  delay(1);
  serviceRtcSync();
  assert(rtcSyncState == RtcSyncState::WaitForPulse && writeCalls == 1);
  serviceRtcSync();
  assert(writeCalls == 1);
  nextPulse();
  serviceRtcSync();
  assert(rtcSyncState == RtcSyncState::Idle && writeCalls == 2 && logs.size() == 1);
}

// Verifies that readback failures exhaust a bounded attempt budget without immediately rearming overdue work.
void testExhaustedAttemptsWaitForNextInterval() {
  resetFixture();
  rtcSetTimerMs = properties.frequency;
  defaultReadStatus = RtcTimestampReadStatus::InvalidTimestamp;
  runAutomaticScheduler();
  for (uint8_t attempt = 1; attempt <= RTC_SYNC_MAX_ATTEMPTS; ++attempt) {
    nextPulse();
    serviceRtcSync();
    assert(rtcSyncAttempts == attempt && writeCalls == attempt);
    assert(logs.empty());
    if (attempt < RTC_SYNC_MAX_ATTEMPTS) {
      assert(rtcSyncState == RtcSyncState::RetryBackoff);
      runAutomaticScheduler();
      delay(RTC_SYNC_RETRY_MILLIS);
      serviceRtcSync();
      assert(rtcSyncState == RtcSyncState::WaitForPulse);
    }
  }
  assert(rtcSyncState == RtcSyncState::WaitForInterval);
  assert(readCalls == RTC_SYNC_MAX_ATTEMPTS * RTC_SYNC_VERIFY_READS);
  assert(!errors.empty());
  runAutomaticScheduler();
  serviceRtcSync();
  assert(rtcSyncState == RtcSyncState::WaitForInterval);
  assert(writeCalls == RTC_SYNC_MAX_ATTEMPTS);
  delay(properties.frequency - 1);
  assert(!rtcSyncIntervalExpired());
  delay(1);
  assert(rtcSyncIntervalExpired());
  runAutomaticScheduler();
  assert(rtcSyncState == RtcSyncState::WaitForPulse && rtcSyncAttempts == 0);
}

// Verifies that manual synchronization can override an exhausted interval without restarting active work.
void testManualRequestOverridesExhaustedCooldown() {
  resetFixture();
  rtcSyncState = RtcSyncState::WaitForInterval;
  rtcSyncStateStartedMillis = millis();
  rtcSyncAttempts = RTC_SYNC_MAX_ATTEMPTS;
  setRtc();
  assert(rtcSyncState == RtcSyncState::WaitForPulse && rtcSyncAttempts == 0);
  rtcSyncAttempts = 1;
  setRtc();
  assert(rtcSyncAttempts == 1);
  rtcSyncState = RtcSyncState::RetryBackoff;
  setRtc();
  assert(rtcSyncState == RtcSyncState::RetryBackoff && rtcSyncAttempts == 1);
}

// Verifies that zero frequency disables automatic scheduling but still permits a manual write.
void testZeroIntervalDisablesOnlyAutomaticSynchronization() {
  resetFixture();
  properties.frequency = 0;
  rtcSetTimerMs = UINT32_MAX;
  assert(!rtcSyncIntervalExpired());
  rtcSyncState = RtcSyncState::WaitForInterval;
  assert(!rtcSyncIntervalExpired());
  setRtc();
  nextPulse();
  serviceRtcSync();
  assert(rtcSyncState == RtcSyncState::Idle && writeCalls == 1);
}

// Verifies that elapsed interval and retry decisions remain correct across millisecond-counter wraparound.
void testSchedulerSurvivesMillisWraparound() {
  resetFixture();
  fakeMicroseconds = static_cast<uint64_t>(UINT32_MAX - 1000U) * 1000ULL;
  rtcSetTimerMs = 0;
  properties.frequency = 2000;
  delay(1999);
  assert(!rtcSyncIntervalExpired());
  delay(1);
  assert(rtcSyncIntervalExpired());
  rtcSyncState = RtcSyncState::WaitForInterval;
  fakeMicroseconds = static_cast<uint64_t>(UINT32_MAX - 1000U) * 1000ULL;
  rtcSyncStateStartedMillis = millis();
  delay(1999);
  assert(!rtcSyncIntervalExpired());
  delay(1);
  assert(rtcSyncIntervalExpired());
  rtcSyncState = RtcSyncState::RetryBackoff;
  fakeMicroseconds = static_cast<uint64_t>(UINT32_MAX - 1000U) * 1000ULL;
  rtcSyncStateStartedMillis = millis();
  delay(RTC_SYNC_RETRY_MILLIS - 1);
  serviceRtcSync();
  assert(rtcSyncState == RtcSyncState::RetryBackoff);
  delay(1);
  serviceRtcSync();
  assert(rtcSyncState == RtcSyncState::WaitForPulse);
}

// Verifies that a temporary invalid or incomplete verification read can recover without another RTC write.
void testVerificationRetriesReadsBeforeRewriting() {
  resetFixture();
  reads.push_back({RtcTimestampReadStatus::TransportFailure});
  reads.push_back({RtcTimestampReadStatus::InvalidTimestamp});
  setRtc();
  nextPulse();
  serviceRtcSync();
  assert(rtcSyncState == RtcSyncState::Idle);
  assert(writeCalls == 1 && readCalls == 3 && logs.size() == 1);
}

// Verifies that consistently failed verification transfers request RTC rediscovery while retaining pending work.
void testVerificationTransportFailureRequestsRecovery() {
  resetFixture();
  defaultReadStatus = RtcTimestampReadStatus::TransportFailure;
  setRtc();
  nextPulse();
  serviceRtcSync();
  assert(rtcSyncState == RtcSyncState::RetryBackoff && !rtcAvailable);
  assert(writeCalls == 1 && readCalls == RTC_SYNC_VERIFY_READS && logs.empty());
  assert(static_cast<uint32_t>(millis() - lastRtcInitializationAttemptMillis) >=
         RTC_INITIALIZATION_RETRY_MILLIS);
  delay(RTC_SYNC_RETRY_MILLIS);
  serviceRtcSync();
  assert(rtcSyncState == RtcSyncState::WaitForPulse);
  nextPulse();
  serviceRtcSync();
  assert(rtcSyncState == RtcSyncState::WaitForPulse && writeCalls == 1);
  rtcAvailable = true;
  defaultReadStatus = RtcTimestampReadStatus::Success;
  nextPulse();
  serviceRtcSync();
  assert(rtcSyncState == RtcSyncState::Idle && writeCalls == 2 && logs.size() == 1);
}

// Verifies that invalid or mismatched readback is distinct from losing I2C communication entirely.
void testVerificationDistinguishesContentAndTransportFailures() {
  resetFixture();
  TimeData written(2026, 9, 2, 4, 32, 0, 500000000);
  defaultReadStatus = RtcTimestampReadStatus::TransportFailure;
  assert(verifyRtcWrite(written) == RtcTimestampReadStatus::TransportFailure);
  reads.push_back({RtcTimestampReadStatus::TransportFailure});
  reads.push_back({RtcTimestampReadStatus::InvalidTimestamp});
  reads.push_back({RtcTimestampReadStatus::TransportFailure});
  assert(verifyRtcWrite(written) == RtcTimestampReadStatus::InvalidTimestamp);
  reads.push_back({RtcTimestampReadStatus::TransportFailure});
  reads.push_back({RtcTimestampReadStatus::Success, toHundredths(written) + 100});
  reads.push_back({RtcTimestampReadStatus::TransportFailure});
  assert(verifyRtcWrite(written) == RtcTimestampReadStatus::InvalidTimestamp);

  resetFixture();
  defaultReadStatus = RtcTimestampReadStatus::InvalidTimestamp;
  setRtc();
  nextPulse();
  serviceRtcSync();
  assert(rtcAvailable && rtcSyncState == RtcSyncState::RetryBackoff);
  assert(writeCalls == 1 && logs.empty());
}

// Verifies that the readback tolerance accepts small quantization differences and rejects larger errors.
void testVerificationToleranceAndMismatches() {
  for (int64_t offset : {-3, -2, 0, 2, 3}) {
    resetFixture();
    TimeData written(2026, 9, 2, 4, 32, 0, 509999999);
    const uint64_t expected = toHundredths(written);
    for (uint8_t read = 0; read < RTC_SYNC_VERIFY_READS; ++read)
      reads.push_back({RtcTimestampReadStatus::Success,
                      static_cast<uint64_t>(static_cast<int64_t>(expected) + offset)});
    const bool expectedMatch = offset >= -RTC_SYNC_VERIFY_TOLERANCE_HUNDREDTHS &&
                               offset <= RTC_SYNC_VERIFY_TOLERANCE_HUNDREDTHS;
    const RtcTimestampReadStatus expectedStatus = expectedMatch
        ? RtcTimestampReadStatus::Success : RtcTimestampReadStatus::InvalidTimestamp;
    assert(verifyRtcWrite(written) == expectedStatus);
    assert(readCalls == (expectedMatch ? 1U : RTC_SYNC_VERIFY_READS));
  }
}

// Verifies elapsed readback comparison across date, month, year, and microsecond-counter boundaries.
void testVerificationAcrossCalendarAndMicrosRollover() {
  const TimeData dates[] = {
      TimeData(2026, 9, 2, 23, 59, 59, 990000000),
      TimeData(2026, 9, 30, 23, 59, 59, 990000000),
      TimeData(2026, 12, 31, 23, 59, 59, 990000000),
      TimeData(2028, 2, 28, 23, 59, 59, 990000000)};
  for (TimeData written : dates) {
    resetFixture();
    fakeMicroseconds = UINT32_MAX - 500U;
    reads.push_back({RtcTimestampReadStatus::Success, toHundredths(written) + 5, 50000});
    assert(verifyRtcWrite(written) == RtcTimestampReadStatus::Success);
    assert(readCalls == 1);
  }
}

} // namespace

// Runs the production RTC synchronization regression scenarios and reports success.
int main() {
  testPendingRequestSurvivesUnavailableRtc();
  testPendingRequestWaitsForValidGpsAndLabel();
  testVerifiedSuccessResetsInterval();
  testFailedWriteUsesDelayedFreshPulseRetry();
  testExhaustedAttemptsWaitForNextInterval();
  testManualRequestOverridesExhaustedCooldown();
  testZeroIntervalDisablesOnlyAutomaticSynchronization();
  testSchedulerSurvivesMillisWraparound();
  testVerificationRetriesReadsBeforeRewriting();
  testVerificationTransportFailureRequestsRecovery();
  testVerificationDistinguishesContentAndTransportFailures();
  testVerificationToleranceAndMismatches();
  testVerificationAcrossCalendarAndMicrosRollover();
  std::puts("RTC sync regression tests passed (13 scenarios).");
  return 0;
}
