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
 
#include <cstddef>  // For std::size_t
#include <cstring>  // For std::strlen and std::strncpy
#include <iterator> // For std::size
#include <EEPROM.h>
#include <SHA256.h>
#include "Properties.h"

// Initializes the server properties with their default values.
Properties::Properties() {
  // Set the default values
  savedData_ = 0;
  eepromWrites_ = 0;
  ethProps_.localIp    = {0,0,0,0};
  ethProps_.subnet     = {0,0,0,0};
  ethProps_.dns1Ip     = {0,0,0,0};
  ethProps_.dns2Ip     = {0,0,0,0};
  ethProps_.gatewayIp  = {0,0,0,0};
  memset(strLocalIp_,   0, sizeof(strLocalIp_));
  memset(strSubnet_,    0, sizeof(strSubnet_));
  memset(strDns1Ip_,    0, sizeof(strDns1Ip_));
  memset(strDns2Ip_,    0, sizeof(strDns2Ip_));
  memset(strGatewayIp_, 0, sizeof(strGatewayIp_));
  logMax_ = 500;
  errorMax_ = 500;
  refreshFrequencyMs_ = 10000;
  rtcSetFrequencyMs_ = 43200000; // 12 hours
  httpTimeoutMs_ = 60000;
  displayOn_ = 1;
  displayAlternate_ = 1;
  memset(serverName_, 0, sizeof(serverName_));
  PasscodeHash_ = OVERRIDE_CODE_HASH;
  isDhcp_ = false;  
}

// Destroys the properties container.
Properties::~Properties()
= default;

// Erases the configured EEPROM property region.
void Properties::clearEEPROM() {
  Serial.println("DEBUG: Clearing EEPROM");
  Serial.print("       EEPROM_DATASIZE = "); Serial.println(EEPROM_DATASIZE);
  Serial.print("       EEPROM_MAXSIZE  = "); Serial.println(EEPROM.length());
  for (unsigned int i = 0; i < EEPROM_DATASIZE; i++) {
    EEPROM.write(i, 0xFF);
  }
}
 
// Replaces all Ethernet properties with the supplied structure.
void Properties::setEthernetProperties(Ethernet_Properties_t ethernetProperties) {
  ethProps_ = ethernetProperties;
}

// Stores Ethernet properties from parsed IP addresses.
void Properties::setEthernetProperties(IPAddress localIp, IPAddress subnet, IPAddress dns1Ip, IPAddress dns2Ip, IPAddress gatewayIp) {
  ethProps_.localIp = { localIp[0], localIp[1], localIp[2], localIp[3] };
  ethProps_.subnet = { subnet[0], subnet[1], subnet[2], subnet[3] };
  ethProps_.dns1Ip = { dns1Ip[0], dns1Ip[1], dns1Ip[2], dns1Ip[3] };
  ethProps_.dns2Ip = { dns2Ip[0], dns2Ip[1], dns2Ip[2], dns2Ip[3] };
  ethProps_.gatewayIp = { gatewayIp[0], gatewayIp[1], gatewayIp[2], gatewayIp[3] };
}

// Validates and stores Ethernet properties from textual IP addresses.
bool Properties::setEthernetProperties(const char* strLocalIp, const char* strSubnet, const char* strDns1Ip, const char* strDns2Ip, const char* strGatewayIp) {
  IPAddress localIp; 
  IPAddress subnet;
  IPAddress dns1Ip; 
  IPAddress dns2Ip; 
  IPAddress gatewayIp;
  if (!localIp.fromString(strLocalIp)) return false;
  if (!subnet.fromString(strSubnet)) return false;
  if (!dns1Ip.fromString(strDns1Ip)) return false;
  if (!dns2Ip.fromString(strDns2Ip)) return false;
  if (!gatewayIp.fromString(strGatewayIp)) return false;

  setEthernetProperties(localIp, subnet, dns1Ip, dns2Ip, gatewayIp);
  return true;
}

// Validates and stores the local IP address.
bool Properties::setLocalIp(const char* strLocalIp) {
  IPAddress localIp;
  if (!localIp.fromString(strLocalIp)) return false; 
  ethProps_.localIp = { localIp[0], localIp[1], localIp[2], localIp[3] };
  std::strcpy(strLocalIp_, strLocalIp);  
  return true;
}

// Validates and stores the subnet mask.
bool Properties::setSubnet(const char* strSubnet) {
  IPAddress subnet;
  if (!subnet.fromString(strSubnet)) return false;
  ethProps_.subnet = { subnet[0], subnet[1], subnet[2], subnet[3] };
  std::strcpy(strSubnet_, strSubnet);
  return true;
}

// Validates and stores the primary DNS server address.
bool Properties::setDns1Ip(const char* strDns1Ip) {
  IPAddress dns1Ip;
  if (!dns1Ip.fromString(strDns1Ip)) return false;
  ethProps_.dns1Ip = { dns1Ip[0], dns1Ip[1], dns1Ip[2], dns1Ip[3] };
  std::strcpy(strDns1Ip_, strDns1Ip);
  return true;
}

// Validates and stores the secondary DNS server address.
bool Properties::setDns2Ip(const char* strDns2Ip) {
  IPAddress dns2Ip;
  if (!dns2Ip.fromString(strDns2Ip)) return false;
  ethProps_.dns2Ip = { dns2Ip[0], dns2Ip[1], dns2Ip[2], dns2Ip[3] };
  std::strcpy(strDns2Ip_, strDns2Ip);
  return true;
}

// Validates and stores the gateway address.
bool Properties::setGatewayIp(const char* strGatewayIp) {
  IPAddress gatewayIp;
  if (!gatewayIp.fromString(strGatewayIp)) return false;
  ethProps_.gatewayIp = { gatewayIp[0], gatewayIp[1], gatewayIp[2], gatewayIp[3] };
  std::strcpy(strGatewayIp_, strGatewayIp);
  return true;
}

// Sets the maximum number of retained log entries.
void Properties::setLogMax(uint16_t logMax) {
  logMax_ = logMax;
}

// Sets the maximum number of retained error entries.
void Properties::setErrorMax(uint16_t errorMax) {
  errorMax_ = errorMax;
}
  
// Sets the status refresh interval in milliseconds.
void Properties::setRefreshFrequency(uint16_t refreshFrequencyMs) {
  refreshFrequencyMs_ = refreshFrequencyMs;
}

// Sets the RTC synchronization interval in milliseconds.
void Properties::setRtcSetFrequency(uint32_t rtcSetFrequencyMs) {
  rtcSetFrequencyMs_ = rtcSetFrequencyMs;
}

// Sets the HTTP client timeout in milliseconds.
void Properties::setHttpTimeout(uint32_t httpTimeoutMs) {
  httpTimeoutMs_ = httpTimeoutMs;
}

// Enables or disables the display.
void Properties::setDisplayOn(uint8_t isOn) {
  displayOn_ = isOn == 1 ? 1 : 0;
}

// Enables or disables display alternation.
void Properties::setDisplayAlternate(uint8_t isAlternating) {
  displayAlternate_ = isAlternating == 1 ? 1 : 0;
}

// Stores a bounded, null-terminated server name.
void Properties::setServerName(const char* serverName) {
  // Get length of strings
  std::size_t lenSet = std::strlen(serverName);
  std::size_t lenPrivate = std::size(serverName_);

  // If longer than 37, only copy 36
  if (lenSet > lenPrivate) lenSet = lenPrivate - 1;

  std::strncpy(serverName_, serverName, lenSet);
  serverName_[lenSet] = '\0';  // ensure null termination
}

// Hashes and stores a new setup passcode.
void Properties::setPasscode(const char* Passcode) {
  SHA256 hasher;
  hasher.reset();
  hasher.update((const uint8_t*)Passcode, std::strlen(Passcode));
  hasher.finalize(PasscodeHash_.data(), 32);
}

// Returns the number of property writes made to EEPROM.
uint32_t Properties::getEepromWrites() {
  return eepromWrites_;
}

// Returns the complete Ethernet property structure.
Ethernet_Properties_t Properties::getEthernetProperties() {
  return ethProps_;
}

// Returns the configured local IP address.
IPAddress Properties::getLocalIp() {
  return IPAddress(ethProps_.localIp[0], ethProps_.localIp[1], ethProps_.localIp[2], ethProps_.localIp[3]);
}

// Returns the configured subnet mask.
IPAddress Properties::getSubnet() {
  return IPAddress(ethProps_.subnet[0], ethProps_.subnet[1], ethProps_.subnet[2], ethProps_.subnet[3]);
}

// Returns the configured primary DNS server address.
IPAddress Properties::getDns1Ip() {
  return IPAddress(ethProps_.dns1Ip[0], ethProps_.dns1Ip[1], ethProps_.dns1Ip[2], ethProps_.dns1Ip[3]);
}

// Returns the configured secondary DNS server address.
IPAddress Properties::getDns2Ip() {
  return IPAddress(ethProps_.dns2Ip[0], ethProps_.dns2Ip[1], ethProps_.dns2Ip[2], ethProps_.dns2Ip[3]);
}

// Returns the configured gateway address.
IPAddress Properties::getGatewayIp() {
  return IPAddress(ethProps_.gatewayIp[0], ethProps_.gatewayIp[1], ethProps_.gatewayIp[2], ethProps_.gatewayIp[3]);
}

// Returns the local IP address as text.
String Properties::getLocalIpStr() {
  return String(strLocalIp_);
}

// Returns the subnet mask as text.
String Properties::getSubnetStr() {
  return String(strSubnet_);
}

// Returns the primary DNS server address as text.
String Properties::getDns1IpStr() {
  return String(strDns1Ip_);
}

// Returns the secondary DNS server address as text.
String Properties::getDns2IpStr() {
  return String(strDns2Ip_);
}

// Returns the gateway address as text.
String Properties::getGatewayIpStr() {
  return String(strGatewayIp_);
}

// Returns the maximum number of retained log entries.
uint16_t Properties::getLogMax() {
  return logMax_;
}

// Returns the maximum number of retained error entries.
uint16_t Properties::getErrorMax() {
  return errorMax_;
}

// Returns the status refresh interval in milliseconds.
uint16_t Properties::getRefreshFrequency() {
  return refreshFrequencyMs_;
}

// Returns the RTC synchronization interval in milliseconds.
uint32_t Properties::getRtcSetFrequency() {
  return rtcSetFrequencyMs_;
}

// Returns the HTTP client timeout in milliseconds.
uint32_t Properties::getHttpTimeout() {
  return httpTimeoutMs_;
}

// Returns whether the display is enabled as a numeric flag.
uint8_t Properties::getDisplayOn() {
  return displayOn_; // 1 is true, 0 is false
}

// Returns whether display alternation is enabled as a numeric flag.
uint8_t Properties::getDisplayAlternate() {
  return displayAlternate_; // 1 is true, 0 is false
}

// Returns the configured server name.
String Properties::getServerName() {
  return String(serverName_);
}

// Verifies a candidate against the configured or override passcode hash.
bool Properties::isPasscode(const char* Passcode) {
  bool isOverride = false;
  bool isPasscode = false;
  std::array<std::uint8_t, 32> digest;

  //if (std::strlen(Passcode) == 0) return false;

  SHA256 hasher;
  hasher.reset();
  hasher.update((const uint8_t*)Passcode, std::strlen(Passcode));
  hasher.finalize(digest.data(), 32);
  // Test if this is the override Passcode
  isOverride = OVERRIDE_CODE_HASH == digest;
  isPasscode = PasscodeHash_ == digest;

  return isOverride || isPasscode;
}

// Reports whether DHCP supplies the Ethernet configuration.
bool Properties::isDhcp() {
  return isDhcp_;
}

// Writes all persistent server properties to EEPROM.
bool Properties::saveProperties() { 
  if (savedData_ != 0x01) {
    // This is the first time this application has run, and we need set the "saved data" indicator bit in the EEPROM and same the default properties.
    savedData_ = 0x01;  
    EEPROM.put(EERPOM_SAVED_ADDR, savedData_);
  }

  eepromWrites_++;
  EEPROM.put(EERPOM_WRITES_ADDR, eepromWrites_);
  EEPROM.put(EEPROM_ETHERNET_ADDR, (const Ethernet_Properties_t)ethProps_);
  EEPROM.put(EEPROM_LOGMAX_ADDR, logMax_);
  EEPROM.put(EEPROM_ERRORMAX_ADDR, errorMax_);
  EEPROM.put(EEPROM_REFRESHFREQ_ADDR, refreshFrequencyMs_);
  EEPROM.put(EEPROM_RTCFREQ_ADDR, rtcSetFrequencyMs_);
  EEPROM.put(EEPROM_HTTPTIMEOUT_ADDR, httpTimeoutMs_);
  EEPROM.put(EEPROM_DISPLAYON_ADDR, displayOn_);
  EEPROM.put(EEPROM_ALTERNATE_ADDR, displayAlternate_);
  EEPROM.put(EEPROM_SERVERNAME_ADDR, serverName_);
  EEPROM.put(EEPROM_PASSCODE_ADDR, PasscodeHash_);

  return true;
}

// Loads all persistent server properties from EEPROM.
bool Properties::loadProperties() {
  EEPROM.get(EERPOM_SAVED_ADDR, savedData_);
  if (savedData_ != 0x01) {
    // This is the first time the this application has run, and we need to call saveProperties().  
    saveProperties();
  }

  EEPROM.get(EERPOM_WRITES_ADDR, eepromWrites_);
  EEPROM.get(EEPROM_ETHERNET_ADDR, ethProps_);
  // If the IP value is 0 then use DHCP.
  if (ethProps_.localIp[0] == 0x00) isDhcp_ = true;
  generateAllIpStrings();
  EEPROM.get(EEPROM_LOGMAX_ADDR, logMax_);
  EEPROM.get(EEPROM_ERRORMAX_ADDR, errorMax_);
  EEPROM.get(EEPROM_REFRESHFREQ_ADDR, refreshFrequencyMs_);
  EEPROM.get(EEPROM_RTCFREQ_ADDR, rtcSetFrequencyMs_);
  EEPROM.get(EEPROM_HTTPTIMEOUT_ADDR, httpTimeoutMs_);
  EEPROM.get(EEPROM_SERVERNAME_ADDR, serverName_);
  EEPROM.get(EEPROM_DISPLAYON_ADDR, displayOn_);
  EEPROM.get(EEPROM_ALTERNATE_ADDR, displayAlternate_);
  EEPROM.get(EEPROM_PASSCODE_ADDR, PasscodeHash_);

  return true;
}

// Formats an IP address as dotted-decimal text.
String Properties::generateIpString(IPAddress ipAddress) {
  char strIpAddress[16] = "";
  sprintf(strIpAddress, "%u.%u.%u.%u", ipAddress[0], ipAddress[1], ipAddress[2], ipAddress[3]);
  return String(strIpAddress);
}

// Regenerates every cached textual IP address from its numeric value.
void Properties::generateAllIpStrings() {
  sprintf(strLocalIp_, "%u.%u.%u.%u", ethProps_.localIp[0], ethProps_.localIp[1], ethProps_.localIp[2], ethProps_.localIp[3]);
  sprintf(strSubnet_, "%u.%u.%u.%u", ethProps_.subnet[0], ethProps_.subnet[1], ethProps_.subnet[2], ethProps_.subnet[3]);
  sprintf(strDns1Ip_, "%u.%u.%u.%u", ethProps_.dns1Ip[0], ethProps_.dns1Ip[1], ethProps_.dns1Ip[2], ethProps_.dns1Ip[3]);
  sprintf(strDns2Ip_, "%u.%u.%u.%u", ethProps_.dns2Ip[0], ethProps_.dns2Ip[1], ethProps_.dns2Ip[2], ethProps_.dns2Ip[3]);
  sprintf(strGatewayIp_, "%u.%u.%u.%u", ethProps_.gatewayIp[0], ethProps_.gatewayIp[1], ethProps_.gatewayIp[2], ethProps_.gatewayIp[3]);
}
