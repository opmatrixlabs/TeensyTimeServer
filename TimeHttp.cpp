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

#include "TimeHttp.h"

#include <algorithm>

namespace {

constexpr std::size_t MAX_HTTP_HEADER_BYTES = 4096;
constexpr uint32_t FIRMWARE_UPLOAD_IDLE_TIMEOUT_MILLIS = 15000;
constexpr uint32_t FIRMWARE_UPLOAD_TOTAL_TIMEOUT_MILLIS = 600000;

enum class HeaderLookup : uint8_t {
  Missing,
  Found,
  Duplicate
};

char lowerAscii(const char value) {
  if (value >= 'A' && value <= 'Z')
    return static_cast<char>(value + ('a' - 'A'));
  return value;
}

bool textEqualsIgnoreCase(const String& value, const char* expected) {
  if (expected == nullptr)
    return false;
  std::size_t expectedLength = 0;
  while (expected[expectedLength] != '\0')
    ++expectedLength;
  if (value.length() != expectedLength)
    return false;
  for (std::size_t index = 0; index < expectedLength; ++index) {
    if (lowerAscii(value[index]) != lowerAscii(expected[index]))
      return false;
  }
  return true;
}

bool requestLineMatches(const String& headers, const char* expected) {
  int end = headers.indexOf('\n');
  if (end < 0)
    return false;
  if (end > 0 && headers[end - 1] == '\r')
    --end;
  return headers.substring(0, end) == expected;
}

HeaderLookup findHeaderValueIgnoreCase(const String& headers, const char* name, String* value) {
  if (name == nullptr || value == nullptr)
    return HeaderLookup::Missing;

  std::size_t nameLength = 0;
  while (name[nameLength] != '\0')
    ++nameLength;

  int lineStart = headers.indexOf('\n');
  if (lineStart < 0)
    return HeaderLookup::Missing;
  ++lineStart;
  bool found = false;
  while (lineStart < static_cast<int>(headers.length())) {
    int lineEnd = headers.indexOf('\n', lineStart);
    if (lineEnd < 0)
      lineEnd = headers.length();
    int contentEnd = lineEnd;
    if (contentEnd > lineStart && headers[contentEnd - 1] == '\r')
      --contentEnd;
    if (contentEnd == lineStart)
      break;

    const int colon = headers.indexOf(':', lineStart);
    if (colon > lineStart && colon < contentEnd && static_cast<std::size_t>(colon - lineStart) == nameLength) {
      bool nameMatches = true;
      for (std::size_t index = 0; index < nameLength; ++index) {
        if (lowerAscii(headers[lineStart + index]) != lowerAscii(name[index])) {
          nameMatches = false;
          break;
        }
      }
      if (nameMatches) {
        if (found)
          return HeaderLookup::Duplicate;
        int valueStart = colon + 1;
        while (valueStart < contentEnd && (headers[valueStart] == ' ' || headers[valueStart] == '\t'))
          ++valueStart;
        while (contentEnd > valueStart && (headers[contentEnd - 1] == ' ' || headers[contentEnd - 1] == '\t'))
          --contentEnd;
        *value = headers.substring(valueStart, contentEnd);
        found = true;
      }
    }
    lineStart = lineEnd + 1;
  }
  return found ? HeaderLookup::Found : HeaderLookup::Missing;
}

bool parseUint32Strict(const String& value, uint32_t* parsed) {
  if (parsed == nullptr || value.length() == 0)
    return false;
  uint64_t result = 0;
  for (std::size_t index = 0; index < value.length(); ++index) {
    const char digit = value[index];
    if (digit < '0' || digit > '9')
      return false;
    result = result * 10U + static_cast<uint8_t>(digit - '0');
    if (result > UINT32_MAX)
      return false;
  }
  *parsed = static_cast<uint32_t>(result);
  return true;
}

bool mediaTypeEquals(const String& value, const char* expected) {
  int end = value.indexOf(';');
  if (end < 0)
    end = value.length();
  while (end > 0 && (value[end - 1] == ' ' || value[end - 1] == '\t'))
    --end;
  int start = 0;
  while (start < end && (value[start] == ' ' || value[start] == '\t'))
    ++start;
  return textEqualsIgnoreCase(value.substring(start, end), expected);
}

bool hasHexFileExtension(const String& fileName) {
  if (fileName.length() < 5 || fileName.length() > 128)
    return false;
  const std::size_t start = fileName.length() - 4;
  return fileName[start] == '.' && lowerAscii(fileName[start + 1]) == 'h' &&
         lowerAscii(fileName[start + 2]) == 'e' && lowerAscii(fileName[start + 3]) == 'x';
}

} // namespace

TimeHttp::TimeHttp() 
= default;

TimeHttp::~TimeHttp()
= default;

void TimeHttp::restartTeensy() {
    SCB_AIRCR = 0x05FA0004;  // Write the reset value directly
    while (true) {
        // Wait for reset
    }
}

void TimeHttp::setHttpClient(EthernetClient* client) {
  pHttpClient_ = client;
}

EthernetClient* TimeHttp::getHttpClient() {
  return pHttpClient_;
}

void TimeHttp::setAppName(const String appName) {
  appName_ = appName;
}

String TimeHttp::getAppName() {
  return appName_;
}

void TimeHttp::setProperties(Properties* properties) {
  pProperties_ = properties;
}

Properties* TimeHttp::getProperties() {
  return pProperties_;
}

void TimeHttp::setLocalIp(String localIp) {
  localIp_ = localIp;
}

void TimeHttp::setGpsFixType(String gpsFixType) {
  gpsFixType_ = gpsFixType;
}

void TimeHttp::setGpsISO8601Time(String gpsISO8601Time) {
  gpsISO8601Time_ = gpsISO8601Time;
}

String TimeHttp::getLocalIp() {
  return localIp_;
}

String TimeHttp::getGpsFixType() {
  return gpsFixType_;
}

String TimeHttp::getGpsISO8601Time() {
  return gpsISO8601Time_;
}

void TimeHttp::setRtcISO8601Time(String rtcISO8601Time) {
  rtcISO8601Time_ = rtcISO8601Time;
}

String TimeHttp::getRtcISO8601Time() {
  return rtcISO8601Time_;
}

void TimeHttp::setConfigString(String* config) {
  pConfig_ = config;
}

String* TimeHttp::getConfigString() {
  return pConfig_;
}

void TimeHttp::setConfigFunction(void (*fptrGetGpsConfig)()) {
    fptrGetGpsConfig_ = fptrGetGpsConfig;
}

void TimeHttp::setGpsTimeFunction(String (*fptrGetGpsTime)()) {
  fptrGetGpsTime_ = fptrGetGpsTime;
}

void TimeHttp::setRtcTimeFunction(String (*fptrGetRtcTime)()) {
  fptrGetRtcTime_ = fptrGetRtcTime;
}

void TimeHttp::setUpdateRtcFunction(void (*fptrUpdateRtc)()) {
    fptrUpdateRtc_ = fptrUpdateRtc;
}

void TimeHttp::setAddLogFunction(void (*fptrAddLog)(String log)) {
    fptrAddLog_ = fptrAddLog;
}

void TimeHttp::setAddErrorFunction(void (*fptrAddError)(String error)) {
    fptrAddError_ = fptrAddError;
}

void TimeHttp::setLogArray(std::list<String>* usageLog) {
  pUsageLog_ = usageLog;
}

std::list<String>* TimeHttp::getLogArray() {
  return pUsageLog_;
}

void TimeHttp::setErrorArray(std::list<String>* errorLog) {
  pErrorLog_ = errorLog;
}

void TimeHttp::setFirmwareUpdater(FirmwareUpdater* firmwareUpdater) {
  pFirmwareUpdater_ = firmwareUpdater;
}

void TimeHttp::setFirmwareMaintenanceFunction(void (*function)(bool active)) {
  fptrFirmwareMaintenance_ = function;
}

void TimeHttp::setFirmwareInstallFunction(void (*function)()) {
  fptrFirmwareInstall_ = function;
}

std::list<String>* TimeHttp::getErrorArray() {
  return pErrorLog_;
}

// Percent-decode a String (e.g., "Hello%20World%21" -> "Hello World!")
// Decodes only %XX hex bytes (ASCII). Unknown/invalid sequences keep the '%' as-is.
// To decode '+' to mean space (common in URL query strings), set decodePlus=true.
String TimeHttp::percentDecode(const String& strHtml, bool decodePlus) {
  String out;
  out.reserve(strHtml.length()); // best-effort to avoid reallocs

  const int n = strHtml.length();
  for (int i = 0; i < n; ++i) {
    char c = strHtml[i];

    if (decodePlus && c == '+') {
      out += ' ';
      continue;
    }

    if (c == '%' && i + 2 < n) {
      int8_t hi = hexNibble(strHtml[i + 1]);
      int8_t lo = hexNibble(strHtml[i + 2]);
      if (hi >= 0 && lo >= 0) {
        char b = char((hi << 4) | lo);
        out += b;
        i += 2; // skip the two hex digits we just consumed
        continue;
      }
      // else fall through and emit '%' literally if not valid hex
    }

    out += c; // normal character or lone/invalid '%'
  }

  return out;
}

HtmlBodyValue_t TimeHttp::getValueFromBody(String key, const String& body, int startIndex) {
  HtmlBodyValue_t bodyValue;
  int index = 0;
  key.append('=');
  if ((index = body.indexOf(key, startIndex)) >= 0) {
    bodyValue.found = true;
    index += key.length();  // length of key
    bodyValue.endIndex = body.indexOf('&', index);
    if (bodyValue.endIndex == -1) {
      bodyValue.value = body.substring(index); // if it is the last parameter
      bodyValue.endIndex = body.length();
    }
    else {
      bodyValue.value = body.substring(index, bodyValue.endIndex);
  	  bodyValue.endIndex++; // skip the &
    }
    bodyValue.value = percentDecode(bodyValue.value, true);
  }
  else {
    bodyValue.found = false;
    bodyValue.endIndex = startIndex;
    bodyValue.value = "";
  }
  return bodyValue;
}

bool TimeHttp::processRequest(EthernetClient* client, String localIp, String gpsFixType, String gpsISO8601Time, String rtcISO8601Time) {
  pHttpClient_ = client;
  localIp_ = localIp;
  gpsFixType_ = gpsFixType;
  gpsISO8601Time_ = gpsISO8601Time;
  rtcISO8601Time_ = rtcISO8601Time;
  return processRequest();
}

bool TimeHttp::processRequest(EthernetClient* client, String localIp, String gpsFixType) {
  pHttpClient_ = client;
  localIp_ = localIp;
  gpsFixType_ = gpsFixType;
  return processRequest();
}

bool TimeHttp::processRequest() { // need verify httpClient before calling this method.
  String headers = "";
  String body = "";
  String currentLine = ""; // String to hold incoming data from the client
  uint32_t contentLength = 0;
  bool isHeaders = true;
  elapsedMillis httpTimer;
  WebPage selectedPage = WebPage::HOME;
  while (pHttpClient_->connected() && (httpTimer < pProperties_->getHttpTimeout())) {
    if (pHttpClient_->available()) {
      char c = pHttpClient_->read();
      if (isHeaders) {
        headers.append(c);
        if (headers.length() > MAX_HTTP_HEADER_BYTES) {
          sendPlainTextResponse(431,
                                "Request Header Fields Too Large",
                                "Request headers exceed the server limit.\n");
          goto END_LOOP;
        }
        // The end of the HTTP headers is indicated by a blank line.
        if (c == '\n') {
          // If the current line is blank, and there is two newline characters in a row, then that is the end of the HTTP headers.
          if (currentLine.length() == 0) {
            isHeaders = false;
            // Figure out which page to serve
            if (requestLineMatches(headers, "POST /firmware HTTP/1.1")) {
              processFirmwareRequest(headers);
              goto END_LOOP;
            }
            else if (headers.indexOf("GET /gpsconfig") >= 0) {
              if (headers.indexOf("?action=reload") >= 0) {
                fptrGetGpsConfig_();
                // Redirect back to the path to process URL form values
                pHttpClient_->println("HTTP/1.1 302 Found");
                pHttpClient_->println("Location: /gpsconfig");
                pHttpClient_->println("Connection: close");
                pHttpClient_->println();
              } 
              sendConfigPage();
              // Done. Break out of the loop.
              goto END_LOOP; 
            }
            else if (headers.indexOf("GET /logs") >= 0) {
              sendLogPage();
              // Done. Break out of the loop.
              goto END_LOOP; 
            }
            else if (headers.indexOf("POST /logs") >= 0) {
              selectedPage = WebPage::LOG;
              // Get the Content-Length
              int keyIndex = headers.indexOf("Content-Length");
              if (keyIndex > 0) {
                int valueIndex = headers.indexOf(':', keyIndex) + 2; // skip the : and space
                int crIndex = headers.indexOf("\r\n", keyIndex);
                if (crIndex > valueIndex) { // should always be the true, but is here to protect against memory corruption
                  String strContentLength = headers.substring(valueIndex, crIndex);
                  contentLength = strContentLength.toInt();
                }
                // No break, because we need to process the body
              }
              else { 
                break;
              }            
            }
            else if (headers.indexOf("GET /errors") >= 0) {     
              sendErrorPage();
              // Done. Break out of the loop.
              goto END_LOOP; 
            }
            else if (headers.indexOf("POST /errors") >= 0) {
              selectedPage = WebPage::ERROR;
              // Get the Content-Length
              int keyIndex = headers.indexOf("Content-Length");
              if (keyIndex > 0) {
                int valueIndex = headers.indexOf(':', keyIndex) + 2; // skip the : and space
                int crIndex = headers.indexOf("\r\n", keyIndex);
                if (crIndex > valueIndex) { // should always be the true, but is here to protect against memory corruption
                  String strContentLength = headers.substring(valueIndex, crIndex);
                  contentLength = strContentLength.toInt();
                }
                // No break, because we need to process the body
              }
              else { 
                // Done. Break out of the loop.                
                goto END_LOOP; 
              }
            }
            else if (headers.indexOf("GET /setup") >= 0) { 
              sendSetupPage(false);
              // Done. Break out of the loop.
              goto END_LOOP; 
            }
            else if (headers.indexOf("POST /setup") >= 0) {
              selectedPage = WebPage::SETUP;
              // Get the Content-Length
              int keyIndex = headers.indexOf("Content-Length");
              if (keyIndex > 0) {
                int valueIndex = headers.indexOf(':', keyIndex) + 2; // skip the : and space
                int crIndex = headers.indexOf("\r\n", keyIndex);
                if (crIndex > valueIndex) { // should always be the true, but is here to protect against memory corruption
                  String strContentLength = headers.substring(valueIndex, crIndex);
                  contentLength = strContentLength.toInt();
                }
                // No break, because we need to process the body
              }
              else { 
                // Done. Break out of the loop.
                goto END_LOOP; 
              }
            }
            else if (headers.indexOf("GET /rtc") >= 0) {
              if (fptrGetRtcTime_ != nullptr)
                rtcISO8601Time_ = fptrGetRtcTime_();
              sendRtcTime();
              goto END_LOOP;
            }
            else {
              if (headers.indexOf("?action=setRtc") >= 0) {
                fptrUpdateRtc_();
                // Redirect back to the path to process URL form values
                pHttpClient_->println("HTTP/1.1 302 Found");
                pHttpClient_->println("Location: /");
                pHttpClient_->println("Connection: close");
                pHttpClient_->println();
                goto END_LOOP;
              }
              if (fptrGetGpsTime_ != nullptr)
                gpsISO8601Time_ = fptrGetGpsTime_();
              if (fptrGetRtcTime_ != nullptr)
                rtcISO8601Time_ = fptrGetRtcTime_();
              sendHomePage();
              // Done. Break out of the loop.
              goto END_LOOP; 
           }
          }
          else { // if you got a newline, then clear currentLine
            currentLine = "";
          }
        }
        else if (c != '\r') {
          // It is something other than a carriage return character, add it to the end of the currentLine.
          currentLine += c;
        }
      }
      else { // this is the body
        body.append(c);
        if (--contentLength == 0) {
		      String Passcode = "";	
          int32_t startIndex = 0;
          int32_t endIndex = 0;		  
          // The body is now loaded
          if ((startIndex = body.indexOf("Passcode=")) >= 0) {
            startIndex += 9;  // length of key
            endIndex = body.indexOf('&', startIndex);
            if (endIndex == -1) Passcode = body.substring(startIndex); // if it is the last parameter
            else Passcode = body.substring(startIndex, endIndex);
            Passcode = percentDecode(Passcode, false);
          }
		      if (pProperties_->isPasscode(Passcode.c_str())) {	
            switch (selectedPage) {
              case WebPage::LOG :
                if (body.indexOf("action=clear") >= 0) {
                  pUsageLog_->clear();
                  fptrAddLog_("Usage Logs cleared from " +  pProperties_->generateIpString(pHttpClient_->remoteIP()));
                  sendLogPage();
                  // Done. Break out of the loop.
                  goto END_LOOP; 
                }               
                break;
              case WebPage::ERROR :
                if (body.indexOf("action=clear") >= 0) {
                  pErrorLog_->clear();
                  fptrAddLog_("Error Logs cleared from " +  pProperties_->generateIpString(pHttpClient_->remoteIP()));
                  sendErrorPage();
                  // Done. Break out of the loop.
                  goto END_LOOP; 
                }                    
                break;
              case WebPage::SETUP :
                if (body.indexOf("action=reboot") >= 0) {
                  sendHttpWait();
                  pHttpClient_->stop();
                  // Reboot the system
                  //_reboot_Teensyduino_(); // restarts and puts the Teensy into program mode
                  restartTeensy();
                  // Code should never get here
                }
                else if (body.indexOf("action=reset") >= 0) {
                  pProperties_->clearEEPROM();
                  sendHttpWait();
                  pHttpClient_->stop();
                  // Reboot the system
                  //_reboot_Teensyduino_(); // restarts and puts the Teensy into program mode
                  restartTeensy();
                  // Code should never get here
                }			
                else if (body.indexOf("action=save") >= 0) {
                  //Serial.print("DEBUG: action = "); Serial.println("save");
                  HtmlBodyValue_t serverName;
                  HtmlBodyValue_t newPasscode;
                  HtmlBodyValue_t localIp;
                  HtmlBodyValue_t subnet;
                  HtmlBodyValue_t dns1Ip;
                  HtmlBodyValue_t dns2Ip;
                  HtmlBodyValue_t gatewayIp;
                  HtmlBodyValue_t logMax;
                  HtmlBodyValue_t errorMax;
                  HtmlBodyValue_t refreshFrequencyMs;
                  HtmlBodyValue_t rtcSetFrequencyMs;
                  HtmlBodyValue_t httpTimeoutMs;
                  HtmlBodyValue_t display;
                  HtmlBodyValue_t alternate;
                  bool restartRequired = false;

                  serverName = getValueFromBody("serverName", body, 0);
                  if (serverName.found) {
                    const String previousValue = pProperties_->getServerName();
                    pProperties_->setServerName(serverName.value.c_str());
                    restartRequired |= pProperties_->getServerName() != previousValue;
                  }
                  newPasscode = getValueFromBody("newPasscode", body, serverName.endIndex);
                  if (newPasscode.found && newPasscode.value.length() > 0) {
                    pProperties_->setPasscode(newPasscode.value.c_str());
                    restartRequired = true;
                  }
                  localIp = getValueFromBody("localIp", body, newPasscode.endIndex);
                  if (localIp.found) {
                    const String previousValue = pProperties_->getLocalIpStr();
                    pProperties_->setLocalIp(localIp.value.c_str());
                    restartRequired |= pProperties_->getLocalIpStr() != previousValue;
                  }
                  subnet = getValueFromBody("subnet", body, localIp.endIndex);
                  if (subnet.found) {
                    const String previousValue = pProperties_->getSubnetStr();
                    pProperties_->setSubnet(subnet.value.c_str());
                    restartRequired |= pProperties_->getSubnetStr() != previousValue;
                  }
                  dns1Ip = getValueFromBody("dns1Ip", body, subnet.endIndex);
                  if (dns1Ip.found) {
                    const String previousValue = pProperties_->getDns1IpStr();
                    pProperties_->setDns1Ip(dns1Ip.value.c_str());
                    restartRequired |= pProperties_->getDns1IpStr() != previousValue;
                  }
                  dns2Ip = getValueFromBody("dns2Ip", body, dns1Ip.endIndex);
                  if (dns2Ip.found) {
                    const String previousValue = pProperties_->getDns2IpStr();
                    pProperties_->setDns2Ip(dns2Ip.value.c_str());
                    restartRequired |= pProperties_->getDns2IpStr() != previousValue;
                  }
                  gatewayIp = getValueFromBody("gatewayIp", body, dns2Ip.endIndex);
                  if (gatewayIp.found) {
                    const String previousValue = pProperties_->getGatewayIpStr();
                    pProperties_->setGatewayIp(gatewayIp.value.c_str());
                    restartRequired |= pProperties_->getGatewayIpStr() != previousValue;
                  }
                  logMax = getValueFromBody("logMax", body, gatewayIp.endIndex);
                  if (logMax.found) {
                    const uint16_t previousValue = pProperties_->getLogMax();
                    pProperties_->setLogMax(logMax.value.toInt());
                    restartRequired |= pProperties_->getLogMax() != previousValue;
                  }
                  errorMax = getValueFromBody("errorMax", body, logMax.endIndex);
                  if (errorMax.found) {
                    const uint16_t previousValue = pProperties_->getErrorMax();
                    pProperties_->setErrorMax(errorMax.value.toInt());
                    restartRequired |= pProperties_->getErrorMax() != previousValue;
                  }
                  refreshFrequencyMs = getValueFromBody("refreshFrequencyMs", body, errorMax.endIndex);
                  if (refreshFrequencyMs.found) {
                    const uint16_t previousValue = pProperties_->getRefreshFrequency();
                    pProperties_->setRefreshFrequency(refreshFrequencyMs.value.toInt());
                    restartRequired |= pProperties_->getRefreshFrequency() != previousValue;
                  }
                  rtcSetFrequencyMs = getValueFromBody("rtcSetFrequencyMs", body, refreshFrequencyMs.endIndex);
                  if (rtcSetFrequencyMs.found) {
                    const uint32_t previousValue = pProperties_->getRtcSetFrequency();
                    pProperties_->setRtcSetFrequency(rtcSetFrequencyMs.value.toInt());
                    restartRequired |= pProperties_->getRtcSetFrequency() != previousValue;
                  }
                  httpTimeoutMs = getValueFromBody("httpTimeoutMs", body, rtcSetFrequencyMs.endIndex);
                  if (httpTimeoutMs.found) {
                    const uint32_t previousValue = pProperties_->getHttpTimeout();
                    pProperties_->setHttpTimeout(httpTimeoutMs.value.toInt());
                    restartRequired |= pProperties_->getHttpTimeout() != previousValue;
                  }
                  display = getValueFromBody("display", body, httpTimeoutMs.endIndex);
                  if (display.found) pProperties_->setDisplayOn(display.value.toInt());              
                  alternate = getValueFromBody("alternate", body, display.endIndex);
                  if (alternate.found) pProperties_->setDisplayAlternate(alternate.value.toInt()); 

                  pProperties_->saveProperties();             
                  sendSetupPage(restartRequired);
                }
                else sendSetupPage(false);            
                break;
              default:
                fptrAddError_("Web POST error.");
            }
          }
          else {
            fptrAddLog_("Invalid Passcode entered from " + pProperties_->generateIpString(pHttpClient_->remoteIP()));
			      sendPasscodeError(selectedPage);
          }
          // Done. Break out of the loop.
          goto END_LOOP; 
        }
      }
    }
  }
  END_LOOP: ;
  // Close the connection
  pHttpClient_->stop();
  return true;
}

void TimeHttp::processFirmwareRequest(const String& headers) {
  if (pFirmwareUpdater_ == nullptr || fptrFirmwareMaintenance_ == nullptr || fptrFirmwareInstall_ == nullptr) {
    sendPlainTextResponse(503, "Service Unavailable", "Firmware update service is not configured.\n");
    return;
  }

  String headerValue;
  const HeaderLookup transferEncoding = findHeaderValueIgnoreCase(headers, "Transfer-Encoding", &headerValue);
  if (transferEncoding != HeaderLookup::Missing) {
    sendPlainTextResponse(400, "Bad Request", "Transfer-Encoding is not supported for firmware uploads.\n");
    return;
  }

  const HeaderLookup lengthLookup = findHeaderValueIgnoreCase(headers, "Content-Length", &headerValue);
  if (lengthLookup == HeaderLookup::Missing) {
    sendPlainTextResponse(411, "Length Required", "A Content-Length header is required.\n");
    return;
  }
  if (lengthLookup == HeaderLookup::Duplicate) {
    sendPlainTextResponse(400, "Bad Request", "Content-Length must appear exactly once.\n");
    return;
  }
  uint32_t contentLength = 0;
  if (!parseUint32Strict(headerValue, &contentLength) || contentLength == 0) {
    sendPlainTextResponse(400, "Bad Request", "Content-Length must be a positive decimal value.\n");
    return;
  }
  if (contentLength > FirmwareUpdater::maxUploadBytes()) {
    sendPlainTextResponse(413, "Content Too Large", "Firmware upload exceeds the staging limit.\n");
    return;
  }

  const HeaderLookup typeLookup = findHeaderValueIgnoreCase(headers, "Content-Type", &headerValue);
  if (typeLookup != HeaderLookup::Found || !mediaTypeEquals(headerValue, "application/x-intel-hex")) {
    sendPlainTextResponse(415, "Unsupported Media Type", "Upload one plain Intel HEX (.hex) file.\n");
    return;
  }

  const HeaderLookup nameLookup = findHeaderValueIgnoreCase(headers, "X-Firmware-Name", &headerValue);
  if (nameLookup != HeaderLookup::Found) {
    sendPlainTextResponse(400, "Bad Request", "X-Firmware-Name must appear exactly once.\n");
    return;
  }
  const String fileName = percentDecode(headerValue, false);
  bool printableName = true;
  for (std::size_t index = 0; index < fileName.length(); ++index) {
    if (fileName[index] < 0x20 || fileName[index] > 0x7E) {
      printableName = false;
      break;
    }
  }
  if (!printableName || !hasHexFileExtension(fileName)) {
    sendPlainTextResponse(415, "Unsupported Media Type", "Firmware filename must end in .hex.\n");
    return;
  }

  const HeaderLookup passcodeLookup = findHeaderValueIgnoreCase(headers, "X-Firmware-Passcode", &headerValue);
  if (passcodeLookup != HeaderLookup::Found) {
    sendPlainTextResponse(403, "Forbidden", "A single valid firmware passcode header is required.\n");
    return;
  }
  const String passcode = percentDecode(headerValue, false);
  bool validPasscodeEncoding = true;
  for (std::size_t index = 0; index < passcode.length(); ++index) {
    if (passcode[index] == '\0' || passcode[index] == '\r' || passcode[index] == '\n') {
      validPasscodeEncoding = false;
      break;
    }
  }
  if (!validPasscodeEncoding || !pProperties_->isPasscode(passcode.c_str())) {
    if (fptrAddLog_ != nullptr)
      fptrAddLog_("Invalid firmware passcode entered from " + pProperties_->generateIpString(pHttpClient_->remoteIP()));
    sendPlainTextResponse(403, "Forbidden", "Firmware passcode is invalid.\n");
    return;
  }

  bool sendContinue = false;
  const HeaderLookup expectLookup = findHeaderValueIgnoreCase(headers, "Expect", &headerValue);
  if (expectLookup == HeaderLookup::Duplicate ||
      (expectLookup == HeaderLookup::Found && !textEqualsIgnoreCase(headerValue, "100-continue"))) {
    sendPlainTextResponse(417, "Expectation Failed", "Only the 100-continue expectation is supported.\n");
    return;
  }
  sendContinue = expectLookup == HeaderLookup::Found;

  if (!pFirmwareUpdater_->begin(contentLength)) {
    const String message = String("Firmware update cannot start: ") + pFirmwareUpdater_->error() + "\n";
    sendPlainTextResponse(pFirmwareUpdater_->failure() == FirmwareUpdateFailure::ServerState ? 503 : 422,
                          pFirmwareUpdater_->failure() == FirmwareUpdateFailure::ServerState ? "Service Unavailable" : "Unprocessable Content",
                          message);
    return;
  }

  fptrFirmwareMaintenance_(true);
  if (sendContinue) {
    pHttpClient_->println("HTTP/1.1 100 Continue");
    pHttpClient_->println();
  }

  uint8_t uploadBuffer[256];
  uint32_t remaining = contentLength;
  uint32_t lastActivityMillis = millis();
  const uint32_t uploadStartedMillis = lastActivityMillis;
  while (remaining > 0) {
    if (static_cast<uint32_t>(millis() - uploadStartedMillis) >= FIRMWARE_UPLOAD_TOTAL_TIMEOUT_MILLIS) {
      pFirmwareUpdater_->abort();
      fptrFirmwareMaintenance_(false);
      sendPlainTextResponse(408, "Request Timeout", "Firmware upload timed out before completion.\n");
      return;
    }

    const int available = pHttpClient_->available();
    if (available > 0) {
      const std::size_t requested = std::min<std::size_t>(
          { static_cast<std::size_t>(available), sizeof(uploadBuffer), static_cast<std::size_t>(remaining) });
      const int received = pHttpClient_->read(uploadBuffer, requested);
      if (received > 0) {
        remaining -= static_cast<uint32_t>(received);
        if (!pFirmwareUpdater_->write(uploadBuffer, static_cast<std::size_t>(received))) {
          const FirmwareUpdateFailure failure = pFirmwareUpdater_->failure();
          const String message = String("Firmware rejected: ") + pFirmwareUpdater_->error() + "\n";
          pFirmwareUpdater_->abort();
          fptrFirmwareMaintenance_(false);
          sendPlainTextResponse(failure == FirmwareUpdateFailure::FlashStorage ? 500 : 422,
                                failure == FirmwareUpdateFailure::FlashStorage ? "Internal Server Error" : "Unprocessable Content",
                                message);
          return;
        }
        lastActivityMillis = millis();
        continue;
      }
    }

    const uint32_t now = millis();
    if (!pHttpClient_->connected()) {
      pFirmwareUpdater_->abort();
      fptrFirmwareMaintenance_(false);
      if (fptrAddLog_ != nullptr)
        fptrAddLog_("Firmware upload disconnected before completion");
      return;
    }
    if (static_cast<uint32_t>(now - lastActivityMillis) >= FIRMWARE_UPLOAD_IDLE_TIMEOUT_MILLIS) {
      pFirmwareUpdater_->abort();
      fptrFirmwareMaintenance_(false);
      sendPlainTextResponse(408, "Request Timeout", "Firmware upload timed out before completion.\n");
      return;
    }
    delay(1);
  }

  if (static_cast<uint32_t>(millis() - uploadStartedMillis) >= FIRMWARE_UPLOAD_TOTAL_TIMEOUT_MILLIS) {
    pFirmwareUpdater_->abort();
    fptrFirmwareMaintenance_(false);
    sendPlainTextResponse(408, "Request Timeout", "Firmware upload timed out before completion.\n");
    return;
  }

  if (!pFirmwareUpdater_->finish()) {
    const FirmwareUpdateFailure failure = pFirmwareUpdater_->failure();
    const String message = String("Firmware rejected: ") + pFirmwareUpdater_->error() + "\n";
    pFirmwareUpdater_->abort();
    fptrFirmwareMaintenance_(false);
    sendPlainTextResponse(failure == FirmwareUpdateFailure::FlashStorage ? 500 : 422,
                          failure == FirmwareUpdateFailure::FlashStorage ? "Internal Server Error" : "Unprocessable Content",
                          message);
    return;
  }

  const uint32_t imageSize = pFirmwareUpdater_->imageSize();
  if (fptrAddLog_ != nullptr)
    fptrAddLog_("Firmware image validated from " + pProperties_->generateIpString(pHttpClient_->remoteIP()) +
                ": " + String(imageSize) + " bytes");
  sendHttpWait();
  // EthernetClient::write waits for W5500 SEND_OK. Avoid its unbounded flush()
  // loop, then give the graceful FIN a short, bounded timeout.
  pHttpClient_->setConnectionTimeout(250);
  pHttpClient_->stop();

  // The callback schedules installation after a short browser-rendering grace
  // period. The main loop performs the non-returning flash replacement.
  fptrFirmwareInstall_();
}

void TimeHttp::sendPlainTextResponse(const uint16_t statusCode, const char* reason, const String& message) {
  pHttpClient_->print("HTTP/1.1 ");
  pHttpClient_->print(statusCode);
  pHttpClient_->print(' ');
  pHttpClient_->println(reason);
  pHttpClient_->println("Content-Type: text/plain; charset=utf-8");
  pHttpClient_->println("Cache-Control: no-store");
  pHttpClient_->print("Content-Length: ");
  pHttpClient_->println(message.length());
  pHttpClient_->println("Connection: close");
  pHttpClient_->println();
  pHttpClient_->print(message);
}

void TimeHttp::sendHomePage(const String appName, Properties* properties, String localIp, String gpsISO8601Time, String rtcISO8601Time) {
  appName_ = appName;
  pProperties_ = properties;
  localIp_ = localIp;
  gpsISO8601Time_ = gpsISO8601Time;
  rtcISO8601Time_ = rtcISO8601Time;
  sendHomePage();
}

void TimeHttp::sendHomePage() {
  pHttpClient_->println("HTTP/1.1 200 OK");
  pHttpClient_->println("Content-Type: text/html");
  pHttpClient_->println("Cache-Control: no-store");
  pHttpClient_->println("Connection: close");
  pHttpClient_->println();
  pHttpClient_->print("<!DOCTYPE html><html lang=\"en\"><head><title>"); pHttpClient_->print(pProperties_->getServerName()); pHttpClient_->println("</title></head><body>");
  pHttpClient_->print("<h1>"); pHttpClient_->print(appName_); pHttpClient_->println("</h1>");
  pHttpClient_->println("<h2>Status</h2>");
  // Button to go to other pages
  pHttpClient_->println("<div style='display: flex; gap: 16px; margin-top: 24px;'>");
  pHttpClient_->println("<form action=\"/gpsconfig\" method=\"get\" style=\"margin:0;\">");
  pHttpClient_->println("<button type=\"submit\">GPS-Time Config</button>");
  pHttpClient_->println("</form>");
  pHttpClient_->println("<form action=\"/logs\" method=\"get\" style=\"margin:0;\">");
  pHttpClient_->println("<button type=\"submit\">Usage Logs</button>");
  pHttpClient_->println("</form>");
  pHttpClient_->println("<form action=\"/errors\" method=\"get\" style=\"margin:0;\">");
  pHttpClient_->println("<button type=\"submit\">Error Logs</button>");
  pHttpClient_->println("</form>");
  pHttpClient_->println("<form action=\"/setup\" method=\"get\" style=\"margin:0;\">");
  pHttpClient_->println("<button type=\"submit\">Setup</button>");
  pHttpClient_->println("</form>");
  pHttpClient_->println("</div>");
  //
  pHttpClient_->print("<br><p><b>IP Address:</b> ");
  pHttpClient_->print(localIp_);
  pHttpClient_->println("</p>");
  pHttpClient_->print("<p><b>GPS Status:</b> ");
  pHttpClient_->print(gpsFixType_);
  pHttpClient_->println("</p>");
  pHttpClient_->print("<p><b>GPS Time:</b> ");
  pHttpClient_->print(gpsISO8601Time_);
  pHttpClient_->println("</p>");
  pHttpClient_->print("<p><b>RTC Time:</b> ");
  pHttpClient_->print(rtcISO8601Time_);
  pHttpClient_->println("</p>");
  pHttpClient_->println("<form action=\"/\" method=\"get\">");
  pHttpClient_->println("<button type=\"submit\" name=\"action\" value=\"setRtc\">Set RTC</button>");
  pHttpClient_->println("</form>");
  pHttpClient_->println("<br><br><p>Note:<br>The RTC fractional field is hundredths of a second (10 ms resolution).");
  pHttpClient_->println("<br>If the GPS and RTC times are off by more than a second, click the \"Set RTC\" button.</p>");
  pHttpClient_->println("</body></html>");
}

void TimeHttp::sendRtcTime() {
  pHttpClient_->println("HTTP/1.1 200 OK");
  pHttpClient_->println("Content-Type: text/plain");
  pHttpClient_->println("Cache-Control: no-store");
  pHttpClient_->println("Connection: close");
  pHttpClient_->println();
  pHttpClient_->print(rtcISO8601Time_);
}

void TimeHttp::sendConfigPage(String* config) {
  pConfig_ = config;
  sendConfigPage();
}

void TimeHttp::sendConfigPage() {
  pHttpClient_->println("HTTP/1.1 200 OK");
  pHttpClient_->println("Content-Type: text/html");
  pHttpClient_->println("Connection: close");
  pHttpClient_->println();
  pHttpClient_->print("<!DOCTYPE html><html lang=\"en\"><head><title>"); pHttpClient_->print(pProperties_->getServerName()); pHttpClient_->println("</title></head><body>");
  pHttpClient_->print("<h1>"); pHttpClient_->print(appName_); pHttpClient_->println("</h1>");
  pHttpClient_->println("<h2>Server Config</h2>");
  // Button to go back
  pHttpClient_->println("<div style='display: flex; gap: 16px; margin-top: 24px;'>");
  pHttpClient_->println("<form action=\"/\" method=\"get\" style=\"margin:0;\">");
  pHttpClient_->println("<button type=\"submit\">Back to Status</button>");
  pHttpClient_->println("</form>");
  pHttpClient_->println("<form action=\"/gpsconfig\" method=\"get\" style=\"margin:0;\">");
  pHttpClient_->println("<button type=\"submit\" name=\"action\" value=\"reload\">Reload Server Config</button>");
  pHttpClient_->println("</form>");
  pHttpClient_->println("</div>");
  //
  pHttpClient_->println("<pre>");
  pHttpClient_->print(*pConfig_);
  pHttpClient_->println("</pre>");
  pHttpClient_->println("</body></html>");
}

void TimeHttp::sendLogPage() {
  pHttpClient_->println("HTTP/1.1 200 OK");
  pHttpClient_->println("Content-Type: text/html");
  pHttpClient_->println("Connection: close");
  pHttpClient_->println();
  pHttpClient_->print("<!DOCTYPE html><html lang=\"en\"><head><title>"); pHttpClient_->print(pProperties_->getServerName()); pHttpClient_->println("</title>");

  pHttpClient_->println("<script>function addPass(form, hiddenId) {");
  pHttpClient_->println("const pass = prompt(\"Enter Passcode:\");");
  pHttpClient_->println("if (!pass) return false;");
  pHttpClient_->println("form.querySelector(\"#\" + hiddenId).value = pass;");
  pHttpClient_->println("return true;");
  pHttpClient_->println("}</script>");

  pHttpClient_->println("</head><body>");
  pHttpClient_->print("<h1>"); pHttpClient_->print(appName_); pHttpClient_->println("</h1>");
  pHttpClient_->println("<h2>Usage Logs</h2>");
  // Button to go back
  pHttpClient_->println("<div style='display: flex; gap: 16px; margin-top: 24px;'>");
  pHttpClient_->println("<form action=\"/\" method=\"get\" style=\"margin:0;\">");
  pHttpClient_->println("<button type=\"submit\">Back to Status</button>");
  pHttpClient_->println("</form>");
  pHttpClient_->println("<form action=\"/logs\" method=\"post\" style=\"margin:0;\">");
  pHttpClient_->println("<input type=\"hidden\" name=\"Passcode\" id=\"clear\">");
  pHttpClient_->println("<button name=\"action\" value=\"clear\" onclick=\"addPass(this.form, 'clear')\">Clear Usage Logs</button>");
  pHttpClient_->println("</form>");
  pHttpClient_->println("</div>");
  //
  pHttpClient_->println("<p>");
  for (const String& entry : *pUsageLog_) {
    pHttpClient_->println(entry);
    pHttpClient_->println("<br>");
  }
  pHttpClient_->println("</p>");
  pHttpClient_->println("</body></html>");
}

void TimeHttp::sendErrorPage() {
  pHttpClient_->println("HTTP/1.1 200 OK");
  pHttpClient_->println("Content-Type: text/html");
  pHttpClient_->println("Connection: close");
  pHttpClient_->println();
  pHttpClient_->print("<!DOCTYPE html><html lang=\"en\"><head><title>"); pHttpClient_->print(pProperties_->getServerName()); pHttpClient_->println("</title>");

  pHttpClient_->println("<script>function addPass(form, hiddenId) {");
  pHttpClient_->println("const pass = prompt(\"Enter Passcode:\");");
  pHttpClient_->println("if (!pass) return false;");
  pHttpClient_->println("form.querySelector(\"#\" + hiddenId).value = pass;");
  pHttpClient_->println("return true;");
  pHttpClient_->println("}</script>");

  pHttpClient_->println("</head><body>");  
  pHttpClient_->print("<h1>"); pHttpClient_->print(appName_); pHttpClient_->println("</h1>");
  pHttpClient_->println("<h2>Error Logs</h2>");
  // Button to go back
  pHttpClient_->println("<div style='display: flex; gap: 16px; margin-top: 24px;'>");
  pHttpClient_->println("<form action=\"/\" method=\"get\" style=\"margin:0;\">");
  pHttpClient_->println("<button type=\"submit\">Back to Status</button>");
  pHttpClient_->println("</form>");
  pHttpClient_->println("<form action=\"/errors\" method=\"post\" style=\"margin:0;\">");
  pHttpClient_->println("<input type=\"hidden\" name=\"Passcode\" id=\"clear\">");
  pHttpClient_->println("<button name=\"action\" value=\"clear\" onclick=\"addPass(this.form, 'clear')\">Clear Error Logs</button>");
  pHttpClient_->println("</form>");
  pHttpClient_->println("</div>");
  //
  pHttpClient_->println("<p>");
  for (const String& entry : *pErrorLog_) {
    pHttpClient_->println(entry);
    pHttpClient_->println("<br>");
  }
  pHttpClient_->println("</p>");
  pHttpClient_->println("</body></html>");
}

void TimeHttp::sendSetupPage(bool isSaved) {
  pHttpClient_->println("HTTP/1.1 200 OK");
  pHttpClient_->println("Content-Type: text/html");
  pHttpClient_->println("Connection: close");
  pHttpClient_->println();
  pHttpClient_->print("<!DOCTYPE html><html lang=\"en\"><head><title>"); pHttpClient_->print(pProperties_->getServerName()); pHttpClient_->println("</title><style>");
  pHttpClient_->println(".container{display:flex;gap:16px;margin-top:24px}");
  pHttpClient_->println(".section{margin-left:-344px}");
  pHttpClient_->println(".row{display:flex;align-items:center;margin-bottom:5px}");
  pHttpClient_->println("label{width:125px;text-align:left}");
  pHttpClient_->println(".w105{width:105px}");
  pHttpClient_->println(".w155{width:155px}");
  pHttpClient_->println(".w80{width:80px}");
  pHttpClient_->println(".mr3{margin-right:3px}</style>");

  pHttpClient_->println("<script>function addPass(form, hiddenId) {");
  pHttpClient_->println("const pass = prompt(\"Enter Passcode:\");");
  pHttpClient_->println("if (!pass) return false;");
  pHttpClient_->println("form.querySelector(\"#\" + hiddenId).value = pass;");
  pHttpClient_->println("return true;");
  pHttpClient_->println("}");
  pHttpClient_->println("async function uploadFirmware() {");
  pHttpClient_->println("const file = document.querySelector('#firmwareFile').files[0];");
  pHttpClient_->println("const button = document.querySelector('#firmwareUploadButton');");
  pHttpClient_->println("const status = document.querySelector('#firmwareStatus');");
  pHttpClient_->println("if (!file) { status.textContent = 'Select a .hex firmware file first.'; return; }");
  pHttpClient_->println("if (!/\\.hex$/i.test(file.name)) { status.textContent = 'Only a plain .hex file is supported.'; return; }");
  pHttpClient_->println("const pass = prompt('Enter Passcode:');");
  pHttpClient_->println("if (!pass) return;");
  pHttpClient_->println("if (!confirm('After validation, installation is not power-fail-safe. Do not remove power until the server has rebooted. Continue?')) return;");
  pHttpClient_->println("button.disabled = true; status.textContent = 'Uploading and validating firmware...';");
  pHttpClient_->println("try {");
  pHttpClient_->println("const response = await fetch('/firmware', {method:'POST', cache:'no-store', headers:{'Content-Type':'application/x-intel-hex','X-Firmware-Name':encodeURIComponent(file.name),'X-Firmware-Passcode':encodeURIComponent(pass)}, body:file});");
  pHttpClient_->println("const responseBody = await response.text();");
  pHttpClient_->println("if (response.ok) { document.open(); document.write(responseBody); document.close(); return; }");
  pHttpClient_->println("status.textContent = responseBody.trim(); status.style.color = 'red';");
  pHttpClient_->println("button.disabled = false;");
  pHttpClient_->println("} catch (error) { status.textContent = 'Upload failed: ' + error; status.style.color = 'red'; button.disabled = false; }");
  pHttpClient_->println("}</script>");

  pHttpClient_->println("</head><body>");
  pHttpClient_->print("<h1>"); pHttpClient_->print(appName_); pHttpClient_->println("</h1>");
  pHttpClient_->println("<h2>Application Setup</h2>");
  // Buttons and Forms
  pHttpClient_->println("<div class=\"container\"><form action=\"/\"><button>Back to Status</button></form>");
  pHttpClient_->println("<form action=\"/setup\" method=\"post\"><input type=\"hidden\" name=\"Passcode\" id=\"rebootPass\"><button name=\"action\" value=\"reboot\" onclick=\"addPass(this.form, 'rebootPass')\">Restart Server</button></form>");
  pHttpClient_->println("<form action=\"/setup\" method=\"post\"><input type=\"hidden\" name=\"Passcode\" id=\"resetPass\"><button style=\"color:red\" name=\"action\" value=\"reset\" onclick=\"addPass(this.form, 'resetPass')\">Reset Server</button></form>");
  pHttpClient_->println("<form action=\"/setup\" method=\"post\"><input type=\"hidden\" name=\"Passcode\" id=\"savePass\"><button name=\"action\" value=\"save\" onclick=\"addPass(this.form, 'savePass')\">Save Settings</button>");  
  pHttpClient_->println("<br><br><div class=\"section\"><div class=\"row\"><b>EEPROM State</b></div>");
  pHttpClient_->print("<div class=\"row\">EEPROM writes: "); pHttpClient_->print(pProperties_->getEepromWrites()); pHttpClient_->println("</div>");
  pHttpClient_->println("<br><div class=\"row\"><b>Server Info</b></div>");
  pHttpClient_->print("<div class=\"row\"><label for=\"serverName\">Server Name:</label><input id=\"serverName\" name=\"serverName\" value=\""); pHttpClient_->print(pProperties_->getServerName()); pHttpClient_->println("\" class=\"w155\"></div>");
  pHttpClient_->println("<div class=\"row\"><label for=\"newPasscode\">New Passcode:</label><input id=\"newPasscode\" name=\"newPasscode\" class=\"w155\"></div>");
  pHttpClient_->print("<br><div class=\"row\"><b>IP Config</b></div>");
  pHttpClient_->print("<div class=\"row\">Using DHCP: "); pHttpClient_->print(pProperties_->isDhcp() ? "true" : "false"); pHttpClient_->println("</div>");
  pHttpClient_->print("<div class=\"row\"><label for=\"localIp\">IP Address:</label><input id=\"localIp\" name=\"localIp\" value=\""); pHttpClient_->print(pProperties_->getLocalIpStr()); pHttpClient_->println("\" class=\"w105\"></div>");
  pHttpClient_->print("<div class=\"row\"><label for=\"subnet\">Subnet:</label><input id=\"subnet\" name=\"subnet\" value=\""); pHttpClient_->print(pProperties_->getSubnetStr()); pHttpClient_->println("\" class=\"w105\"></div>");
  pHttpClient_->print("<div class=\"row\"><label for=\"dns1Ip\">Primary DNS:</label><input id=\"dns1Ip\" name=\"dns1Ip\" value=\""); pHttpClient_->print(pProperties_->getDns1IpStr()); pHttpClient_->println("\" class=\"w105\"></div>");
  pHttpClient_->print("<div class=\"row\"><label for=\"dns2Ip\">Secondary DNS:</label><input id=\"dns2Ip\" name=\"dns2Ip\" value=\""); pHttpClient_->print(pProperties_->getDns2IpStr()); pHttpClient_->println("\" class=\"w105\"></div>");
  pHttpClient_->print("<div class=\"row\"><label for=\"gatewayIp\">Gateway:</label><input id=\"gatewayIp\" name=\"gatewayIp\" value=\""); pHttpClient_->print(pProperties_->getGatewayIpStr()); pHttpClient_->println("\" class=\"w105\"></div>");
  pHttpClient_->println("<br><div class=\"row\"><b>Logging</b></div>");
  pHttpClient_->print("<div class=\"row\"><label for=\"logMax\">Max Log Entries:</label><input type=\"number\" min=\"0\" id=\"logMax\" name=\"logMax\" value=\""); pHttpClient_->print(pProperties_->getLogMax()); pHttpClient_->println("\" class=\"w80\"></div>");
  pHttpClient_->print("<div class=\"row\"><label for=\"errorMax\">Max Error Entries:</label><input type=\"number\" min=\"0\" id=\"errorMax\" name=\"errorMax\" value=\""); pHttpClient_->print(pProperties_->getErrorMax()); pHttpClient_->println("\" class=\"w80\"></div>");
  pHttpClient_->println("<br><div class=\"row\"><b>Operation Frequency</b></div>");
  pHttpClient_->print("<div class=\"row\"><label for=\"refreshFrequencyMs\">Status Frequency:</label><input type=\"number\" min=\"0\" id=\"refreshFrequencyMs\" name=\"refreshFrequencyMs\" value=\""); pHttpClient_->print(pProperties_->getRefreshFrequency()); pHttpClient_->println("\" class=\"w80 mr3\"> ms</div>");
  pHttpClient_->print("<div class=\"row\"><label for=\"rtcSetFrequencyMs\">RTC-GPS Sync:</label><input type=\"number\" min=\"0\" id=\"rtcSetFrequencyMs\" name=\"rtcSetFrequencyMs\" value=\""); pHttpClient_->print(pProperties_->getRtcSetFrequency()); pHttpClient_->println("\" class=\"w80 mr3\"> ms</div>");
  pHttpClient_->print("<div class=\"row\"><label for=\"httpTimeoutMs\">HTTP Timeout:</label><input type=\"number\" min=\"0\" id=\"httpTimeoutMs\" name=\"httpTimeoutMs\" value=\""); pHttpClient_->print(pProperties_->getHttpTimeout()); pHttpClient_->println("\" class=\"w80 mr3\"> ms</div>");
  pHttpClient_->println("<br><div class=\"row\"><b>Display Settings</b></div>");
  pHttpClient_->print("<div class=\"row\">Display: ");
  pHttpClient_->print("<input style=\"margin-left:73px; margin-top:-2px;\" type=\"radio\" id=\"displayOn\" name=\"display\" value=\"1\""); if (pProperties_->getDisplayOn() == 1) pHttpClient_->print(" checked"); pHttpClient_->print("><label for=\"displayOn\">On</label>");
  pHttpClient_->print("<input style=\"margin-left:-85px; margin-top:-2px;\" type=\"radio\" id=\"displayOff\" name=\"display\" value=\"0\""); if (pProperties_->getDisplayOn() == 0) pHttpClient_->print(" checked"); pHttpClient_->print("><label for=\"displayOff\">Off</label></div>");
  pHttpClient_->print("<div class=\"row\">Alternate Display: ");
  pHttpClient_->print("<input style=\"margin-left:10px; margin-top:-2px;\" type=\"radio\" id=\"alternateOn\" name=\"alternate\" value=\"1\""); if (pProperties_->getDisplayAlternate() == 1) pHttpClient_->print(" checked"); pHttpClient_->print("><label for=\"alternateOn\">On</label>");
  pHttpClient_->print("<input style=\"margin-left:-85px; margin-top:-2px;\" type=\"radio\" id=\"alternateOff\" name=\"alternate\" value=\"0\""); if (pProperties_->getDisplayAlternate() == 0) pHttpClient_->print(" checked"); pHttpClient_->print("><label for=\"alternateOff\">Off</label></div>");
  pHttpClient_->println("</div></form></div>");
  pHttpClient_->println("<hr><section><h3>Firmware Update</h3>");
  pHttpClient_->println("<p>Upload a plain Teensy MicroMod <code>.hex</code> build. NTP service pauses during upload and the server restarts after validation.</p>");
  pHttpClient_->println("<p style=\"color:#b00020\"><b>Do not remove power during installation.</b> A power loss after validation can require USB and the DEV-20748 BOOT button for recovery.</p>");
  pHttpClient_->println("<input id=\"firmwareFile\" type=\"file\" accept=\".hex,application/x-intel-hex\">");
  pHttpClient_->println("<button id=\"firmwareUploadButton\" type=\"button\" onclick=\"uploadFirmware()\">Upload Firmware</button>");
  pHttpClient_->println("<output id=\"firmwareStatus\" style=\"display:block;margin-top:10px;white-space:pre-wrap\"></output></section>");
  if (isSaved) {
    pHttpClient_->println("<p style=\"font-size: x-large; color: green;\">Settings saved to EEPROM. Restart to take affect.</p>");
  }
  pHttpClient_->println("</body></html>");
}

void TimeHttp::sendPasscodeError(WebPage page) {
  String strPageName = "";
  String strPageUrl = "";
  switch (page) {
    case WebPage::HOME :
      strPageName = "Status";
      strPageUrl = "/";
      break;
    case WebPage::ERROR :
      strPageName = "Errors";
      strPageUrl = "/errors"; 
      break;
    case WebPage::LOG :
      strPageName = "Logs";
      strPageUrl = "/logs"; 
      break;
    case WebPage::SETUP :
      strPageName = "Setup";
      strPageUrl = "/setup"; 
      break;
    default :
      fptrAddError_("Web POST error.");
  }
  pHttpClient_->println("HTTP/1.1 200 OK");
  pHttpClient_->println("Content-Type: text/html");
  pHttpClient_->println("Connection: close");
  pHttpClient_->println();
  pHttpClient_->print("<!DOCTYPE html><html lang=\"en\"><head><title>"); pHttpClient_->print(pProperties_->getServerName()); pHttpClient_->println("</title></head><body>");
  pHttpClient_->print("<h1>"); pHttpClient_->print(appName_); pHttpClient_->println("</h1>");
  pHttpClient_->println("<h2 style=\"color: red;\">Passcode Error</h2>");
  // Button to go back
  pHttpClient_->println("<div style='display: flex; gap: 16px; margin-top: 24px;'>");
  pHttpClient_->print("<form action=\""); pHttpClient_->print(strPageUrl); pHttpClient_->println("\" method=\"get\" style=\"margin:0;\">");
  pHttpClient_->print("<button type=\"submit\">Back to "); pHttpClient_->print(strPageName); pHttpClient_->println("</button>");
  pHttpClient_->println("</form></div></body></html>");
}

void TimeHttp::sendHttpWait() {
  pHttpClient_->println("HTTP/1.1 200 OK");
  pHttpClient_->println("Content-Type: text/html");
  pHttpClient_->println("Connection: close");
  pHttpClient_->println();
  pHttpClient_->println("");       
  pHttpClient_->print("<!DOCTYPE html><html lang=\"en\"><head><title>"); pHttpClient_->print(pProperties_->getServerName()); pHttpClient_->println("</title></head><body>");
  pHttpClient_->print("<h1>"); pHttpClient_->print(appName_); pHttpClient_->println("</h1>");
  pHttpClient_->println("<h2 style=\"color: red;\">Please wait for reboot...</h2>");
  // Button to go back
  pHttpClient_->println("<div style='display: flex; gap: 16px; margin-top: 24px;'>");
  pHttpClient_->println("<form action=\"/\" method=\"get\" style=\"margin:0;\">");
  pHttpClient_->println("<button type=\"submit\">Back to Status</button></form>");
  pHttpClient_->println("</div></body></html>");                 // end of headers (blank line)
}
