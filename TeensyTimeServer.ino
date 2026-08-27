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

// Forward declarations keep the sketch valid for standard C++ IntelliSense.
void getDeviceConfig();
void setDeviceConfig();
bool configureAutomaticNavPvt();
void cacheNavPvtData(const UBX_NAV_PVT_data_t& data);
void navPvtCallback(UBX_NAV_PVT_data_t* data);
void serviceCachedGnssStatus();
bool configureTimePulse();
bool initializePpsTimebase();
bool acquirePpsTimebase();
void drainGnssBeforeTimTpBoundary();
void servicePpsTimebase();
void updatePpsClockFromPulse();
bool getPpsTimestamp(uint32_t captureMicros, NormalizedTimestamp* timestamp);
bool configureRtcXtOscillator();
void setRtc();
void serviceRtcSync();
String getRtcISO8601Time();
String getRtcWebISO8601Time();
bool bindNtpUdpSocket();
void discardCurrentUdpPacket();
void processNtpRequest(int packetSize, uint32_t receiveCaptureMicros);
void timePulseInterrupt();
void getTimePulseStatus(uint32_t* pulseCount,
                        uint32_t* intervalMicros,
                        uint32_t* edgeMicros = nullptr,
                        uint32_t* invalidIntervalCount = nullptr);
void reportTimePulse();
String getGpsFix();
uint8_t getGpsSignals();
String getGpsISO8601Time();
void addLog(String log);
void addError(String error);
void recordError(String error);
void displaySettings();

const char* APP_NAME = "GPS NTP Time Server";
const char* VERSION = "2.0";
const char* AUTHOR = "Andrew Kevin Bailey";

/**** Setup Properties init *****/
Properties properties;

/***** Ethernet init *****/
byte mac[] = { 0xBC, 0xED, 0x5D, 0x3E, 0x94, 0xB6 };
EthernetUDP udp;
EthernetServer httpServer(80);
bool ntpUdpBound = false;
bool ntpBindFailureReported = false;
uint32_t lastNtpBindAttemptMillis = 0;
// Local IP addresses for display
String strLocalIp = "";
String strSubnet = "";
String strDns1Ip = "";
String strDns2Ip = "";
String strGatewayIp = "";

/***** Time server init *****/
const unsigned int ntpPort = 123;
constexpr uint32_t NTP_BIND_RETRY_MILLIS = 1000;
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

RtcSyncState rtcSyncState = RtcSyncState::Idle;
uint32_t rtcReferencePulseCount = 0;
uint32_t rtcWriteMicros = DEFAULT_RTC_WRITE_MICROS;
bool rtcSyncErrorReported = false;

/***** Logging data init *****/
std::list<String> usageLog;
std::list<String> errorLog;
String configLog;

/***** OLED Display init *****/
Qwiic1in3OLED myOLED;
bool isDisplayInverted = false;

/***** Timer init *****/
elapsedMillis refreshTimerMs;
elapsedMillis rtcSetTimerMs;

/***** GPS init *****/
String gpsFixType = "";
SFE_UBLOX_GNSS myGNSS;
constexpr uint32_t NAVIGATION_EPOCH_MILLIS = 1000;
constexpr uint16_t NAV_PVT_COMMAND_MAX_WAIT_MILLIS = 250;
constexpr uint32_t NAV_PVT_INITIAL_SAMPLE_WAIT_MILLIS = 1200;
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
bool ppsAcquisitionAttempted = false;
uint32_t lastPpsAcquisitionAttemptMillis = 0;
uint32_t observedInvalidIntervalCount = 0;

/***** HTTP server init *****/
TimeHttp timeHttp;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
  Wire.setClock(400000);
  Wire.begin();
  delay(1000);

  /***** Begin real time clock (RTC) setup *****/ 
  rtcAvailable = rtc.begin();
  if (!rtcAvailable) {
    Serial.println("ERROR: Real time clock (RTC) was not found");
  }
  else {
    rtcHundredthsAvailable = configureRtcXtOscillator();
    if (!rtcHundredthsAvailable)
      Serial.println("ERROR: RTC crystal oscillator configuration failed; hundredths are unavailable");
    rtc.clearInterrupts();
    rtc.setStaticPowerSwitchOutput(false);
    rtc.setPowerSwitchLock(true);
    rtc.setAlarmMode(0);
    rtc.set24Hour();
  }
  /***** End real time clock (RTC) setup *****/ 

  /***** Begin load data from properties *****/ 
  if (!properties.loadProperties())
    addError("Error loading setup properties from non-volatile storage");
  /***** End load data from properties *****/ 
  
  /***** Begin Ethernet and UDP setup *****/
  if (properties.isDhcp()) {
    while (Ethernet.begin(mac) == 0) {
      Serial.println("Waiting on DHCP...");
      delay(1000);
    }
    strLocalIp = properties.generateIpString(Ethernet.localIP());
    strSubnet = properties.generateIpString(Ethernet.subnetMask());
    strDns1Ip = properties.generateIpString(Ethernet.dnsServerIP());
    strDns2Ip = "0.0.0.0";
    strGatewayIp = properties.generateIpString(Ethernet.gatewayIP());
  }
  else {
    Ethernet.begin(mac, properties.getLocalIp(), properties.getDns1Ip(), properties.getGatewayIp(), properties.getSubnet());
    strLocalIp = properties.getLocalIpStr();
    strSubnet = properties.getSubnetStr();
    strDns1Ip = properties.getDns1IpStr();
    strDns2Ip = properties.getDns2IpStr();
    strGatewayIp = properties.getGatewayIpStr();
  }

  // Check for Ethernet hardware present
  if (Ethernet.hardwareStatus() == EthernetNoHardware) {
    Serial.println("ERROR: Ethernet was not found.  Sorry, can't run without hardware");
    while (true) { // no point in carrying on, so do nothing forevermore:
      delay(1);
    }
  } 
  else if (Ethernet.linkStatus() == LinkOFF) {
    Serial.println("ERROR: Ethernet cable is not connected");
    while (true) { // no point in carrying on, so do nothing forevermore:
      delay(1);
    }
  }
  if (bindNtpUdpSocket())
    Serial.println("NTP UDP socket listening on port 123");
  /***** End Ethernet and UDP setup *****/ 

  /***** Begin GPS setup *****/ 
  while (myGNSS.begin() == false) { // connect to the u-blox module using our custom port and address
    addError("u-blox GNSS not detected. Retrying...");
    delay (1000);
  }
  setDeviceConfig();
  if (configureTimePulse())
    addLog("GNSS TP1 configured for a UTC-aligned 1 Hz rising edge");
  else
    addError("Could not configure GNSS TP1");
  if (configureAutomaticNavPvt())
    addLog("Automatic UBX-NAV-PVT configured every " + String(navPvtEpochRate) + " navigation epoch(s)");
  else
    addError("Could not configure the requested automatic UBX-NAV-PVT rate");

  pinMode(TIME_PULSE_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(TIME_PULSE_PIN), timePulseInterrupt, RISING);
  addLog("GPS receiver config was set");
  if (initializePpsTimebase())
    addLog("Automatic UBX-TIM-TP enabled for the NTP timebase");
  else
    addError("Could not enable automatic UBX-TIM-TP");
  // Allocate memory for configLog after PPS initialization so the reported
  // timebase state reflects the state which will serve NTP.
  getDeviceConfig();
  if (configLog.length() > 24) { // the string length should be greater than 24 chars
    Serial.println(F("**************************************************"));
    Serial.println(configLog);
  }
  setRtc(); // Request one PPS-aligned synchronization at startup.
  /***** End GPS setup *****/

  /***** Begin Display setup *****/
  if (myOLED.begin() == false) {
    Serial.println("ERROR: Device begin failed. Freezing...");
    while (true);
  }
  myOLED.erase();
  myOLED.display();
  serviceCachedGnssStatus();
  displaySettings();
  properties.getDisplayOn() == 1 ? myOLED.displayPower(true) :  myOLED.displayPower(false);
  myOLED.invert(isDisplayInverted);
  /***** End Display setup *****/ 
  
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
  httpServer.begin();
  Serial.println("HTTP server listening on port 80");
  /***** End HTTP setup *****/ 

  // Reset timers
  refreshTimerMs = 0;
  rtcSetTimerMs = 0;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void loop() {
  // Apply the most recent TP1 edge before checking Ethernet. This is only local
  // arithmetic; GNSS I2C work is serviced after the NTP critical path.
  updatePpsClockFromPulse();

  /***** Begin NTP server *****/
  if (!ntpUdpBound) {
    if (static_cast<uint32_t>(millis() - lastNtpBindAttemptMillis) >= NTP_BIND_RETRY_MILLIS)
      bindNtpUdpSocket();
  }
  else {
    int packetSize = udp.parsePacket();
    if (packetSize) {
      const uint32_t receiveCaptureMicros = micros();
      processNtpRequest(packetSize, receiveCaptureMicros);
    }
  }
  /***** End NTP server *****/

  servicePpsTimebase();
  updatePpsClockFromPulse();
  myGNSS.checkCallbacks();
  serviceCachedGnssStatus();
  reportTimePulse();
  serviceRtcSync();

  /***** Begin HTTP server *****/
  EthernetClient httpClient = httpServer.available();
  if (httpClient) {
    timeHttp.processRequest(&httpClient, strLocalIp, gpsFixType);
  }
  /***** End HTTP server *****/

  // To avoid having delays in loop, we'll use the strategy from BlinkWithoutDelay
  // see: File -> Examples -> 02.Digital -> BlinkWithoutDelay for more info
  if (refreshTimerMs > properties.getRefreshFrequency()) {
    digitalWrite(LED_BUILTIN, digitalRead(LED_BUILTIN) == HIGH ? LOW : HIGH);  // toggle LED
    
    // Refresh the display
    displaySettings();
    properties.getDisplayOn() == 1 ? myOLED.displayPower(true) : myOLED.displayPower(false);
    // If selected, invert the display to prevet burn in
    if (properties.getDisplayAlternate() == 1) { 
      myOLED.invert(isDisplayInverted);
      isDisplayInverted = !isDisplayInverted; 
    }
    refreshTimerMs = 0;
  }

  // To avoid having delays in loop, we'll use the strategy from BlinkWithoutDelay
  // see: File -> Examples -> 02.Digital -> BlinkWithoutDelay for more info
  const uint32_t rtcSetFrequencyMs = properties.getRtcSetFrequency();
  // Zero disables periodic synchronization; the startup request still runs.
  if (rtcSetFrequencyMs > 0 && rtcSetTimerMs >= rtcSetFrequencyMs) {
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

  // Wait for the device to fully connect
  while (myGNSS.getFirmwareType() == nullptr || strcmp(myGNSS.getFirmwareType(), "TBD") == 0)
      delay(1000);

  configLog += "\n\nGNSS configuration:";
  configLog += "\n GPS module name = " + String(myGNSS.getModuleName());
  configLog += "\n GPS firmware type = " + String(myGNSS.getFirmwareType());
  configLog += "\n GPS firmware version = " + String(myGNSS.getFirmwareVersionHigh()) + String(myGNSS.getFirmwareVersionLow());
  configLog += "\n I2C transaction size = " + String(myGNSS.getI2CTransactionSize());
  configLog += "\n I2C timeout = " + String(Wire.getTimeout());
  configLog += "\n CFG-SPI-ENABLED = " + String(myGNSS.getVal8(UBLOX_CFG_SPI_ENABLED));
  configLog += "\n CFG-UART1-ENABLED = " + String(myGNSS.getVal8(UBLOX_CFG_UART1_ENABLED));
  configLog += "\n CFG-UART2-ENABLED = " + String(myGNSS.getVal8(UBLOX_CFG_UART2_ENABLED));
  configLog += "\n CFG-USB-ENABLED = " + String(myGNSS.getVal8(UBLOX_CFG_USB_ENABLED));
  configLog += "\n CFG-I2CINPROT-UBX = " + String(myGNSS.getVal8(UBLOX_CFG_I2CINPROT_UBX));
  configLog += "\n CFG-I2COUTPROT-UBX = " + String(myGNSS.getVal8(UBLOX_CFG_I2COUTPROT_UBX));
  configLog += "\n CFG-I2CINPROT-NMEA = " + String(myGNSS.getVal8(UBLOX_CFG_I2CINPROT_NMEA));
  configLog += "\n CFG-I2COUTPROT-NMEA = " + String(myGNSS.getVal8(UBLOX_CFG_I2COUTPROT_NMEA));
  configLog += "\n CFG-I2CINPROT-RTCM3X = " + String(myGNSS.getVal8(UBLOX_CFG_I2CINPROT_RTCM3X));
  configLog += "\n CFG-I2COUTPROT-RTCM3X = " + String(myGNSS.getVal8(UBLOX_CFG_I2COUTPROT_RTCM3X));
  configLog += "\n CFG-I2C-EXTENDEDTIMEOUT = " + String(myGNSS.getVal8(UBLOX_CFG_I2C_EXTENDEDTIMEOUT));
  configLog += "\n CFG-CLOCK-OSC-FREQ = " + String(myGNSS.getVal8(UBLOX_CFG_CLOCK_OSC_FREQ));
  configLog += "\n CFG-RATE-MEAS = " + String(myGNSS.getVal16(UBLOX_CFG_RATE_MEAS));
  configLog += "\n CFG-RATE-NAV = " + String(myGNSS.getVal16(UBLOX_CFG_RATE_NAV));
  configLog += "\n CFG-MSGOUT-UBX_NAV_PVT_I2C = " + String(myGNSS.getVal8(UBLOX_CFG_MSGOUT_UBX_NAV_PVT_I2C));
  configLog += "\n CFG-MSGOUT-UBX_NAV_TIMEUTC_I2C = " + String(myGNSS.getVal8(UBLOX_CFG_MSGOUT_UBX_NAV_TIMEUTC_I2C));
  configLog += "\n CFG-MSGOUT-UBX_TIM_TP_I2C = " + String(myGNSS.getVal8(UBLOX_CFG_MSGOUT_UBX_TIM_TP_I2C));
  configLog += "\n CFG-TP-FREQ-TP1 = " + String(myGNSS.getVal32(UBLOX_CFG_TP_FREQ_TP1));
  configLog += "\n CFG-TP-FREQ-LOCK-TP1 = " + String(myGNSS.getVal32(UBLOX_CFG_TP_FREQ_LOCK_TP1));
  configLog += "\n CFG-TP-LEN-TP1 = " + String(myGNSS.getVal32(UBLOX_CFG_TP_LEN_TP1));
  configLog += "\n CFG-TP-LEN-LOCK-TP1 = " + String(myGNSS.getVal32(UBLOX_CFG_TP_LEN_LOCK_TP1));
  configLog += "\n CFG-TP-TIMEGRID-TP1 = " + String(myGNSS.getVal8(UBLOX_CFG_TP_TIMEGRID_TP1));
  configLog += "\n CFG-NAVSPG-DYNMODEL = " + String(myGNSS.getVal8(UBLOX_CFG_NAVSPG_DYNMODEL));
  configLog += "\n CFG-SIGNAL-GAL_ENA = " + String(myGNSS.getVal8(UBLOX_CFG_SIGNAL_GAL_ENA));
  configLog += "\n CFG-SIGNAL-GAL_E1_ENA = " + String(myGNSS.getVal8(UBLOX_CFG_SIGNAL_GAL_E1_ENA));
  configLog += "\n CFG-SIGNAL-GAL_E5A_ENA = " + String(myGNSS.getVal8(UBLOX_CFG_SIGNAL_GAL_E5A_ENA));
  configLog += "\n CFG-SIGNAL-GAL_E5B_ENA = " + String(myGNSS.getVal8(UBLOX_CFG_SIGNAL_GAL_E5B_ENA));
  configLog += "\n CFG-SIGNAL-GPS_ENA = " + String(myGNSS.getVal8(UBLOX_CFG_SIGNAL_GPS_ENA));
  configLog += "\n CFG-SIGNAL-GPS_L1CA_ENA = " + String(myGNSS.getVal8(UBLOX_CFG_SIGNAL_GPS_L1CA_ENA));
  configLog += "\n CFG-SIGNAL-GPS_L2C_ENA = " + String(myGNSS.getVal8(UBLOX_CFG_SIGNAL_GPS_L2C_ENA));
  configLog += "\n CFG-SIGNAL-GPS_L5_ENA = " + String(myGNSS.getVal8(UBLOX_CFG_SIGNAL_GPS_L5_ENA));
  configLog += "\n CFG-SIGNAL-BDS_ENA= " + String(myGNSS.getVal8(UBLOX_CFG_SIGNAL_BDS_ENA));
  configLog += "\n CFG-SIGNAL-GLO_ENA = " + String(myGNSS.getVal8(UBLOX_CFG_SIGNAL_GLO_ENA));
  configLog += "\n CFG-SIGNAL-SBAS_ENA = " + String(myGNSS.getVal8(UBLOX_CFG_SIGNAL_SBAS_ENA));
  configLog += "\n CFG-SIGNAL-QZSS_ENA= " + String(myGNSS.getVal8(UBLOX_CFG_SIGNAL_QZSS_ENA));
  configLog += "\n CFG-SPI-ENABLED = " + String(myGNSS.getVal8(UBLOX_CFG_SPI_ENABLED));
  // Get the number of leap seconds since 1980
  for (int i = 0; i < 5 && !myGNSS.getLeapSecondEvent(); i++) {
    t.setLeapSecondsSince1980(myGNSS.packetUBXNAVTIMELS->data.currLs);
    if (i == 4) {
      addError("Could not get leap seconds from GPS.  Defaulting to 27 seconds (2026-01-01).");
      t.setLeapSecondsSince2025();
    }
  }
  if (t.getTotalLeapSeconds() < 27) { // there was a race condition, try setting the leap seconds again
    delay(1000);
    t.setLeapSecondsSince1980(myGNSS.packetUBXNAVTIMELS->data.currLs);
  }
  configLog += "\n GPS leap seconds = " + String(myGNSS.packetUBXNAVTIMELS->data.currLs);
  configLog += "\n Total leap seconds = " + String(myGNSS.packetUBXNAVTIMELS->data.currLs + LEAP_SECONDS_1980);
  configLog += "\n GPS fix type = " + getGpsFix();
  configLog += "\n Number of GPS signals = " + String(getGpsSignals());

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
}

void setDeviceConfig() {
  myGNSS.setVal8(UBLOX_CFG_I2C_ENABLED, 1);
  myGNSS.setVal8(UBLOX_CFG_I2CINPROT_UBX, 1);
  myGNSS.setVal8(UBLOX_CFG_I2COUTPROT_UBX, 1);
  myGNSS.setVal8(UBLOX_CFG_I2CINPROT_NMEA, 0);
  myGNSS.setVal8(UBLOX_CFG_I2COUTPROT_NMEA, 0);
  myGNSS.setVal8(UBLOX_CFG_I2CINPROT_RTCM3X, 0);
  myGNSS.setVal8(UBLOX_CFG_I2COUTPROT_RTCM3X, 0);
  myGNSS.setVal8(UBLOX_CFG_I2CINPROT_SPARTN, 0);
  myGNSS.setVal8(UBLOX_CFG_SPI_ENABLED, 0);
  myGNSS.setVal8(UBLOX_CFG_SPI_ENABLED, 0);
  myGNSS.setVal8(UBLOX_CFG_USB_ENABLED, 0);
  myGNSS.setVal8(UBLOX_CFG_UART1_ENABLED, 0);
  myGNSS.setVal8(UBLOX_CFG_UART2_ENABLED, 0);
  myGNSS.setDynamicModel(DYN_MODEL_STATIONARY);
  myGNSS.setVal8(UBLOX_CFG_I2C_EXTENDEDTIMEOUT, 0);
  // Only use GPS satellites
  myGNSS.setVal8(UBLOX_CFG_SIGNAL_GAL_ENA, 0);
  myGNSS.setVal8(UBLOX_CFG_SIGNAL_GAL_E1_ENA, 0);
  myGNSS.setVal8(UBLOX_CFG_SIGNAL_GAL_E5A_ENA, 0);
  myGNSS.setVal8(UBLOX_CFG_SIGNAL_GAL_E5B_ENA, 0);
  myGNSS.setVal8(UBLOX_CFG_SIGNAL_GPS_ENA, 1);
  myGNSS.setVal8(UBLOX_CFG_SIGNAL_GPS_L1CA_ENA, 1);
  myGNSS.setVal8(UBLOX_CFG_SIGNAL_GPS_L2C_ENA, 1);
  myGNSS.setVal8(UBLOX_CFG_SIGNAL_GPS_L5_ENA, 1);
  myGNSS.setVal8(UBLOX_CFG_SIGNAL_BDS_ENA, 0);
  myGNSS.setVal8(UBLOX_CFG_SIGNAL_GLO_ENA, 0);
  myGNSS.setVal8(UBLOX_CFG_SIGNAL_SBAS_ENA, 0);
  myGNSS.setVal8(UBLOX_CFG_SIGNAL_QZSS_ENA, 0);

  // u-blox recommends a 1 Hz measurement rate and a 1 Hz pulse when using
  // UBX-TIM-TP. RATE-NAV is a 16-bit ratio and must be at least one.
  const bool measurementRateSet = myGNSS.setMeasurementRate(1000);
  const bool navigationRateSet = myGNSS.setNavigationRate(1);
  myGNSS.setI2CpollingWait(20); // Read automatic TIM-TP promptly without busy-polling I2C.
  if (!measurementRateSet || !navigationRateSet)
    addError("Could not configure the GNSS 1 Hz timing rate");
  myGNSS.setVal8(UBLOX_CFG_RATE_TIMEREF, 0); // 0 = UTC
  myGNSS.setVal8(UBLOX_CFG_MSGOUT_UBX_NAV_PVT_I2C, 0); // output rate of the UBX-NAV-PVT message on port I2C
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

void navPvtCallback(UBX_NAV_PVT_data_t* data) {
  if (data != nullptr)
    cacheNavPvtData(*data);
}

bool configureAutomaticNavPvt() {
  gnssStatusCache.clear();
  gnssStatusDirty = true;
  gnssStatusDisplayInitialized = false;

  const uint8_t requestedEpochRate =
      navPvtEpochRateForStatusFrequency(properties.getRefreshFrequency(),
                                        NAVIGATION_EPOCH_MILLIS);

  bool initialSampleReceived = false;
  if (myGNSS.getPVT(NAV_PVT_COMMAND_MAX_WAIT_MILLIS) &&
      myGNSS.packetUBXNAVPVT != nullptr) {
    cacheNavPvtData(myGNSS.packetUBXNAVPVT->data);
    initialSampleReceived = true;
  }

  if (!myGNSS.setAutoPVTcallbackPtr(navPvtCallback,
                                    VAL_LAYER_RAM_BBR,
                                    NAV_PVT_COMMAND_MAX_WAIT_MILLIS)) {
    myGNSS.setAutoPVTrate(0, false, VAL_LAYER_RAM_BBR,
                          NAV_PVT_COMMAND_MAX_WAIT_MILLIS);
    return false;
  }

  // The callback setup enables NAV-PVT at one report per navigation epoch.
  // If the boot-only poll failed, use that temporary rate to obtain an
  // initial snapshot before applying a longer Status Frequency.
  if (!initialSampleReceived) {
    const uint32_t waitStartedMillis = millis();
    GnssStatusSnapshot snapshot = {};
    while (!gnssStatusCache.get(&snapshot) &&
           static_cast<uint32_t>(millis() - waitStartedMillis) <
               NAV_PVT_INITIAL_SAMPLE_WAIT_MILLIS) {
      myGNSS.checkUblox();
      myGNSS.checkCallbacks();
      delay(1);
    }
  }

  myGNSS.setAutoPVTrate(requestedEpochRate, false,
                        VAL_LAYER_RAM_BBR,
                        NAV_PVT_COMMAND_MAX_WAIT_MILLIS);

  // Read RAM back instead of trusting the library return value: if VALSET
  // fails, SparkFun's fallback reports whether VALGET worked, not whether the
  // returned rate equals the requested rate.
  uint8_t confirmedEpochRate = 0;
  const bool rateRead =
      myGNSS.getVal8(UBLOX_CFG_MSGOUT_UBX_NAV_PVT_I2C,
                     &confirmedEpochRate,
                     VAL_LAYER_RAM,
                     NAV_PVT_COMMAND_MAX_WAIT_MILLIS);
  const bool rateConfigured = rateRead &&
                              confirmedEpochRate == requestedEpochRate;
  if (rateRead && confirmedEpochRate > 0) {
    navPvtEpochRate = confirmedEpochRate > MAX_NAV_PVT_EPOCH_RATE
                          ? MAX_NAV_PVT_EPOCH_RATE
                          : confirmedEpochRate;
  }
  else {
    // If readback itself failed, use the requested period for conservative
    // freshness handling. A zero readback means no stream and is treated as
    // one epoch so the cached status becomes stale promptly.
    navPvtEpochRate = rateRead ? 1 : requestedEpochRate;
  }
  gnssStatusMaximumAgeMillis =
      gnssStatusFreshnessLimitMillis(navPvtEpochRate,
                                     NAVIGATION_EPOCH_MILLIS);
  return rateConfigured;
}

void serviceCachedGnssStatus() {
  GnssStatusSnapshot snapshot = {};
  const bool hasSnapshot = gnssStatusCache.get(&snapshot);
  const bool isFresh = gnssStatusCache.isFresh(millis(),
                                                gnssStatusMaximumAgeMillis);

  if (!gnssStatusDirty && gnssStatusDisplayInitialized &&
      isFresh == gnssStatusWasFresh)
    return;

  if (!hasSnapshot)
    gpsFixType = "Waiting for NAV-PVT";
  else if (!isFresh)
    gpsFixType = "Stale NAV-PVT";
  else
    gpsFixType = gnssFixTypeName(snapshot.fixOk, snapshot.fixType);

  gnssStatusDirty = false;
  gnssStatusDisplayInitialized = true;
  gnssStatusWasFresh = isFresh;
}

bool configureTimePulse() {
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
      timingConfigured = myGNSS.sendCfgValset();
  }

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
      controlsConfigured = myGNSS.sendCfgValset();
  }

  return timingConfigured && controlsConfigured;
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

bool acquirePpsTimebase() {
  ppsAcquisitionAttempted = true;
  lastPpsAcquisitionAttemptMillis = millis();
  invalidatePpsTimebase();

  // flushTIMTP only marks parsed data stale; it does not drain the receiver's
  // I2C output buffer. Stop the stream, drain all old bytes, then wait for a
  // newly generated automatic report which can be bracketed against TP1.
  myGNSS.setAutoTIMTP(false, true, VAL_LAYER_RAM_BBR, 250);
  timTpAutomaticEnabled = false;
  delay(TIMTP_DRAIN_DELAY_MILLIS);
  drainGnssBeforeTimTpBoundary();

  timTpAutomaticEnabled = myGNSS.setAutoTIMTP(true, true, VAL_LAYER_RAM_BBR, 250);
  myGNSS.flushTIMTP();
  lastTimTpReportMillis = millis();
  if (!timTpAutomaticEnabled) {
    invalidatePpsTimebase();
    return false;
  }

  uint32_t pulseCount = 0;
  uint32_t intervalMicros = 0;
  uint32_t invalidIntervalCount = 0;
  getTimePulseStatus(&pulseCount, &intervalMicros, nullptr, &invalidIntervalCount);
  observedInvalidIntervalCount = invalidIntervalCount;
  timTpFreshnessReferencePulseCount = pulseCount;
  updatePpsClockFromPulse();
  return true;
}

bool initializePpsTimebase() {
  invalidatePpsTimebase();

  uint32_t pulseCount = 0;
  uint32_t intervalMicros = 0;
  const uint32_t waitStartedMillis = millis();
  do {
    getTimePulseStatus(&pulseCount, &intervalMicros);
    if (pulseCount >= 2 && PpsClock::isExpectedPulseInterval(intervalMicros))
      break;
    delay(1);
  } while (static_cast<uint32_t>(millis() - waitStartedMillis) < 2200);

  return acquirePpsTimebase();
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

  if (!timTpAutomaticEnabled) {
    if (!ppsAcquisitionAttempted ||
        static_cast<uint32_t>(millis() - lastPpsAcquisitionAttemptMillis) >=
            PPS_ACQUISITION_RETRY_MILLIS)
      acquirePpsTimebase();
    else
      drainGnssBeforeTimTpBoundary();
    return;
  }

  if (!ppsClock.isAnchored() && timTpTargetPending &&
      static_cast<uint32_t>(millis() - timTpTargetQueuedMillis) > TIMTP_STALE_MILLIS)
    invalidatePpsTimebase();

  if (!timTpFreshStreamReady) {
    // Keep NAV-PVT status live while waiting for TP1. Any TIM-TP parsed here is
    // discarded before the pulse snapshot and cannot anchor the NTP clock.
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

    // Establish the freshness boundary only after a new, valid TP1 edge. Any
    // TIM-TP report describing that edge or an earlier edge was generated
    // before this point and will be removed by the following drain.
    if (pulseCount == timTpFreshnessReferencePulseCount || pulseCount < 2 ||
        !PpsClock::isExpectedPulseInterval(intervalMicros) ||
        static_cast<uint32_t>(micros() - edgeMicros) <
            TIMTP_POST_EDGE_DRAIN_GUARD_MICROS) {
      return;
    }

    // Ensure SparkFun's 20 ms I2C polling gate cannot suppress this drain.
    delay(TIMTP_DRAIN_DELAY_MILLIS);

    uint32_t pulseCountBeforeDrain = 0;
    uint32_t intervalMicrosBeforeDrain = 0;
    uint32_t invalidIntervalCountBeforeDrain = 0;
    getTimePulseStatus(&pulseCountBeforeDrain,
                       &intervalMicrosBeforeDrain,
                       nullptr,
                       &invalidIntervalCountBeforeDrain);
    if (pulseCountBeforeDrain != pulseCount) {
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
        TIMTP_STREAM_RESTART_MILLIS)
      acquirePpsTimebase();
    return;
  }

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
    reportRtcSyncErrorOnce("RTC synchronization write failed; retrying");
    return;
  }

  rtcSyncState = RtcSyncState::Idle;
  rtcSyncErrorReported = false;
  rtcSetTimerMs = 0;
  addLog(String("Set RTC from UTC TP1 pulse: ") + writtenTime.getISO8601Time(2));
}

String getRtcISO8601Time() {
  if (!rtcAvailable || rtc.updateTime() == false) { // Updates the time variables from RTC.
    if (!rtcReadErrorReported) {
      rtcReadErrorReported = true;
      recordError("RTC failed to update");
    }
    return String("");
  }
  rtcReadErrorReported = false;

  uint16_t year = 2000 + rtc.getYear();
  return t.toISO8601Time(year, rtc.getMonth(), rtc.getDate(), rtc.getHours(), rtc.getMinutes(), rtc.getSeconds(), rtc.getHundredths(), 2);
}

String getRtcWebISO8601Time() {
  const uint8_t oscillatorStatus = rtcAvailable ? rtc.readRegister(RV1805_OSC_STATUS) : 0xFF;
  const bool hundredthsAvailable = oscillatorStatus != 0xFF &&
                                   (oscillatorStatus & RTC_OSCILLATOR_MODE_RC_MASK) == 0;
  if (rtcHundredthsAvailable && !hundredthsAvailable)
    recordError("RTC XT oscillator unavailable; web fractional time disabled");
  rtcHundredthsAvailable = hundredthsAvailable;

  String rtcTime = getRtcISO8601Time();
  if (rtcTime.length() == 0 || hundredthsAvailable)
    return rtcTime;  // NOLINT(clang-diagnostic-nrvo)

  if (rtcTime.length() >= 3)
    rtcTime.remove(rtcTime.length() - 3);
  return rtcTime + " (fraction unavailable: RTC using RC oscillator)";
}

bool bindNtpUdpSocket() {
  lastNtpBindAttemptMillis = millis();
  if (ntpUdpBound)
    udp.stop();

  ntpUdpBound = udp.begin(ntpPort) != 0;
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

void discardCurrentUdpPacket() {
  uint8_t discardBuffer[32];
  while (udp.available() > 0) {
    const int availableBytes = udp.available();
    const std::size_t bytesToRead = availableBytes > static_cast<int>(sizeof(discardBuffer))
                                   ? sizeof(discardBuffer)
                                   : static_cast<std::size_t>(availableBytes);
    if (udp.read(discardBuffer, bytesToRead) <= 0)
      break;
  }
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
      recordError("NTP clock unsynchronized: waiting for a confirmed UTC TP1/TIM-TP timebase");
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
  if (responseStatus != NtpResponseStatus::Ready)
    return;

  if (udp.beginPacket(remoteIp, remotePort) == 0) {
    addError("Could not begin NTP response to " + properties.generateIpString(remoteIp));
    bindNtpUdpSocket();
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
        recordError("NTP clock became unsynchronized while constructing a response");
      }
    }
  }

  // Teensy's EthernetUDP applies its cumulative offset twice across multiple
  // writes. One 48-byte write is required to produce one valid 48-byte NTP
  // datagram and to keep T3 at bytes 40-47.
  if (udp.write(response, sizeof(response)) != sizeof(response)) {
    addError("Could not write NTP response to " + properties.generateIpString(remoteIp));
    bindNtpUdpSocket();
    return;
  }

  if (udp.endPacket() == 0) {
    addError("Could not send NTP response to " + properties.generateIpString(remoteIp));
    bindNtpUdpSocket();
    return;
  }

  // RTC I2C and dynamic String/list work happen only after the response left.
  addLog(String(timeAvailable ? "Synchronized" : "Unsynchronized") +
         " NTP response to " + properties.generateIpString(remoteIp) +
         ", port " + String(remotePort));
}

// GPS fix type
String getGpsFix() {
  uint8_t fixOk = 0;
  uint8_t gpsFixCode = 0;
  String gpsFixType = "No Fix";

  if (myGNSS.getNAVSTATUS() == true) {
    fixOk = myGNSS.packetUBXNAVSTATUS->data.flags.bits.gpsFixOk;
    gpsFixCode = myGNSS.packetUBXNAVSTATUS->data.gpsFix;

    if (fixOk == 1) { // has a fix
      switch (gpsFixCode) {
        case 0x00: gpsFixType = "No Fix"; break;
        case 0x01: gpsFixType = "1D Fix"; break;
        case 0x02: gpsFixType = "2D Fix"; break;
        case 0x03: gpsFixType = "3D Fix"; break;
        case 0x04: gpsFixType = "GPS Fix"; break;
        case 0x05: gpsFixType = "Time Fix"; break;
        default: gpsFixType = "Unknown"; break;
      }
    } 
  }
  return gpsFixType;
}

// Number of GPS signals
uint8_t getGpsSignals() {
  uint8_t numberOfSignals = 0;
  if (myGNSS.getNAVSIG() == true) {
    numberOfSignals = myGNSS.packetUBXNAVSIG->data.header.numSigs;
  }
  return numberOfSignals;
}

String getGpsISO8601Time() {
  NormalizedTimestamp timestamp = {};
  if (!getPpsTimestamp(micros(), &timestamp) ||
      timestamp.secondsSince1900 < 0)
    return "";

  TimeData gpsTime;
  if (!gpsTime.setSecondsSince1900(
          static_cast<uint64_t>(timestamp.secondsSince1900)))
    return "";

  gpsTime.setSubSec(static_cast<int32_t>(timestamp.nanoseconds));
  return gpsTime.getISO8601Time(6);
}

void addLog(String log) {
  if (properties.getLogMax() == 0)
    return;
  if (static_cast<uint16_t>(usageLog.size()) >= properties.getLogMax()) {
    usageLog.pop_back();
  }
  usageLog.push_front(getRtcISO8601Time() + " - " + log);
}

void addError(String error) {
  const String rtcTimestamp = getRtcISO8601Time();
  if (rtcTimestamp.length() > 0)
    error = rtcTimestamp + " - " + error;
  recordError(error);
}

void recordError(String error) {
  if (properties.getErrorMax() == 0) {
    Serial.println(error);
    return;
  }
  if (static_cast<uint16_t>(errorLog.size()) >= properties.getErrorMax()) {
    errorLog.pop_back();
  }
  errorLog.push_front(error);
  Serial.println(error);
}

// Display text on the OLED screen
void displaySettings() {
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
