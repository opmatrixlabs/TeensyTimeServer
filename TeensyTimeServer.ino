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


/*
 * NOTE:  The RTC is just used to timestamp log entries.  The NTP time come directly for the GNSS receiver.
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

// Forward declarations keep the sketch valid for standard C++ IntelliSense.
void getDeviceConfig();
void setDeviceConfig();
void setRtc();
String getRtcISO8601Time();
void discardCurrentUdpPacket();
void processNtpRequest(int packetSize);
NormalizedTimestamp getGpsTime(NormalizedTimestamp* gpsTime);
String getGpsFix();
uint8_t getGpsSignals();
String getGpsISO8601Time();
void addLog(String log);
void addError(String error);
void displaySettings();

const char* APP_NAME = "GPS NTP Time Server";
const char* VERSION = "1.0";
const char* AUTHOR = "Andrew Kevin Bailey";

/**** Setup Properties init *****/
Properties properties;

/***** Ethernet init *****/
byte mac[] = { 0xBC, 0xED, 0x5D, 0x3E, 0x94, 0xB6 };
EthernetUDP udp;
EthernetServer httpServer(80);
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
// The RV1805 has a bug where the hundreds sometimes does not get updated.  To fix
// this we will test if two consecutive readings are the same.  If so, reset the RV1805.
uint8_t previousHundredths_1 = 0;
uint8_t previousHundredths_2 = 0;
uint32_t hundredthError = 0;

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
//#define gnssAddress 0x42 // The default I2C address for u-blox modules is 0x42. Change this if required

/***** HTTP server init *****/
TimeHttp timeHttp;
String gpsISO8601Time;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
  Wire.setClock(400000);
  Wire.begin();
  delay(1000);

  /***** Begin real time clock (RTC) setup *****/ 
  if (rtc.begin() == false) {
    Serial.println("ERROR: Real time clock (RTC) was not found");
  }
  rtc.clearInterrupts();
  rtc.setStaticPowerSwitchOutput(false);
  rtc.setPowerSwitchLock(true);
  rtc.setAlarmMode(0);
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
  udp.begin(ntpPort);
  /***** End Ethernet and UDP setup *****/ 

  /***** Begin GPS setup *****/ 
  while (myGNSS.begin() == false) { // connect to the u-blox module using our custom port and address
    addError("u-blox GNSS not detected. Retrying...");
    delay (1000);
  }
  setDeviceConfig();
  addLog("GPS receiver config was set");
  // Allocate memory for configLog
  getDeviceConfig();
  if (configLog.length() > 24) { // the string length should be greater than 24 chars
    Serial.println(F("**************************************************"));
    Serial.println(configLog);
  }
  /***** End GPS setup *****/

  /***** Begin Display setup *****/
  if (myOLED.begin() == false) {
    Serial.println("ERROR: Device begin failed. Freezing...");
    while (true);
  }
  myOLED.erase();
  myOLED.display();
  gpsFixType = getGpsFix();
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
  timeHttp.setUpdateRtcFunction(&setRtc);
  timeHttp.setAddLogFunction(&addLog);
  timeHttp.setAddErrorFunction(&addError);
  /***** End HTTP setup *****/ 

  // Reset timers
  refreshTimerMs = 0;
  rtcSetTimerMs = 0;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void loop() {
  /***** Begin NTP server *****/
  int packetSize = udp.parsePacket();
  if (packetSize)
    processNtpRequest(packetSize);
  /***** End NTP server *****/

  /***** Begin HTTP server *****/
  EthernetClient httpClient = httpServer.available();
  if (httpClient) {
    timeHttp.processRequest(&httpClient, strLocalIp, gpsFixType, getGpsISO8601Time(), getRtcISO8601Time());
  }
  /***** End HTTP server *****/

  // To avoid having delays in loop, we'll use the strategy from BlinkWithoutDelay
  // see: File -> Examples -> 02.Digital -> BlinkWithoutDelay for more info
  if (refreshTimerMs > properties.getRefreshFrequency()) {
    digitalWrite(LED_BUILTIN, digitalRead(LED_BUILTIN) == HIGH ? LOW : HIGH);  // toggle LED
    
    // Refresh the display
    gpsFixType = getGpsFix();
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
  if (rtcSetTimerMs > properties.getRtcSetFrequency()) {
    setRtc();
    rtcSetTimerMs = 0;
  }
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Returns the final size of configLog.  configLog needs to be a minimum of 1024 chars.
void getDeviceConfig() { // configLog is a global variable
  configLog = String(APP_NAME) + " v" + String(VERSION);
  configLog += "\ncopyright (c) 2025 " + String(AUTHOR);
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
  configLog += "\n CFG-RATE-MEAS = " + String(myGNSS.getVal8(UBLOX_CFG_RATE_MEAS));
  configLog += "\n CFG-MSGOUT-UBX_NAV_PVT_I2C = " + String(myGNSS.getVal8(UBLOX_CFG_MSGOUT_UBX_NAV_PVT_I2C));
  configLog += "\n CFG-MSGOUT-UBX_NAV_TIMEUTC_I2C = " + String(myGNSS.getVal8(UBLOX_CFG_MSGOUT_UBX_NAV_TIMEUTC_I2C));
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
      addError("Could not get leap seconds from GPS.  Defaulting to 27 seconds (2025-01-01).");
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

  // Set the default measurement rate at 10 Hz
  myGNSS.setI2CpollingWait(5);  // I2C polling wait needs to be 4 times less than the GNSS measurement rate
  myGNSS.setVal16(UBLOX_CFG_RATE_MEAS, 30); // time in milliseconds between GNSS measurements
  myGNSS.setVal8(UBLOX_CFG_RATE_NAV, 0); // ratio of measurements to nav solutions
  myGNSS.setVal8(UBLOX_CFG_RATE_TIMEREF, 0); // 0 = UTC
  myGNSS.setVal8(UBLOX_CFG_MSGOUT_UBX_NAV_PVT_I2C, 0); // output rate of the UBX-NAV-PVT message on port I2C
}

void setRtc() { // myGNSS and rtc are global variables
  getGpsISO8601Time(); // will populate the t global variable
  if (t.isValidGpsTime()) {
    rtc.set24Hour();
    // Get the hundreds
    int32_t hundredths = t.getSubSec() / 10000000; // 10,000,000 ns per hundredth
    // Manually clamp to [0, 99]
    if (hundredths < 0)
      hundredths = 0;
    else if (hundredths > 99)
      hundredths = 99;

    rtc.setTime(static_cast<uint8_t>(hundredths), t.getSec(), t.getMin(), t.getHour(), t.getDay(), t.getMonth(), static_cast<uint16_t>(t.getYear()), 0);

    addLog(String("Set RTC time to GPS time: ") + t.getISO8601Time(6));
  }
  else addError("Invalid GPS time.  Did not update real time clock.");
}

String getRtcISO8601Time() {
  if (rtc.updateTime() == false) { //Updates the time variables from RTC
    Serial.println("RTC failed to update");
    addError("RTC failed to update");
    return String("");
  }

  // This code is to get around the RV-1805 RTC bug of the hundredths sometimes not getting updated.
  if (previousHundredths_1 == rtc.getHundredths()) {
    if (previousHundredths_2 == rtc.getHundredths()) {
      Serial.print("WARNING: RTC hundredths frozen count = "); Serial.println(++hundredthError);
      rtc.reset();
      rtc.begin();
      setRtc();
      rtc.updateTime();
    }
    else previousHundredths_2 = rtc.getHundredths();
  }
  else previousHundredths_1 = rtc.getHundredths();

  uint16_t year = 2000 + rtc.getYear();
  return t.toISO8601Time(year, rtc.getMonth(), rtc.getDate(), rtc.getHours(), rtc.getMinutes(), rtc.getSeconds(), rtc.getHundredths(), 2);
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

void processNtpRequest(const int packetSize) {
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

  if (udp.beginPacket(remoteIp, remotePort) == 0) {
    addError("Could not begin NTP response to " + properties.generateIpString(remoteIp));
    return;
  }

  addLog("Valid NTP request from " + properties.generateIpString(remoteIp) + ", port " + String(remotePort));

  NormalizedTimestamp transmitTime = {};
  const NormalizedTimestamp receiveTime = getGpsTime(&transmitTime);
  uint8_t response[NTP_PACKET_SIZE] = {};
  const NtpResponseStatus responseStatus = createNtpResponse(request,
                                                             sizeof(request),
                                                             receiveTime,
                                                             transmitTime,
                                                             t.isValidGpsTime(),
                                                             response,
                                                             sizeof(response));
  if (responseStatus != NtpResponseStatus::Ready)
    return;

  if (udp.write(response, sizeof(response)) != sizeof(response)) {
    addError("Could not write complete NTP response to " + properties.generateIpString(remoteIp));
    return;
  }

  if (udp.endPacket() == 0)
    addError("Could not send NTP response to " + properties.generateIpString(remoteIp));
}

NormalizedTimestamp getGpsTime(NormalizedTimestamp* gpsTime) { // returns the calculated received time
  elapsedMillis timer;
  if (myGNSS.getNAVTIMEUTC() == true) { 
    t.setYear(myGNSS.packetUBXNAVTIMEUTC->data.year);
    t.setMonth(myGNSS.packetUBXNAVTIMEUTC->data.month);
    t.setDay(myGNSS.packetUBXNAVTIMEUTC->data.day);
    t.setHour(myGNSS.packetUBXNAVTIMEUTC->data.hour);
    t.setMin(myGNSS.packetUBXNAVTIMEUTC->data.min);
    t.setSec(myGNSS.packetUBXNAVTIMEUTC->data.sec);

    // Get the corrected time using the GPS nano correction
    t.calculateCorrectedTime(myGNSS.packetUBXNAVTIMEUTC->data.nano);
    // Convert to seconds since epoch, UTC
    const uint64_t secs = t.secondsSince1900();

    // Keep the UTC value in arithmetic-friendly seconds and nanoseconds.
    // Conversion to the NTP binary fraction happens only when the packet is serialized.
    *gpsTime = normalizeTimestamp(static_cast<int64_t>(secs), t.getSubSec());

    // Does the GPS have a time error
    if (t.getYear() < 2025) {
      t.validGpsTime(false);
      addError("Invalid GPS time error");
      Serial.print("ERROR: Invalid GPS time - estimated accuracy: ");
      Serial.println(myGNSS.packetUBXNAVTIMEUTC->data.tAcc);
    }
    else t.validGpsTime(true);;
  }
  else {
    t.validGpsTime(false);;
    addError("Could not get NAVTIMEUTC");
  }

  // Calculate received time by subtracting the elapsed whole milliseconds.
  const uint32_t elapsedMilliseconds = timer;
  const int64_t receiveNanoseconds = static_cast<int64_t>(gpsTime->nanoseconds) -
                                     static_cast<int64_t>(elapsedMilliseconds) * 1000000LL;
  return normalizeTimestamp(gpsTime->secondsSince1900, receiveNanoseconds);
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

// ISO8601Time is a global variable
String getGpsISO8601Time() {
  if (myGNSS.getNAVTIMEUTC() == true) { 
    t.setYear(myGNSS.packetUBXNAVTIMEUTC->data.year);
    t.setMonth(myGNSS.packetUBXNAVTIMEUTC->data.month);
    t.setDay(myGNSS.packetUBXNAVTIMEUTC->data.day);
    t.setHour(myGNSS.packetUBXNAVTIMEUTC->data.hour);
    t.setMin(myGNSS.packetUBXNAVTIMEUTC->data.min);
    t.setSec(myGNSS.packetUBXNAVTIMEUTC->data.sec);

    // Get the corrected time using the GPS nano correction
    t.calculateCorrectedTime(myGNSS.packetUBXNAVTIMEUTC->data.nano);
    
    // Does the GPS have a time error
    if (t.getYear() < 2025) {
      t.validGpsTime(false);;
      addError("Invalid GPS time error");
      Serial.print("ERROR: Invalid GPS time, estimated accuracy: ");
      Serial.println(myGNSS.packetUBXNAVTIMEUTC->data.tAcc);
      gpsISO8601Time = "";
    }
    else {
      t.validGpsTime(true);
      gpsISO8601Time = t.getISO8601Time(6);
    }
  }
  else {
    t.validGpsTime(false);
    gpsISO8601Time = "";
    addError("Could not get NAVTIMEUTC");
  }

  return gpsISO8601Time;
}

void addLog(String log) {
  if (static_cast<uint16_t>(usageLog.size()) >= properties.getLogMax()) {
    usageLog.pop_back();
  }
  usageLog.push_front(getRtcISO8601Time() + " - " + log);
}

void addError(String error) {
  error = getRtcISO8601Time() + " - " + error;
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

  // Subnet (lable)
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
