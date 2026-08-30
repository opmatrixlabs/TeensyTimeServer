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


/*
 * NOTE: The RTC timestamps log entries. NTP time is disciplined by GNSS TP1
 * and labelled with UBX-TIM-TP; the RTC is not in the NTP timing path.
 */
#include <list>

#include <SPI.h> // Needed for SPI to Ethernet
#include <Ethernet.h>
#include <Dhcp.h>
#include <utility/w5100.h>
#include <Wire.h> // Needed for I2C for GNSS, RTC, and OLED
#include <EEPROM.h> // Direct include keeps Visual Micro library discovery in sync

#include <SparkFun_u-blox_GNSS_v3.h> // GNSS receiver
#include <SparkFun_RV1805.h> // RealTime clock
#include <SparkFun_Qwiic_OLED.h> // OLED 1.3in display
#include <res/qw_fnt_8x16.h>  // OLED display font
#include <res/qw_fnt_5x7.h>  // OLED display font

#include "Properties.h"
#include "TimeData.h"
#include "TimeHttp.h"
#include "NtpTimestamp.h"
#include "NtpPacket.h"
#include "PpsClock.h"
#include "GnssStatus.h"
#include "FirmwareUpdater.h"
#include "RtcTimestamp.h"

// Forward declarations keep the sketch valid for standard C++ IntelliSense.
void getDeviceConfig();
void cacheNavPvtData(const UBX_NAV_PVT_data_t& data);
void navPvtCallback(UBX_NAV_PVT_data_t* data);
void serviceCachedGnssStatus();
bool configureTimePulseTiming();
bool configureTimePulseControls();
void initializePeripheralServices();
void serviceGnssInitialization();
void serviceRtcInitialization();
void serviceDisplayInitialization();
void requestPpsTimebaseAcquisition();
void servicePpsTimebaseAcquisition();
void invalidatePpsTimebase();
void drainGnssBeforeTimTpBoundary();
void servicePpsTimebase();
void updatePpsClockFromPulse();
bool getPpsTimestamp(uint32_t captureMicros, NormalizedTimestamp* timestamp);
bool configureRtcXtOscillator();
void setRtc();
void serviceRtcSync();
String getRtcISO8601Time();
String getRtcWebISO8601Time();
void initializeEthernetService();
void serviceEthernet();
void requestEthernetSocketRecovery(const char* reason, bool ntpSocketFailed = false);
void setFirmwareUpdateMaintenance(bool active);
void installFirmwareUpdate();
void serviceFirmwareInstall();
bool bindNtpUdpSocket();
bool discardCurrentUdpPacket();
void processNtpRequest(int packetSize, uint32_t receiveCaptureMicros);
void timePulseInterrupt();
void getTimePulseStatus(uint32_t* pulseCount,
                        uint32_t* intervalMicros,
                        uint32_t* edgeMicros = nullptr,
                        uint32_t* invalidIntervalCount = nullptr);
void reportTimePulse();
String getGpsISO8601Time();
void addLog(String log);
void addError(String error);
void recordError(String error);
void displaySettings();

const char* APP_NAME = "GPS NTP Time Server";
const char* VERSION = "3.0";
const char* AUTHOR = "Andrew Kevin Bailey";

/**** Setup Properties init *****/
Properties properties;

/***** Ethernet init *****/
byte mac[] = { 0xBC, 0xED, 0x5D, 0x3E, 0x94, 0xB6 };

constexpr uint8_t W5500_CS_PIN = 10;
constexpr uint8_t W5500_RESET_PIN = 3;
constexpr uint16_t HTTP_PORT = 80;
constexpr uint32_t W5500_RESET_LOW_MICROS = 1000;
constexpr uint32_t W5500_RESET_RELEASE_MILLIS = 2;
constexpr uint32_t ETHERNET_LINK_POLL_MILLIS = 250;
constexpr uint32_t ETHERNET_LINK_STABLE_MILLIS = 2000;
constexpr uint32_t ETHERNET_LINK_WAIT_RESET_MILLIS = 15000;
constexpr uint32_t ETHERNET_HEALTH_CHECK_MILLIS = 1000;
constexpr uint32_t ETHERNET_RECOVERY_STABLE_MILLIS = 60000;
constexpr uint32_t DHCP_TIMEOUT_MILLIS = 4000;
constexpr uint32_t DHCP_RESPONSE_TIMEOUT_MILLIS = 1000;
constexpr uint32_t DHCP_MAINTAIN_MILLIS = 60000;
constexpr uint8_t MAX_SOCKET_REPAIR_ATTEMPTS = 3;
constexpr uint8_t MAX_SERVICE_REPAIR_ATTEMPTS = 3;
constexpr uint8_t MAX_W5500_RESET_ATTEMPTS = 3;

enum class EthernetServiceState : uint8_t {
  ControllerInitialize,
  WaitingForLink,
  LinkStabilizing,
  ConfigureNetwork,
  StartServices,
  Online,
  RepairSockets,
  ReconfigureServices,
  HardwareReset,
  RetryBackoff
};

EthernetUDP udp;
EthernetServer httpServer(HTTP_PORT);
bool ntpUdpBound = false;
uint8_t ntpSocketNumber = MAX_SOCK_NUM;
bool ntpBindFailureReported = false;
bool ethernetOnline = false;
bool ethernetEverOnline = false;
bool ethernetRestartArmed = false;
EthernetServiceState ethernetServiceState = EthernetServiceState::ControllerInitialize;
EthernetServiceState ethernetRetryState = EthernetServiceState::ControllerInitialize;
uint32_t ethernetStateStartedMillis = 0;
uint32_t ethernetRetryDelayMillis = 0;
uint32_t ethernetLinkStableSinceMillis = 0;
uint32_t lastEthernetPollMillis = 0;
uint32_t lastEthernetHealthCheckMillis = 0;
uint32_t lastDhcpMaintainMillis = 0;
uint32_t ethernetOnlineSinceMillis = 0;
uint8_t ethernetSocketRepairAttempts = 0;
uint8_t ethernetServiceRepairAttempts = 0;
uint8_t ethernetDhcpRetryAttempts = 0;
uint8_t ethernetSocketRecoveryCycles = 0;
uint8_t ethernetServiceRecoveryCycles = 0;
uint8_t w5500ResetAttempts = 0;
const char* ethernetRecoveryReason = "startup";
bool activeEthernetDhcp = false;
IPAddress activeEthernetLocalIp;
IPAddress activeEthernetSubnet;
IPAddress activeEthernetDns1Ip;
IPAddress activeEthernetDns2Ip;
IPAddress activeEthernetGatewayIp;
IPAddress appliedEthernetLocalIp;
IPAddress appliedEthernetSubnet;
IPAddress appliedEthernetGatewayIp;
bool appliedEthernetConfigurationValid = false;
// Local IP addresses for display
String strLocalIp = "";
String strSubnet = "";
String strDns1Ip = "";
String strDns2Ip = "";
String strGatewayIp = "";

/***** Time server init *****/
const unsigned int ntpPort = 123;
TimeData t;

/***** Real time clock *****/
RV1805 rtc;
bool rtcAvailable = false;
bool rtcReadErrorReported = false;
bool rtcHundredthsAvailable = false;

enum class RtcSyncState : uint8_t {
  Idle,
  WaitForPulse
};

constexpr uint64_t GPS_EPOCH_SECONDS_SINCE_1900 = 2524953600ULL; // 1980-01-06T00:00:00Z
constexpr uint64_t SECONDS_PER_WEEK = 604800ULL;
constexpr uint32_t DEFAULT_RTC_WRITE_MICROS = 300;
constexpr uint32_t RTC_CAPTURE_MAX_AGE_MICROS = 5000000;
// Keep the XT oscillator active on both VDD and backup power. The standby RC
// oscillator is calibrated every 512 seconds and is used only if XT fails.
constexpr uint8_t RTC_XT_STARTUP_MODE = 0b01100000;
constexpr uint8_t RTC_XT_FAILURE_FALLBACK_MODE = 0b01101000;
constexpr uint8_t RTC_OSCILLATOR_MODE_RC_MASK = 0b00010000;
constexpr uint8_t RTC_OSCILLATOR_STATUS_FLAGS_MASK = 0b00000011;
constexpr uint32_t RTC_XT_START_TIMEOUT_MS = 500;
constexpr uint32_t RTC_INITIALIZATION_RETRY_MILLIS = 5000;

RtcSyncState rtcSyncState = RtcSyncState::Idle;
uint32_t rtcReferencePulseCount = 0;
uint32_t rtcWriteMicros = DEFAULT_RTC_WRITE_MICROS;
bool rtcSyncErrorReported = false;
bool rtcInitializationFailureReported = false;
uint32_t lastRtcInitializationAttemptMillis = 0;

/***** Logging data init *****/
std::list<String> usageLog;
std::list<String> errorLog;
String configLog;

/***** OLED Display init *****/
Qwiic1in3OLED myOLED;
bool isDisplayInverted = false;
bool appliedDisplayAlternate = false;
bool oledAvailable = false;
bool oledInitializationFailureReported = false;
uint32_t lastOledInitializationAttemptMillis = 0;
constexpr uint32_t OLED_INITIALIZATION_RETRY_MILLIS = 5000;

/***** Timer init *****/
elapsedMillis refreshTimerMs;
elapsedMillis rtcSetTimerMs;

/***** GPS init *****/
String gpsFixType = "Waiting for GNSS";
SFE_UBLOX_GNSS myGNSS;
constexpr uint32_t NAVIGATION_EPOCH_MILLIS = 1000;
constexpr uint16_t NAV_PVT_COMMAND_MAX_WAIT_MILLIS = 250;
constexpr uint32_t NAV_PVT_INITIAL_SAMPLE_WAIT_MILLIS = 1200;
constexpr uint16_t GNSS_COMMAND_MAX_WAIT_MILLIS = 250;
constexpr uint16_t GNSS_CONFIG_QUERY_MAX_WAIT_MILLIS = 100;
constexpr uint32_t GNSS_POWER_SETTLE_MILLIS = 2000;
constexpr uint32_t GNSS_INITIALIZATION_RETRY_MAX_MILLIS = 30000;
constexpr uint8_t MAX_GNSS_CONFIGURATION_FAILURES = 3;

enum class GnssInitializationState : uint8_t {
  WaitingForPower,
  Probe,
  ConfigureVal8,
  ConfigureDynamicModel,
  ConfigureMeasurementRate,
  ConfigureNavigationRate,
  ConfigureTimePulseTiming,
  ConfigureTimePulseControls,
  ConfigureNavPvtCallback,
  WaitForInitialNavPvt,
  ConfigureNavPvtRate,
  ConfirmNavPvtRate,
  ReadModuleInformation,
  ReadLeapSeconds,
  Ready
};

GnssInitializationState gnssInitializationState =
    GnssInitializationState::WaitingForPower;
GnssInitializationState lastReportedGnssFailureState =
    GnssInitializationState::Ready;
uint32_t gnssStateStartedMillis = 0;
uint32_t gnssStateDelayMillis = GNSS_POWER_SETTLE_MILLIS;
uint32_t gnssInitialSampleStartedMillis = 0;
uint8_t gnssConfigurationIndex = 0;
uint8_t gnssInitializationAttempts = 0;
uint8_t requestedNavPvtEpochRate = 1;
bool gnssDetected = false;
bool gnssReady = false;
bool gnssProbeFailureReported = false;
bool gpsLeapSecondsAvailable = false;
int8_t gpsLeapSecondsSince1980 = 0;
bool suppressDetailedGnssConfigQueries = false;
GnssStatusCache gnssStatusCache;
uint8_t navPvtEpochRate = 1;
uint32_t gnssStatusMaximumAgeMillis = 3000;
bool gnssStatusDirty = true;
bool gnssStatusDisplayInitialized = false;
bool gnssStatusWasFresh = false;
//#define gnssAddress 0x42 // The default I2C address for u-blox modules is 0x42. Change this if required

/***** GNSS time pulse init *****/
constexpr uint8_t TIME_PULSE_PIN = 0; // DEV-20748 RXI routes to Teensy pin 0 when SEL is LOW.
constexpr uint32_t TIME_PULSE_STALE_MICROS = 1500000;
constexpr uint32_t TIMTP_STALE_MILLIS = 3000;
constexpr uint32_t TIMTP_STREAM_RESTART_MILLIS = 5000;
constexpr uint32_t TIMTP_DRAIN_DELAY_MILLIS = 25;
constexpr uint32_t TIMTP_POST_EDGE_DRAIN_GUARD_MICROS = 25000;
constexpr uint32_t PPS_ACQUISITION_RETRY_MILLIS = 1379;
constexpr uint8_t MAX_PPS_ACQUISITION_FAILURES = 3;
constexpr int64_t TIMTP_LABEL_TOLERANCE_NANOSECONDS = 1000000LL;
volatile uint32_t timePulseCount = 0;
volatile uint32_t timePulseEdgeMicros = 0;
volatile uint32_t timePulseIntervalMicros = 0;
volatile uint32_t timePulseInvalidIntervalCount = 0;
PpsClock ppsClock;
bool timTpTargetPending = false;
uint32_t timTpTargetPulseCount = 0;
NormalizedTimestamp timTpTargetUtc = {};
uint32_t timTpTargetQueuedMillis = 0;
uint32_t lastValidTimTpMillis = 0;
uint32_t lastTimTpReportMillis = 0;
bool validTimTpSeen = false;
bool ppsClockConfirmed = false;
bool ppsDiscontinuityReported = false;
bool ntpClockUnavailableReported = false;
bool timTpAutomaticEnabled = false;
bool timTpFreshStreamReady = false;
uint32_t timTpFreshnessReferencePulseCount = 0;
bool timTpPostEdgeDrainPending = false;
uint32_t timTpPostEdgeDrainPulseCount = 0;
uint32_t timTpPostEdgeDrainInvalidIntervalCount = 0;
uint32_t timTpPostEdgeDrainStartedMillis = 0;
bool ppsAcquisitionAttempted = false;
uint32_t lastPpsAcquisitionAttemptMillis = 0;
uint32_t observedInvalidIntervalCount = 0;
bool ppsAcquisitionFailureReported = false;
bool ppsAutomaticEverEnabled = false;
uint8_t consecutivePpsAcquisitionFailures = 0;
uint8_t consecutiveTimTpStreamRestarts = 0;

enum class PpsTimebaseAcquisitionState : uint8_t {
  Idle,
  DisableAutomaticTimTp,
  ConfirmAutomaticTimTpDisabled,
  WaitForDrain,
  DrainBufferedData,
  EnableAutomaticTimTp,
  ConfirmAutomaticTimTpEnabled
};

PpsTimebaseAcquisitionState ppsTimebaseAcquisitionState =
    PpsTimebaseAcquisitionState::Idle;
uint32_t ppsAcquisitionStateStartedMillis = 0;
bool ppsEnableCommandSucceeded = false;

/***** Shared I2C startup recovery *****/
constexpr uint32_t I2C_CLOCK_HZ = 400000;
constexpr uint32_t I2C_PERIPHERAL_POWER_SETTLE_MILLIS = 750;
constexpr uint8_t I2C_RECOVERY_FAILURE_THRESHOLD = 3;
uint8_t consecutiveI2cStartupFailures = 0;

/***** HTTP server init *****/
TimeHttp timeHttp;
FirmwareUpdater firmwareUpdater;
constexpr uint32_t FIRMWARE_INSTALL_PAGE_GRACE_MILLIS = 2000;
bool firmwareUpdateMaintenanceActive = false;
bool firmwareInstallPending = false;
uint32_t firmwareInstallRequestedMillis = 0;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
namespace {

void recordRtcTimestampError(String error);

struct GnssVal8Setting {
  uint32_t key;
  uint8_t value;
  bool required;
  const char* name;
};

constexpr GnssVal8Setting GNSS_VAL8_SETTINGS[] = {
  { UBLOX_CFG_I2C_ENABLED, 1, true, "I2C interface" },
  { UBLOX_CFG_I2CINPROT_UBX, 1, true, "I2C UBX input" },
  { UBLOX_CFG_I2COUTPROT_UBX, 1, true, "I2C UBX output" },
  { UBLOX_CFG_I2CINPROT_NMEA, 0, true, "I2C NMEA input" },
  { UBLOX_CFG_I2COUTPROT_NMEA, 0, true, "I2C NMEA output" },
  { UBLOX_CFG_I2CINPROT_RTCM3X, 0, false, "I2C RTCM input" },
  { UBLOX_CFG_I2COUTPROT_RTCM3X, 0, false, "I2C RTCM output" },
  { UBLOX_CFG_I2CINPROT_SPARTN, 0, false, "I2C SPARTN input" },
  { UBLOX_CFG_SPI_ENABLED, 0, false, "SPI interface" },
  { UBLOX_CFG_USB_ENABLED, 0, false, "USB interface" },
  { UBLOX_CFG_UART1_ENABLED, 0, false, "UART1 interface" },
  { UBLOX_CFG_UART2_ENABLED, 0, false, "UART2 interface" },
  { UBLOX_CFG_I2C_EXTENDEDTIMEOUT, 0, false, "I2C extended timeout" },
  { UBLOX_CFG_SIGNAL_GAL_ENA, 0, false, "Galileo" },
  { UBLOX_CFG_SIGNAL_GAL_E1_ENA, 0, false, "Galileo E1" },
  { UBLOX_CFG_SIGNAL_GAL_E5A_ENA, 0, false, "Galileo E5A" },
  { UBLOX_CFG_SIGNAL_GAL_E5B_ENA, 0, false, "Galileo E5B" },
  { UBLOX_CFG_SIGNAL_GPS_ENA, 1, true, "GPS" },
  { UBLOX_CFG_SIGNAL_GPS_L1CA_ENA, 1, true, "GPS L1 C/A" },
  { UBLOX_CFG_SIGNAL_GPS_L2C_ENA, 1, false, "GPS L2C" },
  { UBLOX_CFG_SIGNAL_GPS_L5_ENA, 1, false, "GPS L5" },
  { UBLOX_CFG_SIGNAL_BDS_ENA, 0, false, "BeiDou" },
  { UBLOX_CFG_SIGNAL_GLO_ENA, 0, false, "GLONASS" },
  { UBLOX_CFG_SIGNAL_SBAS_ENA, 0, false, "SBAS" },
  { UBLOX_CFG_SIGNAL_QZSS_ENA, 0, false, "QZSS" },
  { UBLOX_CFG_RATE_TIMEREF, 0, true, "UTC timing reference" },
  { UBLOX_CFG_MSGOUT_UBX_NAV_PVT_I2C, 0, true, "NAV-PVT startup rate" }
};

bool hasElapsed(const uint32_t now, const uint32_t since, const uint32_t interval) {
  return static_cast<uint32_t>(now - since) >= interval;
}

void discardRtcReceiveBuffer() {
  while (Wire.available() > 0)
    static_cast<void>(Wire.read());
}

bool readRtcRegisters(const uint8_t firstRegister,
                      uint8_t* registers,
                      const uint8_t registerCount) {
  if (registers == nullptr || registerCount == 0)
    return false;

  Wire.beginTransmission(RV1805_ADDR);
  const size_t addressBytesWritten = Wire.write(firstRegister);
  const uint8_t addressStatus = Wire.endTransmission(false);
  if (addressBytesWritten != 1U || addressStatus != 0)
    return false;

  const uint8_t received = Wire.requestFrom(RV1805_ADDR,
                                             registerCount,
                                             static_cast<uint8_t>(true));
  if (received != registerCount ||
      Wire.available() != static_cast<int>(registerCount)) {
    discardRtcReceiveBuffer();
    return false;
  }

  for (uint8_t index = 0; index < registerCount; ++index) {
    const int value = Wire.read();
    if (value < 0) {
      discardRtcReceiveBuffer();
      return false;
    }
    registers[index] = static_cast<uint8_t>(value);
  }

  return true;
}

enum class RtcTimestampReadStatus : uint8_t {
  Success,
  TransportFailure,
  InvalidTimestamp
};

RtcTimestampReadStatus readRtcDateTime(RtcDateTime* timestamp) {
  if (timestamp == nullptr)
    return RtcTimestampReadStatus::InvalidTimestamp;

  uint8_t registers[RV1805_TIMESTAMP_REGISTER_COUNT] = {};
  if (!readRtcRegisters(RV1805_HUNDREDTHS,
                        registers,
                        RV1805_TIMESTAMP_REGISTER_COUNT))
    return RtcTimestampReadStatus::TransportFailure;

  return decodeRv1805Timestamp(registers, sizeof(registers), timestamp)
             ? RtcTimestampReadStatus::Success
             : RtcTimestampReadStatus::InvalidTimestamp;
}

uint32_t gnssRetryBackoff(const uint8_t attempt) {
  constexpr uint32_t RETRY_DELAYS[] = { 1000, 2000, 4000, 8000, 16000, 30000 };
  uint8_t index = attempt > 0 ? static_cast<uint8_t>(attempt - 1) : 0;
  if (index >= std::size(RETRY_DELAYS))
    index = static_cast<uint8_t>(std::size(RETRY_DELAYS) - 1);
  return RETRY_DELAYS[index] > GNSS_INITIALIZATION_RETRY_MAX_MILLIS
             ? GNSS_INITIALIZATION_RETRY_MAX_MILLIS
             : RETRY_DELAYS[index];
}

const char* gnssInitializationStateName(const GnssInitializationState state) {
  switch (state) {
    case GnssInitializationState::WaitingForPower: return "waiting for power";
    case GnssInitializationState::Probe: return "probing";
    case GnssInitializationState::ConfigureVal8: return "configuring interfaces";
    case GnssInitializationState::ConfigureDynamicModel: return "configuring dynamic model";
    case GnssInitializationState::ConfigureMeasurementRate: return "configuring measurement rate";
    case GnssInitializationState::ConfigureNavigationRate: return "configuring navigation rate";
    case GnssInitializationState::ConfigureTimePulseTiming: return "configuring TP1 timing";
    case GnssInitializationState::ConfigureTimePulseControls: return "configuring TP1 controls";
    case GnssInitializationState::ConfigureNavPvtCallback: return "enabling NAV-PVT";
    case GnssInitializationState::WaitForInitialNavPvt: return "waiting for NAV-PVT";
    case GnssInitializationState::ConfigureNavPvtRate: return "setting NAV-PVT rate";
    case GnssInitializationState::ConfirmNavPvtRate: return "confirming NAV-PVT rate";
    case GnssInitializationState::ReadModuleInformation: return "reading module information";
    case GnssInitializationState::ReadLeapSeconds: return "reading leap seconds";
    case GnssInitializationState::Ready: return "ready";
  }
  return "unknown";
}

void setGnssInitializationState(const GnssInitializationState state,
                                const uint32_t delayMillis = 0) {
  gnssInitializationState = state;
  gnssStateStartedMillis = millis();
  gnssStateDelayMillis = delayMillis;
  gnssInitializationAttempts = 0;
}

void restartGnssInitialization(const char* reason, const uint8_t backoffAttempt) {
  gnssReady = false;
  gnssDetected = false;
  gnssProbeFailureReported = true;
  gnssConfigurationIndex = 0;
  gnssStatusCache.clear();
  gnssStatusDirty = true;
  timTpAutomaticEnabled = false;
  ppsTimebaseAcquisitionState = PpsTimebaseAcquisitionState::Idle;
  ppsAcquisitionAttempted = false;
  consecutivePpsAcquisitionFailures = 0;
  consecutiveTimTpStreamRestarts = 0;
  invalidatePpsTimebase();
  gnssInitializationAttempts = backoffAttempt;
  gnssInitializationState = GnssInitializationState::Probe;
  gnssStateStartedMillis = millis();
  gnssStateDelayMillis = gnssRetryBackoff(backoffAttempt);
  recordError(reason);
}

void scheduleGnssInitializationRetry(const char* reason) {
  gnssReady = false;
  invalidatePpsTimebase();
  timTpAutomaticEnabled = false;
  ppsTimebaseAcquisitionState = PpsTimebaseAcquisitionState::Idle;

  if (lastReportedGnssFailureState != gnssInitializationState) {
    lastReportedGnssFailureState = gnssInitializationState;
    recordError(String("GNSS ") + gnssInitializationStateName(gnssInitializationState) +
                " failed: " + reason + "; retrying");
  }

  if (gnssInitializationAttempts < UINT8_MAX)
    ++gnssInitializationAttempts;

  // A receiver can disappear after begin succeeds. Re-probe after repeated
  // failures instead of retrying one configuration command forever.
  if (gnssInitializationState != GnssInitializationState::Probe &&
      gnssInitializationAttempts >= MAX_GNSS_CONFIGURATION_FAILURES) {
    restartGnssInitialization("Repeated GNSS configuration failures; restarting receiver initialization",
                              gnssInitializationAttempts);
    return;
  }
  gnssStateStartedMillis = millis();
  gnssStateDelayMillis = gnssRetryBackoff(gnssInitializationAttempts);
}

void noteI2cInitializationResult(const bool success) {
  if (success) {
    consecutiveI2cStartupFailures = 0;
    return;
  }

  // Reinitializing a shared, working bus would disrupt the other peripherals.
  // Reset the controller only when no I2C device has initialized successfully.
  if (gnssDetected || rtcAvailable || oledAvailable)
    return;
  if (consecutiveI2cStartupFailures < UINT8_MAX)
    ++consecutiveI2cStartupFailures;
  if (consecutiveI2cStartupFailures < I2C_RECOVERY_FAILURE_THRESHOLD)
    return;

  consecutiveI2cStartupFailures = 0;
  Wire.begin();
  Wire.setClock(I2C_CLOCK_HZ);
  recordError("All I2C peripherals were unavailable; the I2C controller was reinitialized");
}

bool ethernetElapsed(const uint32_t now, const uint32_t since, const uint32_t interval) {
  return hasElapsed(now, since, interval);
}

uint32_t ethernetRetryBackoff(const uint8_t attempt) {
  constexpr uint32_t RETRY_DELAYS[] = { 1000, 2000, 4000, 8000, 16000, 30000 };
  uint8_t index = attempt > 0 ? static_cast<uint8_t>(attempt - 1) : 0;
  if (index >= std::size(RETRY_DELAYS))
    index = static_cast<uint8_t>(std::size(RETRY_DELAYS) - 1);
  return RETRY_DELAYS[index];
}

uint8_t incrementEthernetAttempt(const uint8_t attempt) {
  return attempt < UINT8_MAX ? static_cast<uint8_t>(attempt + 1) : UINT8_MAX;
}

void setEthernetState(const EthernetServiceState state) {
  ethernetServiceState = state;
  ethernetStateStartedMillis = millis();
  ethernetRetryDelayMillis = 0;
}

void scheduleEthernetRetry(const EthernetServiceState state, const uint8_t attempt) {
  ethernetRetryState = state;
  ethernetRetryDelayMillis = ethernetRetryBackoff(attempt);
  ethernetStateStartedMillis = millis();
  ethernetServiceState = EthernetServiceState::RetryBackoff;
}

void setConfiguredNetworkStrings() {
  if (activeEthernetDhcp) {
    strLocalIp = "0.0.0.0";
    strSubnet = "0.0.0.0";
    strDns1Ip = "0.0.0.0";
    strDns2Ip = "0.0.0.0";
    strGatewayIp = "0.0.0.0";
    return;
  }

  strLocalIp = properties.generateIpString(activeEthernetLocalIp);
  strSubnet = properties.generateIpString(activeEthernetSubnet);
  strDns1Ip = properties.generateIpString(activeEthernetDns1Ip);
  strDns2Ip = properties.generateIpString(activeEthernetDns2Ip);
  strGatewayIp = properties.generateIpString(activeEthernetGatewayIp);
}

void updateNetworkStringsFromEthernet() {
  strLocalIp = properties.generateIpString(Ethernet.localIP());
  strSubnet = properties.generateIpString(Ethernet.subnetMask());
  strDns1Ip = properties.generateIpString(Ethernet.dnsServerIP());
  strDns2Ip = "0.0.0.0";
  strGatewayIp = properties.generateIpString(Ethernet.gatewayIP());
}

bool ipAddressesMatch(const IPAddress left, const IPAddress right) {
  for (uint8_t index = 0; index < 4; ++index) {
    if (left[index] != right[index])
      return false;
  }
  return true;
}

bool isZeroIpAddress(const IPAddress address) {
  return address[0] == 0 && address[1] == 0 && address[2] == 0 && address[3] == 0;
}

bool ethernetMacAddressIsValid() {
  uint8_t currentMac[sizeof(mac)] = {};
  Ethernet.MACAddress(currentMac);
  for (std::size_t index = 0; index < sizeof(mac); ++index) {
    if (currentMac[index] != mac[index])
      return false;
  }
  return true;
}

void captureAppliedEthernetConfiguration() {
  appliedEthernetLocalIp = Ethernet.localIP();
  appliedEthernetSubnet = Ethernet.subnetMask();
  appliedEthernetGatewayIp = Ethernet.gatewayIP();
  appliedEthernetConfigurationValid =
      !isZeroIpAddress(appliedEthernetLocalIp) &&
      !isZeroIpAddress(appliedEthernetSubnet);
}

bool isW5500Responsive() {
  SPIClass* ethernetSpi = Ethernet.spi();
  if (ethernetSpi == nullptr)
    return false;

  ethernetSpi->beginTransaction(SPI_ETHERNET_SETTINGS);
  const uint8_t version = W5100.readVERSIONR_W5500();
  ethernetSpi->endTransaction();
  return version == 4;
}

bool ethernetConfigurationIsValid() {
  if (!appliedEthernetConfigurationValid || !ethernetMacAddressIsValid())
    return false;

  return ipAddressesMatch(Ethernet.localIP(), appliedEthernetLocalIp) &&
         ipAddressesMatch(Ethernet.subnetMask(), appliedEthernetSubnet) &&
         ipAddressesMatch(Ethernet.gatewayIP(), appliedEthernetGatewayIp);
}

uint8_t findNtpSocketNumber() {
  SPIClass* ethernetSpi = Ethernet.spi();
  if (ethernetSpi == nullptr)
    return MAX_SOCK_NUM;

  uint8_t matchingSocket = MAX_SOCK_NUM;
  ethernetSpi->beginTransaction(SPI_ETHERNET_SETTINGS);
  for (uint8_t socketNumber = 0; socketNumber < MAX_SOCK_NUM; ++socketNumber) {
    if (W5100.readSnSR(socketNumber) == SnSR::UDP &&
        W5100.readSnPORT(socketNumber) == ntpPort) {
      if (matchingSocket < MAX_SOCK_NUM) {
        matchingSocket = MAX_SOCK_NUM;
        break;
      }
      matchingSocket = socketNumber;
    }
  }
  ethernetSpi->endTransaction();
  return matchingSocket;
}

bool ntpSocketIsHealthy() {
  if (!ntpUdpBound || udp.localPort() != ntpPort || ntpSocketNumber >= MAX_SOCK_NUM)
    return false;
  return findNtpSocketNumber() == ntpSocketNumber;
}

bool httpSocketIsHealthy() {
  // A normal TCP handshake temporarily changes the only LISTEN socket to
  // ESTABLISHED before EthernetServer::available() opens a replacement. Treat
  // any live, server-owned port-80 socket as healthy during that transition.
  SPIClass* ethernetSpi = Ethernet.spi();
  ethernetSpi->beginTransaction(SPI_ETHERNET_SETTINGS);
  bool activeSocketFound = false;
  for (uint8_t socketNumber = 0; socketNumber < MAX_SOCK_NUM; ++socketNumber) {
    if (EthernetServer::server_port[socketNumber] != HTTP_PORT)
      continue;

    const uint8_t socketStatus = W5100.readSnSR(socketNumber);
    if (socketStatus == SnSR::LISTEN || socketStatus == SnSR::SYNRECV ||
        socketStatus == SnSR::ESTABLISHED || socketStatus == SnSR::CLOSE_WAIT) {
      activeSocketFound = true;
      break;
    }
  }
  ethernetSpi->endTransaction();
  return activeSocketFound;
}

void pulseW5500Reset() {
  digitalWrite(W5500_RESET_PIN, LOW);
  delayMicroseconds(W5500_RESET_LOW_MICROS);
  digitalWrite(W5500_RESET_PIN, HIGH);
}

void clearHttpServerSocketBookkeeping() {
  for (uint8_t socketNumber = 0; socketNumber < MAX_SOCK_NUM; ++socketNumber)
    EthernetServer::server_port[socketNumber] = 0;
}

void stopHttpServerSockets() {
  for (uint8_t socketNumber = 0; socketNumber < MAX_SOCK_NUM; ++socketNumber) {
    if (EthernetServer::server_port[socketNumber] != HTTP_PORT)
      continue;

    EthernetClient client(socketNumber);
    client.setConnectionTimeout(50);
    client.stop();
    EthernetServer::server_port[socketNumber] = 0;
  }
}

void stopEthernetServices(const bool controllerResponsive) {
  ethernetOnline = false;
  if (controllerResponsive) {
    udp.stop();
    stopHttpServerSockets();
  }
  ntpUdpBound = false;
  ntpSocketNumber = MAX_SOCK_NUM;
}

bool applyEthernetConfiguration() {
  appliedEthernetConfigurationValid = false;
  if (activeEthernetDhcp) {
    Serial.println("Requesting a DHCP lease");
    if (Ethernet.begin(mac, DHCP_TIMEOUT_MILLIS, DHCP_RESPONSE_TIMEOUT_MILLIS) == 0)
      return false;
    updateNetworkStringsFromEthernet();
    captureAppliedEthernetConfiguration();
  }
  else {
    Ethernet.begin(mac,
                   activeEthernetLocalIp,
                   activeEthernetDns1Ip,
                   activeEthernetGatewayIp,
                   activeEthernetSubnet);
    setConfiguredNetworkStrings();
    appliedEthernetLocalIp = activeEthernetLocalIp;
    appliedEthernetSubnet = activeEthernetSubnet;
    appliedEthernetGatewayIp = activeEthernetGatewayIp;
    appliedEthernetConfigurationValid =
        !isZeroIpAddress(appliedEthernetLocalIp) &&
        !isZeroIpAddress(appliedEthernetSubnet);
  }

  return isW5500Responsive() && ethernetConfigurationIsValid();
}

bool startEthernetServices() {
  const bool ntpReady = firmwareUpdateMaintenanceActive || bindNtpUdpSocket();
  if (!httpSocketIsHealthy())
    httpServer.begin();
  const bool httpReady = httpSocketIsHealthy();
  return ntpReady && httpReady;
}

void ethernetServicesAreOnline() {
  const bool firstSuccessfulStartup = !ethernetEverOnline;
  ethernetOnline = true;
  ethernetEverOnline = true;
  ethernetSocketRepairAttempts = 0;
  ethernetServiceRepairAttempts = 0;
  ethernetDhcpRetryAttempts = 0;
  lastEthernetHealthCheckMillis = millis();
  lastDhcpMaintainMillis = millis();
  ethernetOnlineSinceMillis = millis();
  if (firstSuccessfulStartup) {
    ethernetSocketRecoveryCycles = 0;
    ethernetServiceRecoveryCycles = 0;
    w5500ResetAttempts = 0;
  }
  setEthernetState(EthernetServiceState::Online);

  // Keep the passive configuration snapshot current without running the setup
  // page's detailed GNSS query from this service path.
  suppressDetailedGnssConfigQueries = true;
  getDeviceConfig();
  suppressDetailedGnssConfigQueries = false;

  Serial.print("Ethernet online at ");
  Serial.println(strLocalIp);
  if (!firmwareUpdateMaintenanceActive)
    Serial.println("NTP UDP socket listening on port 123");
  Serial.println("HTTP server listening on port 80");
  addLog("Ethernet services online at " + strLocalIp);
}

void waitForEthernetLink(const char* message) {
  ethernetOnline = false;
  ntpUdpBound = false;
  ntpSocketNumber = MAX_SOCK_NUM;
  ethernetRecoveryReason = message;
  lastEthernetPollMillis = 0;
  setEthernetState(EthernetServiceState::WaitingForLink);
  Serial.println(message);
}

void restartTeensyAfterEthernetFailure() {
  Serial.println("ERROR: W5500 recovery failed repeatedly; restarting Teensy");
  Serial.flush();
  delay(10);
  SCB_AIRCR = 0x05FA0004;
  while (true) {
    // Wait for the processor reset.
  }
}

void requestW5500Reset(const char* reason) {
  ethernetOnline = false;
  ntpUdpBound = false;
  ntpSocketNumber = MAX_SOCK_NUM;
  ethernetRecoveryReason = reason;

  Serial.print("W5500 recovery requested: ");
  Serial.println(reason);

  if (ethernetRestartArmed && w5500ResetAttempts >= MAX_W5500_RESET_ATTEMPTS) {
    restartTeensyAfterEthernetFailure();
    return;
  }

  scheduleEthernetRetry(EthernetServiceState::HardwareReset,
                        incrementEthernetAttempt(w5500ResetAttempts));
}

void requestEthernetServiceRecovery(const char* reason) {
  ethernetOnline = false;
  ethernetRecoveryReason = reason;
  ethernetServiceRepairAttempts = 0;
  ethernetServiceRecoveryCycles = incrementEthernetAttempt(ethernetServiceRecoveryCycles);
  if (ethernetServiceRecoveryCycles > MAX_SERVICE_REPAIR_ATTEMPTS) {
    requestW5500Reset("repeated service recovery cycles were exhausted");
    return;
  }
  setEthernetState(EthernetServiceState::ReconfigureServices);
}

} // namespace

void requestEthernetSocketRecovery(const char* reason, const bool ntpSocketFailed) {
  if (ntpSocketFailed) {
    ntpUdpBound = false;
    ntpSocketNumber = MAX_SOCK_NUM;
  }
  if (ethernetServiceState != EthernetServiceState::Online)
    return;

  // Keep a healthy NTP socket serving while repairing HTTP alone. Full
  // configuration or controller recovery still takes both services offline.
  ethernetSocketRecoveryCycles = incrementEthernetAttempt(ethernetSocketRecoveryCycles);
  if (ethernetSocketRecoveryCycles > MAX_SOCKET_REPAIR_ATTEMPTS) {
    requestEthernetServiceRecovery("repeated socket recovery cycles were exhausted");
    return;
  }
  if (ntpSocketFailed)
    ethernetOnline = false;
  ethernetRecoveryReason = reason;
  ethernetSocketRepairAttempts = 0;
  setEthernetState(EthernetServiceState::RepairSockets);
  Serial.print("Ethernet socket recovery requested: ");
  Serial.println(reason);
}

void setFirmwareUpdateMaintenance(const bool active) {
  firmwareUpdateMaintenanceActive = active;
  if (active) {
    // Leave the current HTTP connection and listener intact, but release UDP
    // socket 123 while the synchronous firmware upload owns loop().
    udp.stop();
    ntpUdpBound = false;
    ntpSocketNumber = MAX_SOCK_NUM;
    return;
  }

  firmwareInstallPending = false;
  // This was an intentional socket release, not a W5500 failure, so restore it
  // through the nonblocking repair state without consuming a recovery attempt.
  ethernetOnline = false;
  ethernetRecoveryReason = "firmware upload ended; restoring NTP";
  ethernetSocketRepairAttempts = 0;
  setEthernetState(EthernetServiceState::RepairSockets);
}

void installFirmwareUpdate() {
  if (!firmwareUpdater.readyToInstall()) {
    recordError("Validated firmware was not ready when installation was requested");
    setFirmwareUpdateMaintenance(false);
    return;
  }

  // Return from the upload request so its JavaScript can render the reboot-wait
  // page. Installation remains guaranteed even if rendering fails because loop
  // services this pending request after a short grace time.
  firmwareInstallPending = true;
  firmwareInstallRequestedMillis = millis();
}

void serviceFirmwareInstall() {
  if (!firmwareInstallPending ||
      static_cast<uint32_t>(millis() - firmwareInstallRequestedMillis) <
          FIRMWARE_INSTALL_PAGE_GRACE_MILLIS)
    return;

  firmwareInstallPending = false;
  // No SPI/W5500 or GNSS work is permitted after flash replacement begins.
  // The updater masks interrupts and reboots without returning.
  detachInterrupt(digitalPinToInterrupt(TIME_PULSE_PIN));
  udp.stop();
  ntpUdpBound = false;
  ntpSocketNumber = MAX_SOCK_NUM;
  firmwareUpdater.installAndReboot();
}

void initializeEthernetService() {
  // Set inactive levels before changing the pins to outputs to avoid reset or
  // chip-select glitches during startup.
  digitalWrite(W5500_CS_PIN, HIGH);
  pinMode(W5500_CS_PIN, OUTPUT);
  digitalWrite(W5500_RESET_PIN, HIGH);
  pinMode(W5500_RESET_PIN, OUTPUT);
  Ethernet.init(W5500_CS_PIN);

  // Preserve the existing web behavior: saved network settings take effect
  // only after a restart, not during a health check or recovery attempt.
  activeEthernetDhcp = properties.isDhcp();
  activeEthernetLocalIp = properties.getLocalIp();
  activeEthernetSubnet = properties.getSubnet();
  activeEthernetDns1Ip = properties.getDns1Ip();
  activeEthernetDns2Ip = properties.getDns2Ip();
  activeEthernetGatewayIp = properties.getGatewayIp();
  appliedEthernetConfigurationValid = false;

  pulseW5500Reset();
  clearHttpServerSocketBookkeeping();
  setConfiguredNetworkStrings();
  ethernetOnline = false;
  ntpUdpBound = false;
  ntpSocketNumber = MAX_SOCK_NUM;
  ethernetRecoveryReason = "startup";
  ethernetStateStartedMillis = millis();
  ethernetRetryDelayMillis = W5500_RESET_RELEASE_MILLIS;
  ethernetServiceState = EthernetServiceState::ControllerInitialize;
  Serial.println("W5500 reset released; Ethernet startup will continue in loop");
}

void serviceEthernet() {
  const uint32_t now = millis();

  if (ethernetServiceState == EthernetServiceState::RetryBackoff) {
    if (ethernetElapsed(now, ethernetStateStartedMillis, ethernetRetryDelayMillis))
      setEthernetState(ethernetRetryState);
    return;
  }

  switch (ethernetServiceState) {
    case EthernetServiceState::ControllerInitialize:
      if (!ethernetElapsed(now, ethernetStateStartedMillis, ethernetRetryDelayMillis))
        return;
      if (W5100.init() == 0 || !isW5500Responsive()) {
        requestW5500Reset("controller did not respond during initialization");
        return;
      }
      waitForEthernetLink("W5500 detected; waiting for Ethernet link");
      return;

    case EthernetServiceState::WaitingForLink:
      if (!ethernetElapsed(now, lastEthernetPollMillis, ETHERNET_LINK_POLL_MILLIS))
        return;
      lastEthernetPollMillis = now;
      if (!isW5500Responsive()) {
        requestW5500Reset("controller stopped responding while waiting for link");
        return;
      }
      if (Ethernet.linkStatus() == LinkON) {
        ethernetSocketRepairAttempts = 0;
        ethernetServiceRepairAttempts = 0;
        ethernetDhcpRetryAttempts = 0;
        ethernetSocketRecoveryCycles = 0;
        ethernetServiceRecoveryCycles = 0;
        ethernetLinkStableSinceMillis = now;
        setEthernetState(EthernetServiceState::LinkStabilizing);
        Serial.println("Ethernet link detected; waiting for it to stabilize");
        return;
      }
      // VERSIONR can remain readable even when a cold-powered W5500 never
      // completes PHY/link initialization. Do not wait forever on a stale
      // LinkOFF/Unknown readback while the jack LEDs show physical activity.
      if (ethernetElapsed(now,
                          ethernetStateStartedMillis,
                          ETHERNET_LINK_WAIT_RESET_MILLIS))
        requestW5500Reset("link status did not become ready after controller initialization");
      return;

    case EthernetServiceState::LinkStabilizing:
      if (!ethernetElapsed(now, lastEthernetPollMillis, ETHERNET_LINK_POLL_MILLIS))
        return;
      lastEthernetPollMillis = now;
      if (!isW5500Responsive()) {
        requestW5500Reset("controller stopped responding during link negotiation");
        return;
      }
      if (Ethernet.linkStatus() != LinkON) {
        waitForEthernetLink("Ethernet link dropped before stabilization; waiting");
        return;
      }
      if (ethernetElapsed(now, ethernetLinkStableSinceMillis, ETHERNET_LINK_STABLE_MILLIS)) {
        // A reconnect gets fresh UDP and HTTP sockets. Initial startup has no
        // sockets to close and can proceed directly to configuration.
        setEthernetState(ethernetEverOnline ? EthernetServiceState::ReconfigureServices
                                           : EthernetServiceState::ConfigureNetwork);
      }
      return;

    case EthernetServiceState::ConfigureNetwork:
      if (!isW5500Responsive()) {
        requestW5500Reset("controller stopped responding before network configuration");
        return;
      }
      if (Ethernet.linkStatus() != LinkON) {
        waitForEthernetLink("Ethernet link lost before network configuration; waiting");
        return;
      }
      if (applyEthernetConfiguration()) {
        setEthernetState(EthernetServiceState::StartServices);
        return;
      }
      if (!isW5500Responsive()) {
        requestW5500Reset("controller stopped responding during network configuration");
        return;
      }
      if (Ethernet.linkStatus() != LinkON) {
        waitForEthernetLink("Ethernet link lost during network configuration; waiting");
        return;
      }
      Serial.println("Network configuration failed; retrying");
      // A DHCP server outage is external to the W5500. Keep retrying with a
      // capped delay while the controller and physical link remain healthy.
      if (activeEthernetDhcp) {
        ethernetDhcpRetryAttempts = incrementEthernetAttempt(ethernetDhcpRetryAttempts);
        scheduleEthernetRetry(EthernetServiceState::ConfigureNetwork,
                              ethernetDhcpRetryAttempts);
      }
      else {
        ethernetServiceRepairAttempts = incrementEthernetAttempt(ethernetServiceRepairAttempts);
        if (ethernetServiceRepairAttempts >= MAX_SERVICE_REPAIR_ATTEMPTS)
          requestW5500Reset("network configuration retries were exhausted");
        else
          scheduleEthernetRetry(EthernetServiceState::ConfigureNetwork,
                                ethernetServiceRepairAttempts);
      }
      return;

    case EthernetServiceState::StartServices:
      if (!isW5500Responsive()) {
        requestW5500Reset("controller stopped responding before service startup");
        return;
      }
      if (Ethernet.linkStatus() != LinkON) {
        waitForEthernetLink("Ethernet link lost before service startup; waiting");
        return;
      }
      if (startEthernetServices()) {
        ethernetServicesAreOnline();
        return;
      }
      ethernetOnline = ntpSocketIsHealthy();
      ethernetSocketRepairAttempts = incrementEthernetAttempt(ethernetSocketRepairAttempts);
      Serial.println("Ethernet service startup failed; retrying sockets");
      scheduleEthernetRetry(EthernetServiceState::RepairSockets,
                            ethernetSocketRepairAttempts);
      return;

    case EthernetServiceState::Online: {
      if (!ethernetElapsed(now, lastEthernetHealthCheckMillis, ETHERNET_HEALTH_CHECK_MILLIS))
        return;
      lastEthernetHealthCheckMillis = now;

      if (!isW5500Responsive()) {
        requestW5500Reset("live controller health check failed");
        return;
      }
      if (Ethernet.linkStatus() != LinkON) {
        recordError("Ethernet link lost; waiting for reconnection");
        waitForEthernetLink("Ethernet link is down; waiting for reconnection");
        return;
      }
      if (!ethernetConfigurationIsValid()) {
        requestEthernetServiceRecovery("network configuration readback failed");
        return;
      }

      const bool ntpHealthy = firmwareUpdateMaintenanceActive || ntpSocketIsHealthy();
      const bool httpHealthy = httpSocketIsHealthy();
      if (!ntpHealthy || !httpHealthy) {
        requestEthernetSocketRecovery(!ntpHealthy ? "NTP socket health check failed"
                                                 : "HTTP listener health check failed",
                                      !ntpHealthy);
        return;
      }

      if (activeEthernetDhcp &&
          ethernetElapsed(now, lastDhcpMaintainMillis, DHCP_MAINTAIN_MILLIS)) {
        lastDhcpMaintainMillis = now;
        const int maintainResult = Ethernet.maintain();
        if (maintainResult == DHCP_CHECK_RENEW_FAIL ||
            maintainResult == DHCP_CHECK_REBIND_FAIL) {
          ethernetOnline = false;
          ethernetRecoveryReason = "DHCP lease maintenance failed";
          ethernetServiceRepairAttempts = 0;
          setEthernetState(EthernetServiceState::ReconfigureServices);
          return;
        }
        if (maintainResult == DHCP_CHECK_RENEW_OK ||
            maintainResult == DHCP_CHECK_REBIND_OK) {
          captureAppliedEthernetConfiguration();
          updateNetworkStringsFromEthernet();
        }
      }

      // A controller recovery counts as successful only after a sustained
      // healthy period. Rapid online/failure flapping therefore still
      // escalates through the approved recovery tiers.
      if (w5500ResetAttempts > 0 &&
          ethernetElapsed(now, ethernetOnlineSinceMillis, ETHERNET_RECOVERY_STABLE_MILLIS))
        w5500ResetAttempts = 0;
      if (ethernetElapsed(now, ethernetOnlineSinceMillis, ETHERNET_RECOVERY_STABLE_MILLIS)) {
        ethernetSocketRepairAttempts = 0;
        ethernetServiceRepairAttempts = 0;
        ethernetSocketRecoveryCycles = 0;
        ethernetServiceRecoveryCycles = 0;
        ethernetRestartArmed = true;
      }
      return;
    }

    case EthernetServiceState::RepairSockets: {
      if (!isW5500Responsive()) {
        requestW5500Reset("controller stopped responding during socket repair");
        return;
      }
      if (Ethernet.linkStatus() != LinkON) {
        waitForEthernetLink("Ethernet link lost during socket repair; waiting");
        return;
      }

      bool ntpReady = firmwareUpdateMaintenanceActive || ntpSocketIsHealthy();
      if (!ntpReady)
        ntpReady = bindNtpUdpSocket();
      bool httpReady = httpSocketIsHealthy();
      if (!httpReady) {
        httpServer.begin();
        httpReady = httpSocketIsHealthy();
      }
      if (ntpReady && httpReady) {
        ethernetServicesAreOnline();
        return;
      }

      ethernetOnline = ntpReady;
      ethernetSocketRepairAttempts = incrementEthernetAttempt(ethernetSocketRepairAttempts);
      if (ethernetSocketRepairAttempts >= MAX_SOCKET_REPAIR_ATTEMPTS) {
        requestEthernetServiceRecovery("socket repair attempts were exhausted");
      }
      else {
        scheduleEthernetRetry(EthernetServiceState::RepairSockets,
                              ethernetSocketRepairAttempts);
      }
      return;
    }

    case EthernetServiceState::ReconfigureServices: {
      if (!isW5500Responsive()) {
        requestW5500Reset("controller stopped responding during service recovery");
        return;
      }
      if (Ethernet.linkStatus() != LinkON) {
        waitForEthernetLink("Ethernet link lost during service recovery; waiting");
        return;
      }

      stopEthernetServices(true);
      const bool configurationReady = applyEthernetConfiguration();
      const bool servicesReady = configurationReady && startEthernetServices();
      if (servicesReady) {
        ethernetServicesAreOnline();
        return;
      }

      if (!isW5500Responsive()) {
        requestW5500Reset("controller stopped responding while restarting services");
        return;
      }
      if (Ethernet.linkStatus() != LinkON) {
        waitForEthernetLink("Ethernet link lost while restarting services; waiting");
        return;
      }

      if (activeEthernetDhcp && !configurationReady) {
        ethernetDhcpRetryAttempts = incrementEthernetAttempt(ethernetDhcpRetryAttempts);
        scheduleEthernetRetry(EthernetServiceState::ReconfigureServices,
                              ethernetDhcpRetryAttempts);
        return;
      }

      ethernetDhcpRetryAttempts = 0;
      ethernetServiceRepairAttempts = incrementEthernetAttempt(ethernetServiceRepairAttempts);
      if (ethernetServiceRepairAttempts >= MAX_SERVICE_REPAIR_ATTEMPTS)
        requestW5500Reset("service recovery retries were exhausted");
      else
        scheduleEthernetRetry(EthernetServiceState::ReconfigureServices,
                              ethernetServiceRepairAttempts);
      return;
    }

    case EthernetServiceState::HardwareReset:
      ethernetOnline = false;
      ntpUdpBound = false;
      ntpSocketNumber = MAX_SOCK_NUM;
      clearHttpServerSocketBookkeeping();
      pulseW5500Reset();
      w5500ResetAttempts = incrementEthernetAttempt(w5500ResetAttempts);
      ethernetStateStartedMillis = millis();
      ethernetRetryDelayMillis = W5500_RESET_RELEASE_MILLIS;
      ethernetServiceState = EthernetServiceState::ControllerInitialize;
      Serial.print("W5500 hardware reset attempt ");
      Serial.print(w5500ResetAttempts);
      Serial.print(": ");
      Serial.println(ethernetRecoveryReason);
      return;

    case EthernetServiceState::RetryBackoff:
      return;
  }
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void initializePeripheralServices() {
  const uint32_t now = millis();
  // Ethernet begins immediately. Give cold-powered I2C peripherals time to
  // finish their own power-on reset before the first probe.
  lastRtcInitializationAttemptMillis =
      now - (RTC_INITIALIZATION_RETRY_MILLIS -
             I2C_PERIPHERAL_POWER_SETTLE_MILLIS);
  lastOledInitializationAttemptMillis =
      now - (OLED_INITIALIZATION_RETRY_MILLIS -
             I2C_PERIPHERAL_POWER_SETTLE_MILLIS);
  gnssStateStartedMillis = now;
  gnssStateDelayMillis = GNSS_POWER_SETTLE_MILLIS;
  gnssInitializationState = GnssInitializationState::WaitingForPower;

  // Capture TP1 immediately. Pulses are ignored for synchronized NTP until the
  // GNSS state machine confirms the configuration and fresh TIM-TP labels.
  pinMode(TIME_PULSE_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(TIME_PULSE_PIN), timePulseInterrupt, RISING);
}

void serviceRtcInitialization() {
  if (rtcAvailable)
    return;

  const uint32_t now = millis();
  if (!hasElapsed(now,
                  lastRtcInitializationAttemptMillis,
                  RTC_INITIALIZATION_RETRY_MILLIS))
    return;
  lastRtcInitializationAttemptMillis = now;

  const bool detected = rtc.begin(Wire);
  if (!detected) {
    noteI2cInitializationResult(false);
    if (!rtcInitializationFailureReported) {
      rtcInitializationFailureReported = true;
      recordError("Real time clock (RTC) was not found; retrying");
    }
    return;
  }

  rtcHundredthsAvailable = configureRtcXtOscillator();
  if (!rtcHundredthsAvailable)
    recordError("RTC crystal oscillator configuration failed; hundredths are unavailable");
  rtc.clearInterrupts();
  rtc.setStaticPowerSwitchOutput(false);
  rtc.setPowerSwitchLock(true);
  rtc.setAlarmMode(0);
  rtc.set24Hour();
  uint8_t control1 = 0;
  RtcDateTime initialRtcTime = {};
  const bool modeVerified = readRtcRegisters(RV1805_CTRL1, &control1, 1) &&
                            (control1 & (1U << CTRL1_12_24)) == 0;
  const RtcTimestampReadStatus initialTimeStatus = readRtcDateTime(&initialRtcTime);
  if (!modeVerified ||
      initialTimeStatus == RtcTimestampReadStatus::TransportFailure) {
    noteI2cInitializationResult(false);
    if (!rtcInitializationFailureReported) {
      rtcInitializationFailureReported = true;
      recordError("RTC initialization did not complete; retrying");
    }
    return;
  }

  noteI2cInitializationResult(true);
  rtcAvailable = true;
  rtcReadErrorReported = initialTimeStatus == RtcTimestampReadStatus::InvalidTimestamp;
  if (rtcReadErrorReported)
    recordError("RTC contained an invalid timestamp; waiting for UTC TP1 synchronization");
  setRtc();

  if (rtcInitializationFailureReported)
    addLog("Real time clock recovered after startup retry");
  else
    Serial.println("Real time clock initialized");
  rtcInitializationFailureReported = false;
}

void serviceDisplayInitialization() {
  const bool displayEnabled = properties.getDisplayOn() == 1;
  const bool displayAlternate = properties.getDisplayAlternate() == 1;
  if (!displayEnabled) {
    if (oledAvailable) {
      // Send one final command to turn off a display which was enabled at
      // runtime. No further OLED I2C traffic is allowed while it is off.
      myOLED.displayPower(false);
      oledAvailable = false;
    }
    appliedDisplayAlternate = false;
    isDisplayInverted = false;
    return;
  }

  if (oledAvailable) {
    if (displayAlternate != appliedDisplayAlternate) {
      // Apply a live Alternate Display change immediately. Restart the status
      // interval so the new state remains visible for one complete interval.
      appliedDisplayAlternate = displayAlternate;
      isDisplayInverted = displayAlternate;
      myOLED.invert(isDisplayInverted);
      refreshTimerMs = 0;
    }
    return;
  }

  const uint32_t now = millis();
  if (!hasElapsed(now,
                  lastOledInitializationAttemptMillis,
                  OLED_INITIALIZATION_RETRY_MILLIS))
    return;
  lastOledInitializationAttemptMillis = now;

  const bool detected = myOLED.begin(Wire);
  noteI2cInitializationResult(detected);
  if (!detected) {
    if (!oledInitializationFailureReported) {
      oledInitializationFailureReported = true;
      recordError("OLED was not found; retrying without blocking other services");
    }
    return;
  }

  oledAvailable = true;
  serviceCachedGnssStatus();
  displaySettings();
  myOLED.displayPower(true);
  appliedDisplayAlternate = displayAlternate;
  // Start a newly initialized display in normal polarity. When alternation is
  // enabled, the first Status Frequency interval changes it to inverted.
  isDisplayInverted = false;
  myOLED.invert(isDisplayInverted);
  refreshTimerMs = 0;

  if (oledInitializationFailureReported)
    addLog("OLED recovered after startup retry");
  else
    Serial.println("OLED initialized");
  oledInitializationFailureReported = false;
}

void serviceGnssInitialization() {
  if (gnssReady)
    return;

  const uint32_t now = millis();
  if (!hasElapsed(now, gnssStateStartedMillis, gnssStateDelayMillis))
    return;
  gnssStateDelayMillis = 0;

  switch (gnssInitializationState) {
    case GnssInitializationState::WaitingForPower:
      setGnssInitializationState(GnssInitializationState::Probe);
      return;

    case GnssInitializationState::Probe: {
      const bool detected = myGNSS.begin(Wire,
                                         kUBLOXGNSSDefaultAddress,
                                         GNSS_COMMAND_MAX_WAIT_MILLIS,
                                         false);
      noteI2cInitializationResult(detected);
      if (!detected) {
        gnssDetected = false;
        gnssProbeFailureReported = true;
        scheduleGnssInitializationRetry("receiver did not respond");
        return;
      }

      gnssDetected = true;
      gnssConfigurationIndex = 0;
      gnssStatusCache.clear();
      gnssStatusDirty = true;
      requestedNavPvtEpochRate =
          navPvtEpochRateForStatusFrequency(properties.getRefreshFrequency(),
                                            NAVIGATION_EPOCH_MILLIS);
      if (gnssProbeFailureReported)
        addLog("u-blox GNSS recovered after startup retry");
      else
        Serial.println("u-blox GNSS detected");
      gnssProbeFailureReported = false;
      setGnssInitializationState(GnssInitializationState::ConfigureVal8);
      return;
    }

    case GnssInitializationState::ConfigureVal8: {
      if (gnssConfigurationIndex >= std::size(GNSS_VAL8_SETTINGS)) {
        myGNSS.setI2CpollingWait(20);
        setGnssInitializationState(GnssInitializationState::ConfigureDynamicModel);
        return;
      }

      const GnssVal8Setting& setting = GNSS_VAL8_SETTINGS[gnssConfigurationIndex];
      const bool configured = myGNSS.setVal8(setting.key,
                                              setting.value,
                                              VAL_LAYER_RAM_BBR,
                                              GNSS_COMMAND_MAX_WAIT_MILLIS);
      if (!configured && setting.required) {
        scheduleGnssInitializationRetry(setting.name);
        return;
      }
      if (!configured) {
        Serial.print("GNSS optional setting was not accepted: ");
        Serial.println(setting.name);
      }
      ++gnssConfigurationIndex;
      setGnssInitializationState(GnssInitializationState::ConfigureVal8);
      return;
    }

    case GnssInitializationState::ConfigureDynamicModel:
      if (!myGNSS.setDynamicModel(DYN_MODEL_STATIONARY,
                                  VAL_LAYER_RAM_BBR,
                                  GNSS_COMMAND_MAX_WAIT_MILLIS)) {
        scheduleGnssInitializationRetry("stationary dynamic model");
        return;
      }
      setGnssInitializationState(GnssInitializationState::ConfigureMeasurementRate);
      return;

    case GnssInitializationState::ConfigureMeasurementRate:
      if (!myGNSS.setMeasurementRate(1000,
                                     VAL_LAYER_RAM_BBR,
                                     GNSS_COMMAND_MAX_WAIT_MILLIS)) {
        scheduleGnssInitializationRetry("1 Hz measurement rate");
        return;
      }
      setGnssInitializationState(GnssInitializationState::ConfigureNavigationRate);
      return;

    case GnssInitializationState::ConfigureNavigationRate:
      if (!myGNSS.setNavigationRate(1,
                                    VAL_LAYER_RAM_BBR,
                                    GNSS_COMMAND_MAX_WAIT_MILLIS)) {
        scheduleGnssInitializationRetry("1 Hz navigation rate");
        return;
      }
      setGnssInitializationState(GnssInitializationState::ConfigureTimePulseTiming);
      return;

    case GnssInitializationState::ConfigureTimePulseTiming:
      if (!configureTimePulseTiming()) {
        scheduleGnssInitializationRetry("TP1 timing values");
        return;
      }
      setGnssInitializationState(GnssInitializationState::ConfigureTimePulseControls);
      return;

    case GnssInitializationState::ConfigureTimePulseControls:
      if (!configureTimePulseControls()) {
        scheduleGnssInitializationRetry("TP1 control values");
        return;
      }
      setGnssInitializationState(GnssInitializationState::ConfigureNavPvtCallback);
      return;

    case GnssInitializationState::ConfigureNavPvtCallback:
      gnssStatusCache.clear();
      gnssStatusDirty = true;
      gnssStatusDisplayInitialized = false;
      if (!myGNSS.setAutoPVTcallbackPtr(navPvtCallback,
                                        VAL_LAYER_RAM_BBR,
                                        NAV_PVT_COMMAND_MAX_WAIT_MILLIS)) {
        scheduleGnssInitializationRetry("automatic NAV-PVT callback");
        return;
      }
      gnssInitialSampleStartedMillis = millis();
      setGnssInitializationState(GnssInitializationState::WaitForInitialNavPvt);
      return;

    case GnssInitializationState::WaitForInitialNavPvt: {
      myGNSS.checkUblox();
      myGNSS.checkCallbacks();
      GnssStatusSnapshot snapshot = {};
      if (!gnssStatusCache.get(&snapshot) &&
          !hasElapsed(millis(),
                      gnssInitialSampleStartedMillis,
                      NAV_PVT_INITIAL_SAMPLE_WAIT_MILLIS))
        return;
      setGnssInitializationState(GnssInitializationState::ConfigureNavPvtRate);
      return;
    }

    case GnssInitializationState::ConfigureNavPvtRate:
      myGNSS.setAutoPVTrate(requestedNavPvtEpochRate,
                            false,
                            VAL_LAYER_RAM_BBR,
                            NAV_PVT_COMMAND_MAX_WAIT_MILLIS);
      // Preserve the retry counter across apply/readback cycles so persistent
      // failures escalate back to a full receiver probe.
      gnssInitializationState = GnssInitializationState::ConfirmNavPvtRate;
      gnssStateStartedMillis = millis();
      gnssStateDelayMillis = 0;
      return;

    case GnssInitializationState::ConfirmNavPvtRate: {
      uint8_t confirmedEpochRate = 0;
      const bool rateRead =
          myGNSS.getVal8(UBLOX_CFG_MSGOUT_UBX_NAV_PVT_I2C,
                         &confirmedEpochRate,
                         VAL_LAYER_RAM,
                         NAV_PVT_COMMAND_MAX_WAIT_MILLIS);
      if (!rateRead || confirmedEpochRate != requestedNavPvtEpochRate) {
        gnssInitializationState = GnssInitializationState::ConfigureNavPvtRate;
        scheduleGnssInitializationRetry("automatic NAV-PVT rate readback");
        return;
      }
      navPvtEpochRate = confirmedEpochRate;
      gnssStatusMaximumAgeMillis =
          gnssStatusFreshnessLimitMillis(navPvtEpochRate,
                                         NAVIGATION_EPOCH_MILLIS);
      setGnssInitializationState(GnssInitializationState::ReadModuleInformation);
      return;
    }

    case GnssInitializationState::ReadModuleInformation:
      if (!myGNSS.getModuleInfo(GNSS_COMMAND_MAX_WAIT_MILLIS))
        Serial.println("GNSS module information was not available during startup");
      setGnssInitializationState(GnssInitializationState::ReadLeapSeconds);
      return;

    case GnssInitializationState::ReadLeapSeconds:
      gpsLeapSecondsAvailable =
          myGNSS.getLeapSecondEvent(GNSS_COMMAND_MAX_WAIT_MILLIS) &&
          myGNSS.packetUBXNAVTIMELS != nullptr &&
          myGNSS.packetUBXNAVTIMELS->data.currLs >=
              LEAP_SECONDS_2025 - LEAP_SECONDS_1980;
      if (gpsLeapSecondsAvailable) {
        gpsLeapSecondsSince1980 = myGNSS.packetUBXNAVTIMELS->data.currLs;
        t.setLeapSecondsSince1980(gpsLeapSecondsSince1980);
      }
      else {
        t.setLeapSecondsSince2025();
        gpsLeapSecondsSince1980 =
            static_cast<int8_t>(t.getTotalLeapSeconds() - LEAP_SECONDS_1980);
        recordError("GNSS leap seconds were unavailable; using the 2026 default");
      }
      setGnssInitializationState(GnssInitializationState::Ready);
      return;

    case GnssInitializationState::Ready:
      gnssReady = true;
      lastReportedGnssFailureState = GnssInitializationState::Ready;
      serviceCachedGnssStatus();
      addLog("GNSS initialization completed");
      addLog("GNSS TP1 configured for a UTC-aligned 1 Hz rising edge");
      addLog("Automatic UBX-NAV-PVT configured every " +
             String(navPvtEpochRate) + " navigation epoch(s)");
      suppressDetailedGnssConfigQueries = true;
      getDeviceConfig();
      suppressDetailedGnssConfigQueries = false;
      requestPpsTimebaseAcquisition();
      if (rtcAvailable)
        setRtc();
      return;
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
  Wire.begin();
  Wire.setClock(I2C_CLOCK_HZ);

  /***** Begin load data from properties *****/
  if (!properties.loadProperties())
    addError("Error loading setup properties from non-volatile storage");
  /***** End load data from properties *****/

  /***** Begin Ethernet setup *****/
  // The DEV-20748 Slot-0 connector routes the W5500 reset signal through
  // MicroMod PWM0 to Teensy pin 3. Ethernet startup and retries continue in
  // loop so an absent controller or link can never freeze setup.
  initializeEthernetService();
  /***** End Ethernet setup *****/
  
  /***** Begin HTTP setup *****/ 
  timeHttp.setAppName(APP_NAME);
  timeHttp.setConfigString(&configLog);
  timeHttp.setProperties(&properties);
  timeHttp.setLogArray(&usageLog);
  timeHttp.setErrorArray(&errorLog);
  timeHttp.setConfigFunction(&getDeviceConfig);
  timeHttp.setGpsTimeFunction(&getGpsISO8601Time);
  timeHttp.setRtcTimeFunction(&getRtcWebISO8601Time);
  timeHttp.setUpdateRtcFunction(&setRtc);
  timeHttp.setAddLogFunction(&addLog);
  timeHttp.setAddErrorFunction(&addError);
  timeHttp.setFirmwareUpdater(&firmwareUpdater);
  timeHttp.setFirmwareMaintenanceFunction(&setFirmwareUpdateMaintenance);
  timeHttp.setFirmwareInstallFunction(&installFirmwareUpdate);
  Serial.println("HTTP server configured; waiting for Ethernet");
  /***** End HTTP setup *****/ 

  // GNSS, RTC, and OLED initialization continues in loop. No I2C peripheral
  // can prevent Ethernet, HTTP, or NTP service from starting.
  initializePeripheralServices();
  getDeviceConfig();
  if (configLog.length() > 24) {
    Serial.println(F("**************************************************"));
    Serial.println(configLog);
  }

  // Reset timers
  refreshTimerMs = 0;
  rtcSetTimerMs = 0;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void loop() {
  // Once the browser has had time to load '/', begin the non-returning flash
  // replacement before making any further peripheral or network calls.
  serviceFirmwareInstall();

  // Apply the most recent TP1 edge before checking Ethernet. This is only local
  // arithmetic; GNSS I2C work is serviced after the NTP critical path.
  updatePpsClockFromPulse();

  /***** Begin NTP server *****/
  if (ethernetOnline && ntpUdpBound) {
    int packetSize = udp.parsePacket();
    if (packetSize) {
      const uint32_t receiveCaptureMicros = micros();
      processNtpRequest(packetSize, receiveCaptureMicros);
    }
  }
  /***** End NTP server *****/

  // Supervise Ethernet before any retryable I2C work. A late or absent
  // peripheral therefore cannot prevent the network state machine advancing.
  serviceEthernet();

  /***** Begin HTTP server *****/
  if (ethernetOnline && ethernetServiceState == EthernetServiceState::Online) {
    EthernetClient httpClient = httpServer.available();
    if (httpClient) {
      timeHttp.processRequest(&httpClient, strLocalIp, gpsFixType);
    }
  }
  /***** End HTTP server *****/

  serviceDisplayInitialization();
  serviceRtcInitialization();
  serviceGnssInitialization();

  if (gnssReady) {
    servicePpsTimebase();
    updatePpsClockFromPulse();
    myGNSS.checkCallbacks();
  }
  serviceCachedGnssStatus();
  reportTimePulse();
  serviceRtcSync();

  // To avoid having delays in loop, we'll use the strategy from BlinkWithoutDelay
  // see: File -> Examples -> 02.Digital -> BlinkWithoutDelay for more info
  if (refreshTimerMs > properties.getRefreshFrequency()) {
    // Start the next status interval before any OLED I2C transfer so transfer
    // time does not accumulate as refresh-schedule drift.
    refreshTimerMs = 0;
    digitalWrite(LED_BUILTIN, digitalRead(LED_BUILTIN) == HIGH ? LOW : HIGH);  // toggle LED

    if (oledAvailable && properties.getDisplayOn() == 1) {
      displaySettings();
      // If selected, invert the display to prevent burn in.
      if (appliedDisplayAlternate) {
        isDisplayInverted = !isDisplayInverted;
        myOLED.invert(isDisplayInverted);
      }
    }
  }

  // To avoid having delays in loop, we'll use the strategy from BlinkWithoutDelay
  // see: File -> Examples -> 02.Digital -> BlinkWithoutDelay for more info
  const uint32_t rtcSetFrequencyMs = properties.getRtcSetFrequency();
  // Zero disables periodic synchronization; the startup request still runs.
  if (rtcAvailable && rtcSetFrequencyMs > 0 && rtcSetTimerMs >= rtcSetFrequencyMs) {
    setRtc();
  }
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Returns the final size of configLog.  configLog needs to be a minimum of 1024 chars.
void getDeviceConfig() { // configLog is a global variable
  configLog = String(APP_NAME) + " v" + String(VERSION);
  configLog += "\ncopyright (c) 2026 " + String(AUTHOR);
  configLog += "\n\nServer Name        : " + properties.getServerName();

  configLog += "\nNetwork Configuration:";
  configLog += "\n Local IP address  : " + strLocalIp;
  configLog += "\n IP Subnet         : " + strSubnet;
  configLog += "\n Primary DNS       : " + strDns1Ip;
  configLog += "\n Secondary DNS     : " + strDns2Ip;
  configLog += "\n Gateway IP address: " + strGatewayIp;
  configLog += "\n\nGNSS configuration:";
  configLog += "\n Initialization state = " +
               String(gnssInitializationStateName(gnssInitializationState));
  configLog += "\n Receiver detected = " + String(gnssDetected ? 1 : 0);
  configLog += "\n Receiver ready = " + String(gnssReady ? 1 : 0);

  uint32_t pulseCount = 0;
  uint32_t intervalMicros = 0;
  getTimePulseStatus(&pulseCount, &intervalMicros);
  configLog += "\n TP1 input pin = " + String(TIME_PULSE_PIN);
  configLog += "\n TP1 captured pulse count = " + String(pulseCount);
  configLog += "\n TP1 last interval (us) = " + String(intervalMicros);
  configLog += "\n UBX-TIM-TP automatic output = " + String(timTpAutomaticEnabled ? 1 : 0);
  configLog += "\n UBX-TIM-TP fresh stream = " + String(timTpFreshStreamReady ? 1 : 0);
  configLog += "\n UBX-TIM-TP valid UTC label = " + String(validTimTpSeen ? 1 : 0);
  configLog += "\n UBX-TIM-TP target pending = " + String(timTpTargetPending ? 1 : 0);
  configLog += "\n TP1 NTP clock anchored = " + String(ppsClock.isAnchored() ? 1 : 0);
  configLog += "\n TP1 NTP clock confirmed = " + String(ppsClockConfirmed ? 1 : 0);

  // The setup page is available before GNSS startup finishes. Never turn its
  // Reload Server Config action into a wait for an absent receiver.
  if (!gnssReady) {
    configLog += "\n GNSS details = unavailable while initialization is pending";
    return;
  }
  if (suppressDetailedGnssConfigQueries) {
    configLog += "\n GNSS details = select Reload Server Config for live receiver values";
    return;
  }
  if (!myGNSS.isConnected(GNSS_CONFIG_QUERY_MAX_WAIT_MILLIS)) {
    configLog += "\n GNSS details = receiver did not respond to the bounded status query";
    return;
  }

  configLog += "\n GPS module name = " +
               String(myGNSS.getModuleName(GNSS_CONFIG_QUERY_MAX_WAIT_MILLIS));
  configLog += "\n GPS firmware type = " +
               String(myGNSS.getFirmwareType(GNSS_CONFIG_QUERY_MAX_WAIT_MILLIS));
  configLog += "\n GPS firmware version = " +
               String(myGNSS.getFirmwareVersionHigh(GNSS_CONFIG_QUERY_MAX_WAIT_MILLIS)) +
               String(myGNSS.getFirmwareVersionLow(GNSS_CONFIG_QUERY_MAX_WAIT_MILLIS));
  configLog += "\n I2C transaction size = " + String(myGNSS.getI2CTransactionSize());
  configLog += "\n I2C clock (Hz) = " + String(I2C_CLOCK_HZ);
  configLog += "\n CFG-SPI-ENABLED = " + String(myGNSS.getVal8(UBLOX_CFG_SPI_ENABLED, VAL_LAYER_RAM, GNSS_CONFIG_QUERY_MAX_WAIT_MILLIS));
  configLog += "\n CFG-UART1-ENABLED = " + String(myGNSS.getVal8(UBLOX_CFG_UART1_ENABLED, VAL_LAYER_RAM, GNSS_CONFIG_QUERY_MAX_WAIT_MILLIS));
  configLog += "\n CFG-UART2-ENABLED = " + String(myGNSS.getVal8(UBLOX_CFG_UART2_ENABLED, VAL_LAYER_RAM, GNSS_CONFIG_QUERY_MAX_WAIT_MILLIS));
  configLog += "\n CFG-USB-ENABLED = " + String(myGNSS.getVal8(UBLOX_CFG_USB_ENABLED, VAL_LAYER_RAM, GNSS_CONFIG_QUERY_MAX_WAIT_MILLIS));
  configLog += "\n CFG-I2CINPROT-UBX = " + String(myGNSS.getVal8(UBLOX_CFG_I2CINPROT_UBX, VAL_LAYER_RAM, GNSS_CONFIG_QUERY_MAX_WAIT_MILLIS));
  configLog += "\n CFG-I2COUTPROT-UBX = " + String(myGNSS.getVal8(UBLOX_CFG_I2COUTPROT_UBX, VAL_LAYER_RAM, GNSS_CONFIG_QUERY_MAX_WAIT_MILLIS));
  configLog += "\n CFG-I2CINPROT-NMEA = " + String(myGNSS.getVal8(UBLOX_CFG_I2CINPROT_NMEA, VAL_LAYER_RAM, GNSS_CONFIG_QUERY_MAX_WAIT_MILLIS));
  configLog += "\n CFG-I2COUTPROT-NMEA = " + String(myGNSS.getVal8(UBLOX_CFG_I2COUTPROT_NMEA, VAL_LAYER_RAM, GNSS_CONFIG_QUERY_MAX_WAIT_MILLIS));
  configLog += "\n CFG-I2CINPROT-RTCM3X = " + String(myGNSS.getVal8(UBLOX_CFG_I2CINPROT_RTCM3X, VAL_LAYER_RAM, GNSS_CONFIG_QUERY_MAX_WAIT_MILLIS));
  configLog += "\n CFG-I2COUTPROT-RTCM3X = " + String(myGNSS.getVal8(UBLOX_CFG_I2COUTPROT_RTCM3X, VAL_LAYER_RAM, GNSS_CONFIG_QUERY_MAX_WAIT_MILLIS));
  configLog += "\n CFG-I2C-EXTENDEDTIMEOUT = " + String(myGNSS.getVal8(UBLOX_CFG_I2C_EXTENDEDTIMEOUT, VAL_LAYER_RAM, GNSS_CONFIG_QUERY_MAX_WAIT_MILLIS));
  configLog += "\n CFG-CLOCK-OSC-FREQ = " + String(myGNSS.getVal8(UBLOX_CFG_CLOCK_OSC_FREQ, VAL_LAYER_RAM, GNSS_CONFIG_QUERY_MAX_WAIT_MILLIS));
  configLog += "\n CFG-RATE-MEAS = " + String(myGNSS.getVal16(UBLOX_CFG_RATE_MEAS, VAL_LAYER_RAM, GNSS_CONFIG_QUERY_MAX_WAIT_MILLIS));
  configLog += "\n CFG-RATE-NAV = " + String(myGNSS.getVal16(UBLOX_CFG_RATE_NAV, VAL_LAYER_RAM, GNSS_CONFIG_QUERY_MAX_WAIT_MILLIS));
  configLog += "\n CFG-MSGOUT-UBX_NAV_PVT_I2C = " + String(myGNSS.getVal8(UBLOX_CFG_MSGOUT_UBX_NAV_PVT_I2C, VAL_LAYER_RAM, GNSS_CONFIG_QUERY_MAX_WAIT_MILLIS));
  configLog += "\n CFG-MSGOUT-UBX_NAV_TIMEUTC_I2C = " + String(myGNSS.getVal8(UBLOX_CFG_MSGOUT_UBX_NAV_TIMEUTC_I2C, VAL_LAYER_RAM, GNSS_CONFIG_QUERY_MAX_WAIT_MILLIS));
  configLog += "\n CFG-MSGOUT-UBX_TIM_TP_I2C = " + String(myGNSS.getVal8(UBLOX_CFG_MSGOUT_UBX_TIM_TP_I2C, VAL_LAYER_RAM, GNSS_CONFIG_QUERY_MAX_WAIT_MILLIS));
  configLog += "\n CFG-TP-FREQ-TP1 = " + String(myGNSS.getVal32(UBLOX_CFG_TP_FREQ_TP1, VAL_LAYER_RAM, GNSS_CONFIG_QUERY_MAX_WAIT_MILLIS));
  configLog += "\n CFG-TP-FREQ-LOCK-TP1 = " + String(myGNSS.getVal32(UBLOX_CFG_TP_FREQ_LOCK_TP1, VAL_LAYER_RAM, GNSS_CONFIG_QUERY_MAX_WAIT_MILLIS));
  configLog += "\n CFG-TP-LEN-TP1 = " + String(myGNSS.getVal32(UBLOX_CFG_TP_LEN_TP1, VAL_LAYER_RAM, GNSS_CONFIG_QUERY_MAX_WAIT_MILLIS));
  configLog += "\n CFG-TP-LEN-LOCK-TP1 = " + String(myGNSS.getVal32(UBLOX_CFG_TP_LEN_LOCK_TP1, VAL_LAYER_RAM, GNSS_CONFIG_QUERY_MAX_WAIT_MILLIS));
  configLog += "\n CFG-TP-TIMEGRID-TP1 = " + String(myGNSS.getVal8(UBLOX_CFG_TP_TIMEGRID_TP1, VAL_LAYER_RAM, GNSS_CONFIG_QUERY_MAX_WAIT_MILLIS));
  configLog += "\n CFG-NAVSPG-DYNMODEL = " + String(myGNSS.getVal8(UBLOX_CFG_NAVSPG_DYNMODEL, VAL_LAYER_RAM, GNSS_CONFIG_QUERY_MAX_WAIT_MILLIS));
  configLog += "\n CFG-SIGNAL-GAL_ENA = " + String(myGNSS.getVal8(UBLOX_CFG_SIGNAL_GAL_ENA, VAL_LAYER_RAM, GNSS_CONFIG_QUERY_MAX_WAIT_MILLIS));
  configLog += "\n CFG-SIGNAL-GAL_E1_ENA = " + String(myGNSS.getVal8(UBLOX_CFG_SIGNAL_GAL_E1_ENA, VAL_LAYER_RAM, GNSS_CONFIG_QUERY_MAX_WAIT_MILLIS));
  configLog += "\n CFG-SIGNAL-GAL_E5A_ENA = " + String(myGNSS.getVal8(UBLOX_CFG_SIGNAL_GAL_E5A_ENA, VAL_LAYER_RAM, GNSS_CONFIG_QUERY_MAX_WAIT_MILLIS));
  configLog += "\n CFG-SIGNAL-GAL_E5B_ENA = " + String(myGNSS.getVal8(UBLOX_CFG_SIGNAL_GAL_E5B_ENA, VAL_LAYER_RAM, GNSS_CONFIG_QUERY_MAX_WAIT_MILLIS));
  configLog += "\n CFG-SIGNAL-GPS_ENA = " + String(myGNSS.getVal8(UBLOX_CFG_SIGNAL_GPS_ENA, VAL_LAYER_RAM, GNSS_CONFIG_QUERY_MAX_WAIT_MILLIS));
  configLog += "\n CFG-SIGNAL-GPS_L1CA_ENA = " + String(myGNSS.getVal8(UBLOX_CFG_SIGNAL_GPS_L1CA_ENA, VAL_LAYER_RAM, GNSS_CONFIG_QUERY_MAX_WAIT_MILLIS));
  configLog += "\n CFG-SIGNAL-GPS_L2C_ENA = " + String(myGNSS.getVal8(UBLOX_CFG_SIGNAL_GPS_L2C_ENA, VAL_LAYER_RAM, GNSS_CONFIG_QUERY_MAX_WAIT_MILLIS));
  configLog += "\n CFG-SIGNAL-GPS_L5_ENA = " + String(myGNSS.getVal8(UBLOX_CFG_SIGNAL_GPS_L5_ENA, VAL_LAYER_RAM, GNSS_CONFIG_QUERY_MAX_WAIT_MILLIS));
  configLog += "\n CFG-SIGNAL-BDS_ENA = " + String(myGNSS.getVal8(UBLOX_CFG_SIGNAL_BDS_ENA, VAL_LAYER_RAM, GNSS_CONFIG_QUERY_MAX_WAIT_MILLIS));
  configLog += "\n CFG-SIGNAL-GLO_ENA = " + String(myGNSS.getVal8(UBLOX_CFG_SIGNAL_GLO_ENA, VAL_LAYER_RAM, GNSS_CONFIG_QUERY_MAX_WAIT_MILLIS));
  configLog += "\n CFG-SIGNAL-SBAS_ENA = " + String(myGNSS.getVal8(UBLOX_CFG_SIGNAL_SBAS_ENA, VAL_LAYER_RAM, GNSS_CONFIG_QUERY_MAX_WAIT_MILLIS));
  configLog += "\n CFG-SIGNAL-QZSS_ENA = " + String(myGNSS.getVal8(UBLOX_CFG_SIGNAL_QZSS_ENA, VAL_LAYER_RAM, GNSS_CONFIG_QUERY_MAX_WAIT_MILLIS));
  configLog += "\n GPS leap seconds = " +
               String(static_cast<int>(gpsLeapSecondsSince1980));
  configLog += "\n Total leap seconds = " +
               String(static_cast<int>(t.getTotalLeapSeconds()));
  configLog += "\n Leap-second value from GNSS = " +
               String(gpsLeapSecondsAvailable ? 1 : 0);

  GnssStatusSnapshot snapshot = {};
  if (gnssStatusCache.get(&snapshot)) {
    configLog += "\n GPS fix type = " +
                 String(gnssFixTypeName(snapshot.fixOk, snapshot.fixType));
    configLog += "\n Number of GPS signals = " + String(snapshot.satellitesUsed);
  }
  else {
    configLog += "\n GPS fix type = Waiting for NAV-PVT";
    configLog += "\n Number of GPS signals = 0";
  }
}

void cacheNavPvtData(const UBX_NAV_PVT_data_t& data) {
  GnssStatusSnapshot snapshot = {};
  snapshot.receivedMillis = millis();
  snapshot.timeAccuracyNanoseconds = data.tAcc;
  snapshot.nanoseconds = data.nano;
  snapshot.year = data.year;
  snapshot.month = data.month;
  snapshot.day = data.day;
  snapshot.hour = data.hour;
  snapshot.minute = data.min;
  snapshot.second = data.sec;
  snapshot.fixType = data.fixType;
  snapshot.satellitesUsed = data.numSV;
  snapshot.fixOk = data.flags.bits.gnssFixOK == 1;
  snapshot.validDate = data.valid.bits.validDate == 1;
  snapshot.validTime = data.valid.bits.validTime == 1;
  snapshot.fullyResolved = data.valid.bits.fullyResolved == 1;
  gnssStatusCache.update(snapshot);
  gnssStatusDirty = true;
}

// SparkFun's setAutoPVTcallbackPtr requires this exact mutable-pointer callback
// signature, although this callback treats the supplied report as read-only.
// NOLINTNEXTLINE(readability-non-const-parameter)
void navPvtCallback(UBX_NAV_PVT_data_t* data) {
  if (data != nullptr)
    cacheNavPvtData(*data);
}

void serviceCachedGnssStatus() {
  GnssStatusSnapshot snapshot = {};
  const bool hasSnapshot = gnssStatusCache.get(&snapshot);
  const bool isFresh = gnssStatusCache.isFresh(millis(),
                                                gnssStatusMaximumAgeMillis);

  if (!gnssStatusDirty && gnssStatusDisplayInitialized &&
      isFresh == gnssStatusWasFresh)
    return;

  if (!gnssReady)
    gpsFixType = gnssDetected ? "Configuring GNSS" : "Waiting for GNSS";
  else if (!hasSnapshot)
    gpsFixType = "Waiting for NAV-PVT";
  else if (!isFresh)
    gpsFixType = "Stale NAV-PVT";
  else
    gpsFixType = gnssFixTypeName(snapshot.fixOk, snapshot.fixType);

  gnssStatusDirty = false;
  gnssStatusDisplayInitialized = true;
  gnssStatusWasFresh = isFresh;
}

bool configureTimePulseTiming() {
  // Configure both the unlocked and locked signals. UTC validity is checked
  // through UBX-TIM-TP before TP1 is accepted as the NTP timebase.
  bool timingConfigured = myGNSS.newCfgValset(VAL_LAYER_RAM_BBR);
  if (timingConfigured) {
    timingConfigured &= myGNSS.addCfgValset(UBLOX_CFG_TP_FREQ_TP1, 1);
    timingConfigured &= myGNSS.addCfgValset(UBLOX_CFG_TP_FREQ_LOCK_TP1, 1);
    timingConfigured &= myGNSS.addCfgValset(UBLOX_CFG_TP_LEN_TP1, 100000);
    timingConfigured &= myGNSS.addCfgValset(UBLOX_CFG_TP_LEN_LOCK_TP1, 100000);
    timingConfigured &= myGNSS.addCfgValset(UBLOX_CFG_TP_USER_DELAY_TP1, 0);
    if (timingConfigured)
      timingConfigured = myGNSS.sendCfgValset(GNSS_COMMAND_MAX_WAIT_MILLIS);
  }
  return timingConfigured;
}

bool configureTimePulseControls() {
  bool controlsConfigured = myGNSS.newCfgValset(VAL_LAYER_RAM_BBR);
  if (controlsConfigured) {
    controlsConfigured &= myGNSS.addCfgValset(UBLOX_CFG_TP_TP1_ENA, 1);
    controlsConfigured &= myGNSS.addCfgValset(UBLOX_CFG_TP_SYNC_GNSS_TP1, 1);
    controlsConfigured &= myGNSS.addCfgValset(UBLOX_CFG_TP_USE_LOCKED_TP1, 1);
    controlsConfigured &= myGNSS.addCfgValset(UBLOX_CFG_TP_ALIGN_TO_TOW_TP1, 1);
    controlsConfigured &= myGNSS.addCfgValset(UBLOX_CFG_TP_POL_TP1, 1);
    controlsConfigured &= myGNSS.addCfgValset(UBLOX_CFG_TP_TIMEGRID_TP1, 0); // UTC
    controlsConfigured &= myGNSS.addCfgValset(UBLOX_CFG_TP_PULSE_DEF, 1); // Frequency
    controlsConfigured &= myGNSS.addCfgValset(UBLOX_CFG_TP_PULSE_LENGTH_DEF, 1); // Length in microseconds
    if (controlsConfigured)
      controlsConfigured = myGNSS.sendCfgValset(GNSS_COMMAND_MAX_WAIT_MILLIS);
  }
  return controlsConfigured;
}

bool timTpToUtc(const UBX_TIM_TP_data_t& pulse, NormalizedTimestamp* utc) {
  if (utc == nullptr || pulse.flags.bits.timeBase != 1 || pulse.flags.bits.utc != 1 ||
      pulse.week == 0 || pulse.towMS >= SECONDS_PER_WEEK * 1000ULL)
    return false;

  const int64_t secondsSince1900 = static_cast<int64_t>(GPS_EPOCH_SECONDS_SINCE_1900) +
                                   static_cast<int64_t>(pulse.week) * SECONDS_PER_WEEK +
                                   pulse.towMS / 1000U;
  const int64_t nanoseconds = static_cast<int64_t>(pulse.towMS % 1000U) * 1000000LL +
                              static_cast<int64_t>((static_cast<uint64_t>(pulse.towSubMS) *
                                                    1000000ULL) >> 32);
  *utc = normalizeTimestamp(secondsSince1900, nanoseconds);

  TimeData pulseTime;
  return pulseTime.setSecondsSince1900(static_cast<uint64_t>(utc->secondsSince1900)) &&
         pulseTime.getYear() >= 2026 && pulseTime.getYear() <= 2099;
}

bool timestampsAreNear(const NormalizedTimestamp& left, const NormalizedTimestamp& right) {
  const int64_t secondsDifference = left.secondsSince1900 - right.secondsSince1900;
  if (secondsDifference < -1 || secondsDifference > 1)
    return false;

  int64_t nanosecondsDifference = secondsDifference * 1000000000LL +
                                  static_cast<int64_t>(left.nanoseconds) - right.nanoseconds;
  if (nanosecondsDifference < 0)
    nanosecondsDifference = -nanosecondsDifference;
  return nanosecondsDifference <= TIMTP_LABEL_TOLERANCE_NANOSECONDS;
}

void queueTimTpTarget(const uint32_t pulseCount, const NormalizedTimestamp& utc) {
  timTpTargetPulseCount = pulseCount;
  timTpTargetUtc = utc;
  timTpTargetPending = true;
  timTpTargetQueuedMillis = millis();
  lastValidTimTpMillis = timTpTargetQueuedMillis;
  validTimTpSeen = true;
}

void invalidatePpsTimebase() {
  ppsClock.reset();
  timTpTargetPending = false;
  validTimTpSeen = false;
  ppsClockConfirmed = false;
  timTpFreshStreamReady = false;
  timTpPostEdgeDrainPending = false;
  noInterrupts();
  timTpFreshnessReferencePulseCount = timePulseCount;
  interrupts();
}

void drainGnssBeforeTimTpBoundary() {
  // Drain the shared stream so NAV-PVT callbacks keep advancing, then discard
  // every TIM-TP label parsed before a clean TP1 freshness boundary.
  myGNSS.checkUblox();
  myGNSS.flushTIMTP();
}

void requestPpsTimebaseAcquisition() {
  if (!gnssReady || ppsTimebaseAcquisitionState != PpsTimebaseAcquisitionState::Idle)
    return;

  ppsAcquisitionAttempted = true;
  lastPpsAcquisitionAttemptMillis = millis();
  invalidatePpsTimebase();
  timTpAutomaticEnabled = false;
  ppsEnableCommandSucceeded = false;
  ppsTimebaseAcquisitionState =
      PpsTimebaseAcquisitionState::DisableAutomaticTimTp;
  ppsAcquisitionStateStartedMillis = millis();
}

void failPpsTimebaseAcquisition(const char* reason) {
  timTpAutomaticEnabled = false;
  ppsTimebaseAcquisitionState = PpsTimebaseAcquisitionState::Idle;
  lastPpsAcquisitionAttemptMillis = millis();
  invalidatePpsTimebase();
  if (consecutivePpsAcquisitionFailures < UINT8_MAX)
    ++consecutivePpsAcquisitionFailures;
  if (!ppsAcquisitionFailureReported) {
    ppsAcquisitionFailureReported = true;
    recordError(String("Could not establish the automatic UBX-TIM-TP stream: ") +
                reason + "; retrying");
  }

  if (consecutivePpsAcquisitionFailures < MAX_PPS_ACQUISITION_FAILURES)
    return;

  // Repeated TIM-TP command failures usually mean the receiver reset or left
  // the bus. Return to the full, capped-backoff initialization sequence so all
  // timing configuration is reapplied and degraded service does not dominate
  // loop time indefinitely.
  const uint8_t backoffAttempt = consecutivePpsAcquisitionFailures;
  restartGnssInitialization(
      "Repeated UBX-TIM-TP failures; restarting full GNSS initialization",
      backoffAttempt);
}

void servicePpsTimebaseAcquisition() {
  switch (ppsTimebaseAcquisitionState) {
    case PpsTimebaseAcquisitionState::Idle:
      return;

    case PpsTimebaseAcquisitionState::DisableAutomaticTimTp:
      // The following readback, not the SparkFun return value alone, proves
      // that the receiver actually stopped the stream.
      myGNSS.setAutoTIMTP(false,
                          true,
                          VAL_LAYER_RAM_BBR,
                          GNSS_COMMAND_MAX_WAIT_MILLIS);
      ppsTimebaseAcquisitionState =
          PpsTimebaseAcquisitionState::ConfirmAutomaticTimTpDisabled;
      return;

    case PpsTimebaseAcquisitionState::ConfirmAutomaticTimTpDisabled: {
      uint8_t rate = UINT8_MAX;
      if (!myGNSS.getVal8(UBLOX_CFG_MSGOUT_UBX_TIM_TP_I2C,
                          &rate,
                          VAL_LAYER_RAM,
                          GNSS_COMMAND_MAX_WAIT_MILLIS) ||
          rate != 0) {
        failPpsTimebaseAcquisition("automatic output could not be disabled");
        return;
      }
      ppsAcquisitionStateStartedMillis = millis();
      ppsTimebaseAcquisitionState = PpsTimebaseAcquisitionState::WaitForDrain;
      return;
    }

    case PpsTimebaseAcquisitionState::WaitForDrain:
      if (!hasElapsed(millis(),
                      ppsAcquisitionStateStartedMillis,
                      TIMTP_DRAIN_DELAY_MILLIS))
        return;
      ppsTimebaseAcquisitionState = PpsTimebaseAcquisitionState::DrainBufferedData;
      return;

    case PpsTimebaseAcquisitionState::DrainBufferedData:
      // flushTIMTP only marks parsed data stale. Drain receiver output before
      // defining the fresh TP1/TIM-TP association boundary.
      drainGnssBeforeTimTpBoundary();
      ppsTimebaseAcquisitionState = PpsTimebaseAcquisitionState::EnableAutomaticTimTp;
      return;

    case PpsTimebaseAcquisitionState::EnableAutomaticTimTp:
      ppsEnableCommandSucceeded =
          myGNSS.setAutoTIMTP(true,
                              true,
                              VAL_LAYER_RAM_BBR,
                              GNSS_COMMAND_MAX_WAIT_MILLIS);
      ppsTimebaseAcquisitionState =
          PpsTimebaseAcquisitionState::ConfirmAutomaticTimTpEnabled;
      return;

    case PpsTimebaseAcquisitionState::ConfirmAutomaticTimTpEnabled: {
      uint8_t rate = 0;
      const bool readbackMatches =
          myGNSS.getVal8(UBLOX_CFG_MSGOUT_UBX_TIM_TP_I2C,
                         &rate,
                         VAL_LAYER_RAM,
                         GNSS_COMMAND_MAX_WAIT_MILLIS) &&
          rate == 1;
      // SparkFun's setter may report success when fallback VALGET succeeds but
      // the requested rate was not applied. Require both internal setup and an
      // explicit rate readback so getTIMTP remains nonblocking in Ready state.
      if (!ppsEnableCommandSucceeded || !readbackMatches) {
        failPpsTimebaseAcquisition("automatic output enable readback failed");
        return;
      }

      timTpAutomaticEnabled = true;
      myGNSS.flushTIMTP();
      lastTimTpReportMillis = millis();
      uint32_t pulseCount = 0;
      uint32_t intervalMicros = 0;
      uint32_t invalidIntervalCount = 0;
      getTimePulseStatus(&pulseCount,
                         &intervalMicros,
                         nullptr,
                         &invalidIntervalCount);
      observedInvalidIntervalCount = invalidIntervalCount;
      timTpFreshnessReferencePulseCount = pulseCount;
      updatePpsClockFromPulse();
      ppsTimebaseAcquisitionState = PpsTimebaseAcquisitionState::Idle;
      if (ppsAcquisitionFailureReported)
        addLog("Automatic UBX-TIM-TP recovered for the NTP timebase");
      else if (!ppsAutomaticEverEnabled)
        addLog("Automatic UBX-TIM-TP enabled for the NTP timebase");
      ppsAutomaticEverEnabled = true;
      ppsAcquisitionFailureReported = false;
      consecutivePpsAcquisitionFailures = 0;
      return;
    }
  }
}

void updatePpsClockFromPulse() {
  uint32_t pulseCount = 0;
  uint32_t intervalMicros = 0;
  uint32_t edgeMicros = 0;
  uint32_t invalidIntervalCount = 0;
  getTimePulseStatus(&pulseCount,
                     &intervalMicros,
                     &edgeMicros,
                     &invalidIntervalCount);

  if (invalidIntervalCount != observedInvalidIntervalCount) {
    observedInvalidIntervalCount = invalidIntervalCount;
    invalidatePpsTimebase();
    return;
  }

  bool anchoredFromTarget = false;
  if (timTpTargetPending) {
    const int32_t targetDifference = static_cast<int32_t>(pulseCount - timTpTargetPulseCount);
    if (targetDifference == 0 && pulseCount >= 2 &&
        PpsClock::isExpectedPulseInterval(intervalMicros)) {
      anchoredFromTarget = ppsClock.setLabelledPulse(pulseCount,
                                                     edgeMicros,
                                                     intervalMicros,
                                                     timTpTargetUtc);
      timTpTargetPending = false;
      if (!anchoredFromTarget) {
        invalidatePpsTimebase();
        return;
      }
    }
    else if (targetDifference > 0) {
      // Only the latest edge is retained by the ISR, so a missed target cannot
      // be assigned a trustworthy capture timestamp.
      timTpTargetPending = false;
      if (!ppsClock.isAnchored()) {
        invalidatePpsTimebase();
        return;
      }
    }
  }

  if (ppsClock.isAnchored() && !anchoredFromTarget &&
      !ppsClock.advanceToPulse(pulseCount, edgeMicros, intervalMicros)) {
    invalidatePpsTimebase();
  }
}

bool getCoherentPpsAnchor(uint32_t* pulseCount,
                          uint32_t* edgeMicros,
                          uint32_t* intervalMicros,
                          NormalizedTimestamp* utcAtEdge) {
  for (uint8_t attempt = 0; attempt < 3; ++attempt) {
    updatePpsClockFromPulse();

    uint32_t anchorPulseCount = 0;
    uint32_t anchorEdgeMicros = 0;
    NormalizedTimestamp anchorUtc = {};
    if (!ppsClock.getAnchor(&anchorPulseCount, &anchorEdgeMicros, &anchorUtc))
      return false;

    uint32_t currentPulseCount = 0;
    uint32_t currentEdgeMicros = 0;
    uint32_t currentIntervalMicros = 0;
    getTimePulseStatus(&currentPulseCount, &currentIntervalMicros, &currentEdgeMicros);
    if (anchorPulseCount == currentPulseCount && anchorEdgeMicros == currentEdgeMicros) {
      *pulseCount = anchorPulseCount;
      *edgeMicros = anchorEdgeMicros;
      *intervalMicros = currentIntervalMicros;
      *utcAtEdge = anchorUtc;
      return true;
    }
  }
  return false;
}

void servicePpsTimebase() {
  updatePpsClockFromPulse();

  if (ppsTimebaseAcquisitionState != PpsTimebaseAcquisitionState::Idle) {
    servicePpsTimebaseAcquisition();
    return;
  }

  if (!timTpAutomaticEnabled) {
    if (!ppsAcquisitionAttempted ||
        static_cast<uint32_t>(millis() - lastPpsAcquisitionAttemptMillis) >=
            PPS_ACQUISITION_RETRY_MILLIS)
      requestPpsTimebaseAcquisition();
    return;
  }

  if (!ppsClock.isAnchored() && timTpTargetPending &&
      static_cast<uint32_t>(millis() - timTpTargetQueuedMillis) > TIMTP_STALE_MILLIS)
    invalidatePpsTimebase();

  if (!timTpFreshStreamReady) {
    if (!timTpPostEdgeDrainPending) {
      // Keep NAV-PVT status live while waiting for TP1. Any TIM-TP parsed here
      // is discarded before the pulse snapshot and cannot anchor the clock.
      drainGnssBeforeTimTpBoundary();

      uint32_t pulseCount = 0;
      uint32_t intervalMicros = 0;
      uint32_t edgeMicros = 0;
      uint32_t invalidIntervalCount = 0;
      getTimePulseStatus(&pulseCount,
                         &intervalMicros,
                         &edgeMicros,
                         &invalidIntervalCount);

      if (invalidIntervalCount != observedInvalidIntervalCount) {
        observedInvalidIntervalCount = invalidIntervalCount;
        invalidatePpsTimebase();
        return;
      }

      // Establish the freshness boundary only after a new, valid TP1 edge.
      if (pulseCount == timTpFreshnessReferencePulseCount || pulseCount < 2 ||
          !PpsClock::isExpectedPulseInterval(intervalMicros) ||
          static_cast<uint32_t>(micros() - edgeMicros) <
              TIMTP_POST_EDGE_DRAIN_GUARD_MICROS) {
        return;
      }

      // Wait cooperatively until SparkFun's 20 ms I2C polling gate cannot
      // suppress the final drain. Ethernet and client service continue meanwhile.
      timTpPostEdgeDrainPending = true;
      timTpPostEdgeDrainPulseCount = pulseCount;
      timTpPostEdgeDrainInvalidIntervalCount = invalidIntervalCount;
      timTpPostEdgeDrainStartedMillis = millis();
      return;
    }

    if (!hasElapsed(millis(),
                    timTpPostEdgeDrainStartedMillis,
                    TIMTP_DRAIN_DELAY_MILLIS))
      return;
    timTpPostEdgeDrainPending = false;

    uint32_t pulseCountBeforeDrain = 0;
    uint32_t intervalMicrosBeforeDrain = 0;
    uint32_t invalidIntervalCountBeforeDrain = 0;
    getTimePulseStatus(&pulseCountBeforeDrain,
                       &intervalMicrosBeforeDrain,
                       nullptr,
                       &invalidIntervalCountBeforeDrain);
    if (pulseCountBeforeDrain != timTpPostEdgeDrainPulseCount ||
        invalidIntervalCountBeforeDrain !=
            timTpPostEdgeDrainInvalidIntervalCount) {
      observedInvalidIntervalCount = invalidIntervalCountBeforeDrain;
      timTpFreshnessReferencePulseCount = pulseCountBeforeDrain;
      return;
    }

    drainGnssBeforeTimTpBoundary();

    uint32_t pulseCountAfterDrain = 0;
    uint32_t intervalMicrosAfterDrain = 0;
    uint32_t invalidIntervalCountAfterDrain = 0;
    getTimePulseStatus(&pulseCountAfterDrain,
                       &intervalMicrosAfterDrain,
                       nullptr,
                       &invalidIntervalCountAfterDrain);
    if (pulseCountAfterDrain != pulseCountBeforeDrain ||
        invalidIntervalCountAfterDrain != invalidIntervalCountBeforeDrain ||
        !PpsClock::isExpectedPulseInterval(intervalMicrosAfterDrain)) {
      observedInvalidIntervalCount = invalidIntervalCountAfterDrain;
      timTpFreshnessReferencePulseCount = pulseCountAfterDrain;
      return;
    }

    timTpFreshStreamReady = true;
    lastTimTpReportMillis = millis();
    return;
  }

  // Other GNSS queries can parse an automatic report outside this timing
  // bracket. Discard that unread flag first; only a report parsed by the
  // following getTIMTP call is safe to associate with these TP1 snapshots.
  myGNSS.flushTIMTP();

  // In automatic mode getTIMTP performs a non-blocking check of the I2C receive
  // buffer. Bracket the parse with TP1 snapshots: TIM-TP labels the next edge,
  // and a report which crosses an edge while being parsed is intentionally
  // discarded instead of risking a one-second association error.
  uint32_t pulseCountBefore = 0;
  uint32_t intervalMicrosBefore = 0;
  uint32_t invalidIntervalCountBefore = 0;
  getTimePulseStatus(&pulseCountBefore,
                     &intervalMicrosBefore,
                     nullptr,
                     &invalidIntervalCountBefore);

  const bool reportAvailable = myGNSS.getTIMTP() && myGNSS.packetUBXTIMTP != nullptr;

  uint32_t pulseCountAfter = 0;
  uint32_t intervalMicrosAfter = 0;
  uint32_t invalidIntervalCountAfter = 0;
  getTimePulseStatus(&pulseCountAfter,
                     &intervalMicrosAfter,
                     nullptr,
                     &invalidIntervalCountAfter);

  if (invalidIntervalCountAfter != invalidIntervalCountBefore) {
    observedInvalidIntervalCount = invalidIntervalCountAfter;
    invalidatePpsTimebase();
    if (reportAvailable)
      myGNSS.flushTIMTP();
    return;
  }

  if (!reportAvailable) {
    if (ppsClock.isAnchored() &&
        (!validTimTpSeen ||
         static_cast<uint32_t>(millis() - lastValidTimTpMillis) > TIMTP_STALE_MILLIS))
      invalidatePpsTimebase();

    if (static_cast<uint32_t>(millis() - lastTimTpReportMillis) >
        TIMTP_STREAM_RESTART_MILLIS) {
      if (consecutiveTimTpStreamRestarts < UINT8_MAX)
        ++consecutiveTimTpStreamRestarts;

      // A successful rate readback does not prove that the receiver is
      // actually emitting TIM-TP. After repeated silent stream restarts,
      // re-probe and reapply the complete GNSS configuration.
      if (consecutiveTimTpStreamRestarts >= MAX_PPS_ACQUISITION_FAILURES) {
        const uint8_t backoffAttempt = consecutiveTimTpStreamRestarts;
        restartGnssInitialization(
            "Automatic UBX-TIM-TP produced no reports; restarting full GNSS initialization",
            backoffAttempt);
      }
      else {
        requestPpsTimebaseAcquisition();
      }
    }
    return;
  }

  consecutiveTimTpStreamRestarts = 0;
  const UBX_TIM_TP_data_t pulse = myGNSS.packetUBXTIMTP->data;
  myGNSS.flushTIMTP();
  lastTimTpReportMillis = millis();

  NormalizedTimestamp reportedUtc = {};
  if (!timTpToUtc(pulse, &reportedUtc)) {
    invalidatePpsTimebase();
    return;
  }

  if (!ppsClock.isAnchored()) {
    if (timTpTargetPending)
      return;

    if (pulseCountBefore == pulseCountAfter && pulseCountAfter >= 2 &&
        invalidIntervalCountAfter == observedInvalidIntervalCount &&
        PpsClock::isExpectedPulseInterval(intervalMicrosAfter)) {
      queueTimTpTarget(pulseCountAfter + 1, reportedUtc);
      updatePpsClockFromPulse();
    }
    return;
  }

  uint32_t anchorPulseCount = 0;
  uint32_t intervalMicros = 0;
  uint32_t anchorEdgeMicros = 0;
  NormalizedTimestamp anchorUtc = {};
  if (!getCoherentPpsAnchor(&anchorPulseCount,
                            &anchorEdgeMicros,
                            &intervalMicros,
                            &anchorUtc))
    return;

  const NormalizedTimestamp expectedNextUtc =
      normalizeTimestamp(anchorUtc.secondsSince1900 + 1, anchorUtc.nanoseconds);
  bool labelAccepted = false;
  if (timestampsAreNear(reportedUtc, anchorUtc)) {
    // A report can remain buffered until just after its edge. The established
    // anchor makes that case unambiguous.
    if (PpsClock::isExpectedPulseInterval(intervalMicros)) {
      labelAccepted = ppsClock.setLabelledPulse(anchorPulseCount,
                                                anchorEdgeMicros,
                                                intervalMicros,
                                                reportedUtc);
      if (labelAccepted)
        timTpTargetPending = false;
    }
  }
  else if (timestampsAreNear(reportedUtc, expectedNextUtc)) {
    queueTimTpTarget(anchorPulseCount + 1, reportedUtc);
    updatePpsClockFromPulse();
    labelAccepted = true;
  }
  else if (reportedUtc.secondsSince1900 < anchorUtc.secondsSince1900 ||
           (reportedUtc.secondsSince1900 == anchorUtc.secondsSince1900 &&
            reportedUtc.nanoseconds < anchorUtc.nanoseconds)) {
    // A delayed automatic report is harmless once an anchor exists. Discard
    // it instead of ever guessing which already-passed pulse it described.
    return;
  }
  else {
    // A future label which disagrees with the running clock is a time step.
    // Drop the anchor and let the next freshly bracketed automatic report
    // establish a new target; never attach this report to a guessed edge.
    invalidatePpsTimebase();
    if (!ppsDiscontinuityReported) {
      ppsDiscontinuityReported = true;
      recordError("TP1 UTC label discontinuity; NTP paused for safe reacquisition");
    }
    return;
  }

  if (labelAccepted) {
    lastValidTimTpMillis = millis();
    validTimTpSeen = true;
    // Require an independently received, consecutive TIM-TP label to agree
    // with the first anchored pulse before the precision clock can serve NTP.
    if (!ppsClockConfirmed)
      Serial.println("NTP clock confirmed from TP1 and consecutive UTC TIM-TP labels");
    ppsClockConfirmed = true;
    ppsDiscontinuityReported = false;
    updatePpsClockFromPulse();
  }
}

bool getPpsTimestamp(const uint32_t captureMicros, NormalizedTimestamp* timestamp) {
  if (timestamp == nullptr || !ppsClockConfirmed || !validTimTpSeen ||
      static_cast<uint32_t>(millis() - lastValidTimTpMillis) > TIMTP_STALE_MILLIS)
    return false;

  uint32_t pulseCount = 0;
  uint32_t intervalMicros = 0;
  uint32_t edgeMicros = 0;
  uint32_t invalidIntervalCount = 0;
  getTimePulseStatus(&pulseCount,
                     &intervalMicros,
                     &edgeMicros,
                     &invalidIntervalCount);
  if (pulseCount < 2 || !PpsClock::isExpectedPulseInterval(intervalMicros) ||
      invalidIntervalCount != observedInvalidIntervalCount ||
      static_cast<uint32_t>(micros() - edgeMicros) > TIME_PULSE_STALE_MICROS)
    return false;

  return ppsClock.timestampAt(captureMicros, timestamp);
}

bool configureRtcXtOscillator() {
  // SparkFun's begin() selects the low-power RC oscillator. The RV1805
  // hundredths counter is valid only while the 32.768 kHz XT oscillator runs.
  uint8_t oscillatorStatus = rtc.readRegister(RV1805_OSC_STATUS);
  if (oscillatorStatus == 0xFF)
    return false;

  // RC mode leaves the oscillator-failure flag set. Clear the oscillator
  // flags before requesting XT, and enable failure fallback only after XT is
  // confirmed active.
  oscillatorStatus &= static_cast<uint8_t>(~RTC_OSCILLATOR_STATUS_FLAGS_MASK);
  if (!rtc.writeRegister(RV1805_OSC_STATUS, oscillatorStatus) ||
      !rtc.writeRegister(RV1805_CONF_KEY, RV1805_CONF_OSC) ||
      !rtc.writeRegister(RV1805_OSC_CTRL, RTC_XT_STARTUP_MODE))
    return false;

  const uint32_t startedMillis = millis();
  do {
    oscillatorStatus = rtc.readRegister(RV1805_OSC_STATUS);
    if (oscillatorStatus != 0xFF &&
        (oscillatorStatus & RTC_OSCILLATOR_MODE_RC_MASK) == 0)
      break;
    delay(1);
  } while (static_cast<uint32_t>(millis() - startedMillis) < RTC_XT_START_TIMEOUT_MS);

  if (oscillatorStatus == 0xFF ||
      (oscillatorStatus & RTC_OSCILLATOR_MODE_RC_MASK) != 0)
    return false;

  oscillatorStatus &= static_cast<uint8_t>(~RTC_OSCILLATOR_STATUS_FLAGS_MASK);
  if (!rtc.writeRegister(RV1805_OSC_STATUS, oscillatorStatus) ||
      !rtc.writeRegister(RV1805_CONF_KEY, RV1805_CONF_OSC) ||
      !rtc.writeRegister(RV1805_OSC_CTRL, RTC_XT_FAILURE_FALLBACK_MODE))
    return false;

  return rtc.readRegister(RV1805_OSC_CTRL) == RTC_XT_FAILURE_FALLBACK_MODE &&
         (rtc.readRegister(RV1805_OSC_STATUS) & RTC_OSCILLATOR_MODE_RC_MASK) == 0;
}

void timePulseInterrupt() {
  const uint32_t edgeMicros = micros();
  const uint32_t previousEdgeMicros = timePulseEdgeMicros;
  const uint32_t newPulseCount = timePulseCount + 1;

  timePulseEdgeMicros = edgeMicros;
  if (timePulseCount > 0) {
    timePulseIntervalMicros = edgeMicros - previousEdgeMicros;
    if (timePulseIntervalMicros < PpsClock::MIN_PULSE_INTERVAL_MICROS ||
        timePulseIntervalMicros > PpsClock::MAX_PULSE_INTERVAL_MICROS)
      ++timePulseInvalidIntervalCount;
  }
  timePulseCount = newPulseCount;
}

void getTimePulseStatus(uint32_t* pulseCount,
                        uint32_t* intervalMicros,
                        uint32_t* edgeMicros,
                        uint32_t* invalidIntervalCount) {
  noInterrupts();
  *pulseCount = timePulseCount;
  *intervalMicros = timePulseIntervalMicros;
  if (edgeMicros != nullptr)
    *edgeMicros = timePulseEdgeMicros;
  if (invalidIntervalCount != nullptr)
    *invalidIntervalCount = timePulseInvalidIntervalCount;
  interrupts();
}

void reportTimePulse() {
  static uint32_t reportedPulseCount = 0;
  static uint8_t initialReportsRemaining = 5;
  uint32_t pulseCount = 0;
  uint32_t intervalMicros = 0;
  getTimePulseStatus(&pulseCount, &intervalMicros);

  if (pulseCount == reportedPulseCount)
    return;

  reportedPulseCount = pulseCount;
  const bool unexpectedInterval = pulseCount > 1 &&
                                  (intervalMicros < 999000 || intervalMicros > 1001000);
  if (initialReportsRemaining > 0 || pulseCount % 60 == 0 || unexpectedInterval) {
    Serial.print("TP1 pulse ");
    Serial.print(pulseCount);
    if (pulseCount > 1) {
      Serial.print(", interval ");
      Serial.print(intervalMicros);
      Serial.print(" us");
    }
    if (unexpectedInterval)
      Serial.print(" (unexpected interval)");
    Serial.println();
    if (initialReportsRemaining > 0)
      --initialReportsRemaining;
  }
}

void setRtc() {
  // This is intentionally a request, not an immediate write. The RTC is written
  // once at the next labelled UTC pulse, then free-runs until another request.
  if (rtcSyncState != RtcSyncState::Idle)
    return;

  uint32_t pulseCount = 0;
  uint32_t intervalMicros = 0;
  getTimePulseStatus(&pulseCount, &intervalMicros);
  rtcReferencePulseCount = pulseCount;
  rtcSyncErrorReported = false;
  rtcSyncState = RtcSyncState::WaitForPulse;
}

void reportRtcSyncErrorOnce(const String& error) {
  if (!rtcSyncErrorReported) {
    rtcSyncErrorReported = true;
    addError(error);
  }
}

bool writeRtcAtCapturedPulse(const uint32_t capturedEdgeMicros,
                             const NormalizedTimestamp& utcAtEdge,
                             TimeData* writtenTime) {
  const uint32_t elapsedMicros = micros() - capturedEdgeMicros;
  if (elapsedMicros > RTC_CAPTURE_MAX_AGE_MICROS)
    return false;

  uint64_t effectiveNanoseconds = static_cast<uint64_t>(utcAtEdge.nanoseconds) +
                                  (static_cast<uint64_t>(elapsedMicros) + rtcWriteMicros) * 1000ULL;
  uint64_t secondsSince1900 = static_cast<uint64_t>(utcAtEdge.secondsSince1900) +
                              effectiveNanoseconds / 1000000000ULL;
  effectiveNanoseconds %= 1000000000ULL;

  if (!writtenTime->setSecondsSince1900(secondsSince1900))
    return false;
  writtenTime->setSubSec(static_cast<int32_t>(effectiveNanoseconds));
  if (writtenTime->getYear() < 2000 || writtenTime->getYear() > 2099)
    return false;

  // The RV1805 stores one-based weekdays with Sunday == 1. 1900-01-01 was Monday.
  const uint64_t daysSince1900 = secondsSince1900 / 86400ULL;
  const uint8_t weekday = static_cast<uint8_t>(((daysSince1900 + 1ULL) % 7ULL) + 1ULL);
  const uint8_t hundredths = static_cast<uint8_t>(effectiveNanoseconds / 10000000ULL);

  // Use the array overload to perform one I2C burst. The scalar overload first
  // performs an extra 12/24-hour register read, adding avoidable uncertainty.
  uint8_t rtcTime[] = {
    rtc.DECtoBCD(hundredths),
    rtc.DECtoBCD(writtenTime->getSec()),
    rtc.DECtoBCD(writtenTime->getMin()),
    rtc.DECtoBCD(writtenTime->getHour()),
    rtc.DECtoBCD(writtenTime->getDay()),
    rtc.DECtoBCD(writtenTime->getMonth()),
    rtc.DECtoBCD(static_cast<uint8_t>(writtenTime->getYear() - 2000)),
    rtc.DECtoBCD(weekday)
  };

  const uint32_t writeStartedMicros = micros();
  const bool writeSucceeded = rtc.setTime(rtcTime, sizeof(rtcTime));
  const uint32_t measuredWriteMicros = micros() - writeStartedMicros;
  if (writeSucceeded && measuredWriteMicros > 0 && measuredWriteMicros < 10000)
    rtcWriteMicros = measuredWriteMicros;
  return writeSucceeded;
}

void serviceRtcSync() {
  if (rtcSyncState == RtcSyncState::Idle)
    return;
  if (!rtcAvailable) {
    reportRtcSyncErrorOnce("RTC synchronization unavailable: RTC was not detected");
    rtcSyncState = RtcSyncState::Idle;
    return;
  }

  uint32_t pulseCount = 0;
  uint32_t intervalMicros = 0;
  uint32_t edgeMicros = 0;
  getTimePulseStatus(&pulseCount, &intervalMicros, &edgeMicros);

  if (pulseCount == rtcReferencePulseCount)
    return;
  rtcReferencePulseCount = pulseCount;

  if (pulseCount < 2 || !PpsClock::isExpectedPulseInterval(intervalMicros) ||
      !ppsClockConfirmed || !validTimTpSeen ||
      static_cast<uint32_t>(millis() - lastValidTimTpMillis) > TIMTP_STALE_MILLIS) {
    reportRtcSyncErrorOnce("RTC synchronization waiting for a valid UTC TP1 timebase");
    return;
  }

  updatePpsClockFromPulse();
  uint32_t anchorPulseCount = 0;
  uint32_t anchorEdgeMicros = 0;
  NormalizedTimestamp utcAtEdge = {};
  if (!ppsClock.getAnchor(&anchorPulseCount, &anchorEdgeMicros, &utcAtEdge) ||
      anchorPulseCount != pulseCount || anchorEdgeMicros != edgeMicros) {
    reportRtcSyncErrorOnce("RTC synchronization waiting for a labelled TP1 edge");
    return;
  }

  TimeData writtenTime;
  if (!writeRtcAtCapturedPulse(edgeMicros, utcAtEdge, &writtenTime)) {
    rtcAvailable = false;
    rtcSyncState = RtcSyncState::Idle;
    lastRtcInitializationAttemptMillis =
        millis() - RTC_INITIALIZATION_RETRY_MILLIS;
    reportRtcSyncErrorOnce("RTC synchronization write failed; reinitializing RTC");
    return;
  }

  rtcSyncState = RtcSyncState::Idle;
  rtcSyncErrorReported = false;
  rtcSetTimerMs = 0;
  addLog(String("Set RTC from UTC TP1 pulse: ") + writtenTime.getISO8601Time(2));
}

String getRtcISO8601Time() {
  if (!rtcAvailable)
    return String("");

  RtcDateTime rtcTime = {};
  const RtcTimestampReadStatus status = readRtcDateTime(&rtcTime);
  if (status == RtcTimestampReadStatus::TransportFailure) {
    rtcAvailable = false;
    lastRtcInitializationAttemptMillis =
        millis() - RTC_INITIALIZATION_RETRY_MILLIS;
    if (!rtcReadErrorReported) {
      rtcReadErrorReported = true;
      recordRtcTimestampError("RTC returned an incomplete or invalid timestamp; retrying");
    }
    return String("");
  }
  if (status == RtcTimestampReadStatus::InvalidTimestamp) {
    if (rtcSyncState == RtcSyncState::Idle)
      setRtc();
    if (!rtcReadErrorReported) {
      rtcReadErrorReported = true;
      recordRtcTimestampError("RTC returned an invalid timestamp; waiting for UTC TP1 synchronization");
    }
    return String("");
  }
  rtcReadErrorReported = false;

  return t.toISO8601Time(rtcTime.year,
                         rtcTime.month,
                         rtcTime.day,
                         rtcTime.hour,
                         rtcTime.minute,
                         rtcTime.second,
                         rtcTime.hundredths,
                         2);
}

String getRtcWebISO8601Time() {
  uint8_t oscillatorStatus = 0;
  const bool oscillatorStatusRead =
      rtcAvailable && readRtcRegisters(RV1805_OSC_STATUS, &oscillatorStatus, 1);
  if (oscillatorStatusRead) {
    const bool hundredthsAvailable =
        (oscillatorStatus & RTC_OSCILLATOR_MODE_RC_MASK) == 0;
    if (rtcHundredthsAvailable && !hundredthsAvailable)
      recordError("RTC XT oscillator unavailable; web fractional time disabled");
    rtcHundredthsAvailable = hundredthsAvailable;
  }

  String rtcTime = getRtcISO8601Time();
  if (rtcTime.length() == 0 || rtcHundredthsAvailable)
    return rtcTime;  // NOLINT(clang-diagnostic-nrvo)

  if (rtcTime.length() >= 3)
    rtcTime.remove(rtcTime.length() - 3);
  return rtcTime + " (fraction unavailable: RTC using RC oscillator)";
}

bool bindNtpUdpSocket() {
  if (ntpUdpBound)
    udp.stop();

  ntpUdpBound = false;
  ntpSocketNumber = MAX_SOCK_NUM;
  if (udp.begin(ntpPort) != 0) {
    ntpSocketNumber = findNtpSocketNumber();
    ntpUdpBound = ntpSocketNumber < MAX_SOCK_NUM;
    if (!ntpUdpBound)
      udp.stop();
  }
  if (!ntpUdpBound) {
    if (!ntpBindFailureReported) {
      ntpBindFailureReported = true;
      recordError("Could not bind the NTP UDP socket to port 123; retrying");
    }
    return false;
  }

  if (ntpBindFailureReported)
    Serial.println("NTP UDP socket recovered on port 123");
  ntpBindFailureReported = false;
  return true;
}

bool discardCurrentUdpPacket() {
  uint8_t discardBuffer[32];
  while (udp.available() > 0) {
    const int availableBytes = udp.available();
    const std::size_t bytesToRead = availableBytes > static_cast<int>(sizeof(discardBuffer))
                                   ? sizeof(discardBuffer)
                                   : static_cast<std::size_t>(availableBytes);
    if (udp.read(discardBuffer, bytesToRead) <= 0) {
      // Teensy's EthernetUDP::parsePacket loops forever if its private
      // remaining-byte count is nonzero and a later socket read keeps failing.
      // Take NTP offline and rebind the socket before parsePacket is called
      // again; a successful begin() clears that stale count.
      requestEthernetSocketRecovery("could not drain a partial NTP datagram", true);
      return false;
    }
  }
  return true;
}

void processNtpRequest(const int packetSize, const uint32_t receiveCaptureMicros) {
  const IPAddress remoteIp = udp.remoteIP();
  const uint16_t remotePort = udp.remotePort();

  if (packetSize != static_cast<int>(NTP_PACKET_SIZE)) {
    discardCurrentUdpPacket();
    addError("Invalid NTP client request from " + properties.generateIpString(remoteIp) +
             ", packet size " + String(packetSize));
    return;
  }

  uint8_t request[NTP_PACKET_SIZE] = {};
  const int bytesRead = udp.read(request, sizeof(request));
  if (bytesRead != static_cast<int>(NTP_PACKET_SIZE)) {
    discardCurrentUdpPacket();
    addError("Invalid NTP client request from " + properties.generateIpString(remoteIp) +
             ", bytes read " + String(bytesRead));
    return;
  }

  const NtpResponseStatus requestStatus = validateNtpRequest(request, sizeof(request));
  if (requestStatus != NtpResponseStatus::Ready) {
    addError("Invalid NTP client request from " + properties.generateIpString(remoteIp));
    return;
  }

  NormalizedTimestamp referenceTime = {};
  NormalizedTimestamp receiveTime = {};
  const char* deferredNtpClockError = nullptr;
  bool timeAvailable = getPpsTimestamp(receiveCaptureMicros, &receiveTime);
  if (timeAvailable) {
    uint32_t referencePulseCount = 0;
    uint32_t referenceEdgeMicros = 0;
    timeAvailable = ppsClock.getAnchor(&referencePulseCount,
                                       &referenceEdgeMicros,
                                       &referenceTime);
  }
  if (!timeAvailable) {
    if (!ntpClockUnavailableReported) {
      ntpClockUnavailableReported = true;
      deferredNtpClockError =
          "NTP clock unsynchronized: waiting for a confirmed UTC TP1/TIM-TP timebase";
    }
  }
  else {
    ntpClockUnavailableReported = false;
  }

  uint8_t response[NTP_PACKET_SIZE] = {};
  const NtpResponseStatus responseStatus = createNtpResponse(request,
                                                             sizeof(request),
                                                             referenceTime,
                                                             receiveTime,
                                                             receiveTime,
                                                             timeAvailable,
                                                             response,
                                                             sizeof(response));
  if (responseStatus != NtpResponseStatus::Ready) {
    if (deferredNtpClockError != nullptr)
      recordError(deferredNtpClockError);
    return;
  }

  if (udp.beginPacket(remoteIp, remotePort) == 0) {
    if (deferredNtpClockError != nullptr)
      recordError(deferredNtpClockError);
    addError("Could not begin NTP response to " + properties.generateIpString(remoteIp));
    requestEthernetSocketRecovery("could not begin an NTP response", true);
    return;
  }

  constexpr std::size_t TRANSMIT_TIMESTAMP_OFFSET = 40;
  if (timeAvailable) {
    // Capture T3 after beginPacket's setup work but before the one and only UDP
    // payload write, keeping it close to the actual W5500 transmission.
    const uint32_t transmitCaptureMicros = micros();
    NormalizedTimestamp transmitTime = {};
    if (getPpsTimestamp(transmitCaptureMicros, &transmitTime)) {
      writeNtpTimestamp(response + TRANSMIT_TIMESTAMP_OFFSET, toNtpTimestamp(transmitTime));
    }
    else {
      timeAvailable = false;
      createNtpResponse(request,
                        sizeof(request),
                        {},
                        {},
                        {},
                        false,
                        response,
                        sizeof(response));
      if (!ntpClockUnavailableReported) {
        ntpClockUnavailableReported = true;
        deferredNtpClockError =
            "NTP clock became unsynchronized while constructing a response";
      }
    }
  }

  // Teensy's EthernetUDP applies its cumulative offset twice across multiple
  // writes. One 48-byte write is required to produce one valid 48-byte NTP
  // datagram and to keep T3 at bytes 40-47.
  if (udp.write(response, sizeof(response)) != sizeof(response)) {
    if (deferredNtpClockError != nullptr)
      recordError(deferredNtpClockError);
    addError("Could not write NTP response to " + properties.generateIpString(remoteIp));
    requestEthernetSocketRecovery("could not write an NTP response", true);
    return;
  }

  if (udp.endPacket() == 0) {
    if (deferredNtpClockError != nullptr)
      recordError(deferredNtpClockError);
    addError("Could not send NTP response to " + properties.generateIpString(remoteIp));
    requestEthernetSocketRecovery("could not send an NTP response", true);
    return;
  }

  // RTC I2C and dynamic String/list work happen only after the response left.
  if (deferredNtpClockError != nullptr)
    recordError(deferredNtpClockError);
  addLog(String(timeAvailable ? "Synchronized" : "Unsynchronized") +
         " NTP response to " + properties.generateIpString(remoteIp) +
         ", port " + String(remotePort));
}

namespace {

constexpr char ENTRY_TIMESTAMP_PLACEHOLDER[] = "____-__-__T__:__:__.__";
static_assert(sizeof(ENTRY_TIMESTAMP_PLACEHOLDER) - 1 == 22,
              "Entry timestamp placeholder must match YYYY-MM-DDTHH:MM:SS.hh");

String getPpsISO8601Time(const uint8_t decimalPrecision) {
  NormalizedTimestamp timestamp = {};
  if (!getPpsTimestamp(micros(), &timestamp) ||
      timestamp.secondsSince1900 < 0)
    return "";

  TimeData gpsTime;
  if (!gpsTime.setSecondsSince1900(
          static_cast<uint64_t>(timestamp.secondsSince1900)))
    return "";

  gpsTime.setSubSec(static_cast<int32_t>(timestamp.nanoseconds));
  return gpsTime.getISO8601Time(decimalPrecision);
}

String getFallbackISO8601Time() {
  String timestamp = getPpsISO8601Time(2);
  if (timestamp.length() > 0)
    return timestamp;

  return String(ENTRY_TIMESTAMP_PLACEHOLDER);
}

String getEntryISO8601Time() {
  String timestamp = getRtcISO8601Time();
  if (timestamp.length() > 0)
    return timestamp;

  return getFallbackISO8601Time();
}

void appendTimestampedError(String error) {
  if (properties.getErrorMax() == 0) {
    Serial.println(error);
    return;
  }
  if (static_cast<uint16_t>(errorLog.size()) >= properties.getErrorMax())
    errorLog.pop_back();
  errorLog.push_front(error);
  Serial.println(error);
}

void recordRtcTimestampError(String error) {
  // RTC acquisition generated this error, so retrying the RTC to timestamp it
  // would recurse. PPS and the fixed placeholder are the remaining two steps
  // of the normal entry timestamp selection.
  appendTimestampedError(getFallbackISO8601Time() + " - " + error);
}

} // namespace

String getGpsISO8601Time() {
  return getPpsISO8601Time(6);
}

void addLog(String log) {
  if (properties.getLogMax() == 0)
    return;
  if (static_cast<uint16_t>(usageLog.size()) >= properties.getLogMax()) {
    usageLog.pop_back();
  }
  usageLog.push_front(getEntryISO8601Time() + " - " + log);
}

void addError(String error) {
  recordError(error);
}

void recordError(String error) {
  appendTimestampedError(getEntryISO8601Time() + " - " + error);
}

// Display text on the OLED screen
void displaySettings() {
  if (!oledAvailable || properties.getDisplayOn() != 1)
    return;

  String strText = "";
  int xText = 5;
  int yText = 1;

  // Clear screen
  myOLED.erase();

  // GPS Fix Type
  strText = "GPS: " + gpsFixType; // gpsFixType is a global variable
  myOLED.setFont(&QW_FONT_8X16);
  // Draw the text
  myOLED.text(xText, yText, strText);

  // IP Address (label)
  strText = "IP Address:";
  myOLED.setFont(&QW_FONT_5X7);
  // Leave a small blank from the title
  yText = yText + 20;
  // Draw the text
  myOLED.text(xText, yText, strText);
  // IP Address (value)
  strText = strLocalIp;
  myOLED.setFont(&QW_FONT_5X7);
  // Leave a small blank from the title
  yText = yText + myOLED.getStringHeight(strText) + 2;
  // Draw the text
  myOLED.text(xText, yText, strText);

  // Subnet (label)
  strText = "Subnet:";
  myOLED.setFont(&QW_FONT_5X7);
  // Leave a small blank from the title
  yText = yText + myOLED.getStringHeight(strText) + 5;
  // Draw the text
  myOLED.text(xText, yText, strText);
  // IP Address (value)
  strText = strSubnet;
  myOLED.setFont(&QW_FONT_5X7);
  // Leave a small blank from the title
  yText = yText + myOLED.getStringHeight(strText) + 2;
  // Draw the text
  myOLED.text(xText, yText, strText);

  // Send the graphics to the device
  myOLED.display();
}
