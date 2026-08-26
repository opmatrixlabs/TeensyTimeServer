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

#pragma once

#include <array>
#include <cstdint>
#include <Arduino.h>
#include <IPAddress.h>

/*
FYI:
Typical emulated EEPROM size is no more than 8 KB (8192 bytes).
Larger sizes may result in inefficiencies.
*/

// Struct for the Ethernet settings
typedef struct {
  // Must use std::array<std::uint8_t, 4> in place of the Arduino IPAddress class
  // to be able to store the struct in EEPROM.
  std::array<std::uint8_t, 4> localIp;
  std::array<std::uint8_t, 4> subnet;
  std::array<std::uint8_t, 4> dns1Ip;
  std::array<std::uint8_t, 4> dns2Ip;
  std::array<std::uint8_t, 4> gatewayIp;
} Ethernet_Properties_t;

// Location of data in the EEPROM
#define EERPOM_SAVED_ADDR        0
#define EERPOM_WRITES_ADDR       (EERPOM_SAVED_ADDR       + sizeof(uint8_t))
#define EEPROM_ETHERNET_ADDR     (EERPOM_WRITES_ADDR      + sizeof(uint32_t))
#define EEPROM_LOGMAX_ADDR       (EEPROM_ETHERNET_ADDR    + sizeof(Ethernet_Properties_t))
#define EEPROM_ERRORMAX_ADDR     (EEPROM_LOGMAX_ADDR      + sizeof(uint16_t))
#define EEPROM_REFRESHFREQ_ADDR  (EEPROM_ERRORMAX_ADDR    + sizeof(uint16_t))
#define EEPROM_RTCFREQ_ADDR      (EEPROM_REFRESHFREQ_ADDR + sizeof(uint16_t))
#define EEPROM_HTTPTIMEOUT_ADDR  (EEPROM_RTCFREQ_ADDR     + sizeof(uint32_t))
#define EEPROM_DISPLAYON_ADDR    (EEPROM_HTTPTIMEOUT_ADDR + sizeof(uint32_t))
#define EEPROM_ALTERNATE_ADDR    (EEPROM_DISPLAYON_ADDR   + sizeof(uint8_t))
#define EEPROM_SERVERNAME_ADDR   (EEPROM_ALTERNATE_ADDR   + sizeof(uint8_t))
#define EEPROM_PASSCODE_ADDR     (EEPROM_SERVERNAME_ADDR  + sizeof(char) * 255) // 32 bit SHA256 hash
#define EEPROM_DATASIZE          (EEPROM_PASSCODE_ADDR    + sizeof(std::array<std::uint8_t, 32>) + 200) // add 100 to make sure to erase all the data from previous builds

class Properties {
public:
  Properties();
  ~Properties();
  
  void clearEEPROM();
  void setEthernetProperties(Ethernet_Properties_t ethernetProperties);
  void setEthernetProperties(IPAddress localIp, IPAddress subnet, IPAddress dns1Ip, IPAddress dns2Ip, IPAddress gatewayIp);
  bool setEthernetProperties(const char* strLocalIp, const char* strSubnet, const char* strDns1Ip, const char* strDns2Ip, const char* strGatewayIp);
  bool setLocalIp(const char* strLocalIp);
  bool setSubnet(const char* strSubnet);
  bool setDns1Ip(const char* strDns1Ip);
  bool setDns2Ip(const char* strDns2Ip);
  bool setGatewayIp(const char* strGatewayIp);
  void setLogMax(uint16_t logMax);
  void setErrorMax(uint16_t errorMax);
  void setRefreshFrequency(uint16_t refreshFrequencyMs);
  void setRtcSetFrequency(uint32_t rtcSetFrequencyMs);
  void setHttpTimeout(uint32_t httpTimeoutMs);
  void setDisplayOn(uint8_t isOn);
  void setDisplayAlternate(uint8_t isAlternating);
  void setServerName(const char* serverName);
  void setPasscode(const char* Passcode);

  uint32_t getEepromWrites();
  Ethernet_Properties_t getEthernetProperties();
  IPAddress getLocalIp();
  IPAddress getSubnet();
  IPAddress getDns1Ip();
  IPAddress getDns2Ip();
  IPAddress getGatewayIp();
  String getLocalIpStr();
  String getSubnetStr();
  String getDns1IpStr();
  String getDns2IpStr();
  String getGatewayIpStr();
  uint16_t getLogMax();
  uint16_t getErrorMax();
  uint16_t getRefreshFrequency();
  uint32_t getRtcSetFrequency();
  uint32_t getHttpTimeout();
  uint8_t getDisplayOn();
  uint8_t getDisplayAlternate();
  String getServerName();
  bool isPasscode(const char* Passcode);
  bool isDhcp();
  
  bool saveProperties();
  bool loadProperties();

  String generateIpString(IPAddress);

private:
  // The override code is 5281-7493-0285
  std::array<std::uint8_t, 32> OVERRIDE_CODE_HASH {
    0xd6, 0xcb, 0xf0, 0x02, 0xe9, 0x40, 0x65, 0x6c,
    0x36, 0x6a, 0x34, 0x80, 0x1d, 0x86, 0xfa, 0xc0,
    0x74, 0xd3, 0x00, 0x55, 0xe7, 0x80, 0x26, 0xcc,
    0xa9, 0x7f, 0xce, 0xf9, 0x2c, 0xdf, 0xd1, 0xd1
  };
  
  uint8_t  savedData_; // 1 for true and 0 for false
  uint32_t eepromWrites_;
  Ethernet_Properties_t ethProps_;
  char strLocalIp_[16];
  char strSubnet_[16];
  char strDns1Ip_[16];
  char strDns2Ip_[16];
  char strGatewayIp_[16];
  uint16_t logMax_;
  uint16_t errorMax_;
  uint16_t refreshFrequencyMs_;
  uint32_t rtcSetFrequencyMs_;
  uint32_t httpTimeoutMs_;
  uint8_t displayOn_; // 1 for true and 0 for false
  uint8_t displayAlternate_; // 1 for true and 0 for false
  char serverName_[255]; // name of the server
  std::array<std::uint8_t, 32> PasscodeHash_; // Password SHA256 hash
  bool isDhcp_;

  void generateAllIpStrings();
};
