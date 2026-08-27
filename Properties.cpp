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

Properties::~Properties()
= default;

void Properties::clearEEPROM() {
  Serial.println("DEBUG: Clearing EEPROM");
  Serial.print("       EEPROM_DATASIZE = "); Serial.println(EEPROM_DATASIZE);
  Serial.print("       EEPROM_MAXSIZE  = "); Serial.println(EEPROM.length());
  for (unsigned int i = 0; i < EEPROM_DATASIZE; i++) {
    EEPROM.write(i, 0xFF);
  }
}
 
void Properties::setEthernetProperties(Ethernet_Properties_t ethernetProperties) {
  ethProps_ = ethernetProperties;
}

void Properties::setEthernetProperties(IPAddress localIp, IPAddress subnet, IPAddress dns1Ip, IPAddress dns2Ip, IPAddress gatewayIp) {
  ethProps_.localIp = { localIp[0], localIp[1], localIp[2], localIp[3] };
  ethProps_.subnet = { subnet[0], subnet[1], subnet[2], subnet[3] };
  ethProps_.dns1Ip = { dns1Ip[0], dns1Ip[1], dns1Ip[2], dns1Ip[3] };
  ethProps_.dns2Ip = { dns2Ip[0], dns2Ip[1], dns2Ip[2], dns2Ip[3] };
  ethProps_.gatewayIp = { gatewayIp[0], gatewayIp[1], gatewayIp[2], gatewayIp[3] };
}

// Returns false if any of the strings are not valid IP addresses.
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

bool Properties::setLocalIp(const char* strLocalIp) {
  IPAddress localIp;
  if (!localIp.fromString(strLocalIp)) return false; 
  ethProps_.localIp = { localIp[0], localIp[1], localIp[2], localIp[3] };
  std::strcpy(strLocalIp_, strLocalIp);  
  return true;
}

bool Properties::setSubnet(const char* strSubnet) {
  IPAddress subnet;
  if (!subnet.fromString(strSubnet)) return false;
  ethProps_.subnet = { subnet[0], subnet[1], subnet[2], subnet[3] };
  std::strcpy(strSubnet_, strSubnet);
  return true;
}

bool Properties::setDns1Ip(const char* strDns1Ip) {
  IPAddress dns1Ip;
  if (!dns1Ip.fromString(strDns1Ip)) return false;
  ethProps_.dns1Ip = { dns1Ip[0], dns1Ip[1], dns1Ip[2], dns1Ip[3] };
  std::strcpy(strDns1Ip_, strDns1Ip);
  return true;
}

bool Properties::setDns2Ip(const char* strDns2Ip) {
  IPAddress dns2Ip;
  if (!dns2Ip.fromString(strDns2Ip)) return false;
  ethProps_.dns2Ip = { dns2Ip[0], dns2Ip[1], dns2Ip[2], dns2Ip[3] };
  std::strcpy(strDns2Ip_, strDns2Ip);
  return true;
}

bool Properties::setGatewayIp(const char* strGatewayIp) {
  IPAddress gatewayIp;
  if (!gatewayIp.fromString(strGatewayIp)) return false;
  ethProps_.gatewayIp = { gatewayIp[0], gatewayIp[1], gatewayIp[2], gatewayIp[3] };
  std::strcpy(strGatewayIp_, strGatewayIp);
  return true;
}

void Properties::setLogMax(uint16_t logMax) {
  logMax_ = logMax;
}

void Properties::setErrorMax(uint16_t errorMax) {
  errorMax_ = errorMax;
}
  
void Properties::setRefreshFrequency(uint16_t refreshFrequencyMs) {
  refreshFrequencyMs_ = refreshFrequencyMs;
}

void Properties::setRtcSetFrequency(uint32_t rtcSetFrequencyMs) {
  rtcSetFrequencyMs_ = rtcSetFrequencyMs;
}

void Properties::setHttpTimeout(uint32_t httpTimeoutMs) {
  httpTimeoutMs_ = httpTimeoutMs;
}

void Properties::setDisplayOn(uint8_t isOn) {
  displayOn_ = isOn == 1 ? 1 : 0;
}

void Properties::setDisplayAlternate(uint8_t isAlternating) {
  displayAlternate_ = isAlternating == 1 ? 1 : 0;
}

void Properties::setServerName(const char* serverName) {
  // Get length of strings
  std::size_t lenSet = std::strlen(serverName);
  std::size_t lenPrivate = std::size(serverName_);

  // If longer than 37, only copy 36
  if (lenSet > lenPrivate) lenSet = lenPrivate - 1;

  std::strncpy(serverName_, serverName, lenSet);
  serverName_[lenSet] = '\0';  // ensure null termination
}

void Properties::setPasscode(const char* Passcode) {
  SHA256 hasher;
  hasher.reset();
  hasher.update((const uint8_t*)Passcode, std::strlen(Passcode));
  hasher.finalize(PasscodeHash_.data(), 32);
}

uint32_t Properties::getEepromWrites() {
  return eepromWrites_;
}

Ethernet_Properties_t Properties::getEthernetProperties() {
  return ethProps_;
}

IPAddress Properties::getLocalIp() {
  return IPAddress(ethProps_.localIp[0], ethProps_.localIp[1], ethProps_.localIp[2], ethProps_.localIp[3]);
}

IPAddress Properties::getSubnet() {
  return IPAddress(ethProps_.subnet[0], ethProps_.subnet[1], ethProps_.subnet[2], ethProps_.subnet[3]);
}

IPAddress Properties::getDns1Ip() {
  return IPAddress(ethProps_.dns1Ip[0], ethProps_.dns1Ip[1], ethProps_.dns1Ip[2], ethProps_.dns1Ip[3]);
}

IPAddress Properties::getDns2Ip() {
  return IPAddress(ethProps_.dns2Ip[0], ethProps_.dns2Ip[1], ethProps_.dns2Ip[2], ethProps_.dns2Ip[3]);
}

IPAddress Properties::getGatewayIp() {
  return IPAddress(ethProps_.gatewayIp[0], ethProps_.gatewayIp[1], ethProps_.gatewayIp[2], ethProps_.gatewayIp[3]);
}

String Properties::getLocalIpStr() {
  return String(strLocalIp_);
}

String Properties::getSubnetStr() {
  return String(strSubnet_);
}

String Properties::getDns1IpStr() {
  return String(strDns1Ip_);
}

String Properties::getDns2IpStr() {
  return String(strDns2Ip_);
}

String Properties::getGatewayIpStr() {
  return String(strGatewayIp_);
}

uint16_t Properties::getLogMax() {
  return logMax_;
}

uint16_t Properties::getErrorMax() {
  return errorMax_;
}

uint16_t Properties::getRefreshFrequency() {
  return refreshFrequencyMs_;
}

uint32_t Properties::getRtcSetFrequency() {
  return rtcSetFrequencyMs_;
}

uint32_t Properties::getHttpTimeout() {
  return httpTimeoutMs_;
}

uint8_t Properties::getDisplayOn() {
  return displayOn_; // 1 is true, 0 is false
}

uint8_t Properties::getDisplayAlternate() {
  return displayAlternate_; // 1 is true, 0 is false
}

String Properties::getServerName() {
  return String(serverName_);
}

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

bool Properties::isDhcp() {
  return isDhcp_;
}

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

String Properties::generateIpString(IPAddress ipAddress) {
  char strIpAddress[16] = "";
  sprintf(strIpAddress, "%u.%u.%u.%u", ipAddress[0], ipAddress[1], ipAddress[2], ipAddress[3]);
  return String(strIpAddress);
}

void Properties::generateAllIpStrings() {
  sprintf(strLocalIp_, "%u.%u.%u.%u", ethProps_.localIp[0], ethProps_.localIp[1], ethProps_.localIp[2], ethProps_.localIp[3]);
  sprintf(strSubnet_, "%u.%u.%u.%u", ethProps_.subnet[0], ethProps_.subnet[1], ethProps_.subnet[2], ethProps_.subnet[3]);
  sprintf(strDns1Ip_, "%u.%u.%u.%u", ethProps_.dns1Ip[0], ethProps_.dns1Ip[1], ethProps_.dns1Ip[2], ethProps_.dns1Ip[3]);
  sprintf(strDns2Ip_, "%u.%u.%u.%u", ethProps_.dns2Ip[0], ethProps_.dns2Ip[1], ethProps_.dns2Ip[2], ethProps_.dns2Ip[3]);
  sprintf(strGatewayIp_, "%u.%u.%u.%u", ethProps_.gatewayIp[0], ethProps_.gatewayIp[1], ethProps_.gatewayIp[2], ethProps_.gatewayIp[3]);
}
