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

#include <list>
#include <Ethernet.h>
#include "Properties.h"

// Converts a single hexadecimal digit character into its numeric value for HTML escape character conversion.
static inline int8_t hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  return -1;
}

// Struct for getting values from HTML body
typedef struct {
  bool found;
  int endIndex;
  String value;
} HtmlBodyValue_t;

// enum to easily select the webpage we want to display
enum class WebPage { HOME, CONFIG, ERROR, LOG, SETUP };

class TimeHttp {
public:
  TimeHttp();
  ~TimeHttp();

  void setHttpClient(EthernetClient* client);
  void setAppName(const String appName);
  void setProperties(Properties* properties);
  void setLocalIp(String localIp);
  void setGpsFixType(String gpsFixType);
  void setGpsISO8601Time(String gpsISO8601Time);
  void setRtcISO8601Time(String rtcISO8601Time);
  void setConfigString(String* config);
  void setConfigFunction(void (*fptrGetGpsConfig)());
  void setUpdateRtcFunction(void (*fptrUpdateRtc)());
  void setAddErrorFunction(void (*fptrAddError)(String error));
  void setAddLogFunction(void (*fptrAddLog)(String log));
  void setLogArray(std::list<String>* usageLog);
  void setErrorArray(std::list<String>* errorLog);
  EthernetClient* getHttpClient();
  const String getAppName();
  Properties* getProperties();
  String getLocalIp();
  String getGpsFixType();
  String getGpsISO8601Time();
  String getRtcISO8601Time();
  String* getConfigString();
  std::list<String>* getLogArray();
  std::list<String>* getErrorArray();

  bool processRequest();
  bool processRequest(EthernetClient* client, String localIp, String gpsFixType, String gpsISO8601Time, String rtcISO8601Time);
 
private:
  EthernetClient* pHttpClient_ = nullptr;
  Properties* pProperties_ = nullptr;
  String appName_ = "";
  String pageTitle_ = "";
  String localIp_ = "";
  String gpsFixType_ = "";
  String gpsISO8601Time_ = "";
  String rtcISO8601Time_ = "";
  String* pConfig_ = nullptr;
  std::list<String>* pUsageLog_ = nullptr;
  std::list<String>* pErrorLog_ = nullptr;
  // Get GPS config function pointer
  void (*fptrGetGpsConfig_)();
  // Get update RTC function pointer
  void (*fptrUpdateRtc_)();
  // Get addLog function pointer
  void (*fptrAddLog_)(String log);
  // Get addError function pointer
  void (*fptrAddError_)(String error);

  String percentDecode(const String& strHtml, bool decodePlus = false);
  HtmlBodyValue_t getValueFromBody(String key, String& body, int startIndex);
  void sendHomePage();
  void sendHomePage(const String appName, Properties* properties, String localIp, String gpsISO8601Time, String rtcISO8601Time);
  void sendConfigPage();
  void sendConfigPage(String* config);
  void sendLogPage();
  void sendErrorPage();
  void sendSetupPage(bool isSaved);
  void sendPasscodeError(WebPage page);
  void sendHttpWait();
  void restartTeensy();
};