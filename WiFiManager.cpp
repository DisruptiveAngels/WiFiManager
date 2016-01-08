/**************************************************************
   WiFiManager is a library for the ESP8266/Arduino platform
   (https://github.com/esp8266/Arduino) to enable easy
   configuration and reconfiguration of WiFi credentials and
   store them in EEPROM.
   inspired by:
   http://www.esp8266.com/viewtopic.php?f=29&t=2520
   https://github.com/chriscook8/esp-arduino-apboot
   https://github.com/esp8266/Arduino/tree/esp8266/hardware/esp8266com/esp8266/libraries/DNSServer/examples/CaptivePortalAdvanced
   Built by AlexT https://github.com/tzapu
   Licensed under MIT license
 **************************************************************/

#include "WiFiManager.h"

WiFiManagerParameter::WiFiManagerParameter(const char *id, const char *placeholder, const char *defaultValue, int length) {
  _id = id;
  _placeholder = placeholder;
  _length = length;
  _value = new char[length];
  for (int i = 0; i < length; i++) {
    _value[i] = 0;
  }
  if (defaultValue != NULL) {
    strncpy(_value, defaultValue, length);
  }
}

const char* WiFiManagerParameter::getValue() {
  return _value;
}
const char* WiFiManagerParameter::getID() {
  return _id;
}
const char* WiFiManagerParameter::getPlaceholder() {
  return _placeholder;
}
int WiFiManagerParameter::getValueLength() {
  return _length;
}

WiFiManager::WiFiManager() {
}

void WiFiManager::addParameter(WiFiManagerParameter *p) {
  _params[_paramsCount] = p;
  _paramsCount++;
  DEBUG_WM("Adding parameter");
  DEBUG_WM(p->getID());
}


void WiFiManager::begin() {
  begin("NoNetESP");
}

void WiFiManager::begin(char const *apName) {
  begin(apName, NULL);
}

void WiFiManager::begin(char const *apName, char const *apPasswd) {
  dnsServer.reset(new DNSServer());
  server.reset(new ESP8266WebServer(80));

  DEBUG_WM(F(""));
  _apName = apName;
  _apPasswd = apPasswd;
  start = millis();

  DEBUG_WM(F("Configuring access point... "));
  DEBUG_WM(_apName);
  if (_apPasswd != NULL) {
    if (strlen(_apPasswd) < 8 || strlen(_apPasswd) > 63) {
      // fail passphrase to short or long!
      DEBUG_WM(F("Invalid AccessPoint password"));
    }
    DEBUG_WM(_apPasswd);
  }

  //optional soft ip config
  if (_ip) {
    DEBUG_WM(F("Custom IP/GW/Subnet"));
    WiFi.softAPConfig(_ip, _gw, _sn);
  }

  if (_apPasswd != NULL) {
    WiFi.softAP(_apName, _apPasswd);//password option
  } else {
    WiFi.softAP(_apName);
  }

  delay(500); // Without delay I've seen the IP address blank
  DEBUG_WM(F("AP IP address: "));
  DEBUG_WM(WiFi.softAPIP());

  /* Setup the DNS server redirecting all the domains to the apIP */
  dnsServer->setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer->start(DNS_PORT, "*", WiFi.softAPIP());

  /* Setup web pages: root, wifi config pages, SO captive portal detectors and not found. */
  server->on("/", std::bind(&WiFiManager::handleRoot, this));
  server->on("/wifi", std::bind(&WiFiManager::handleWifi, this, true));
  server->on("/0wifi", std::bind(&WiFiManager::handleWifi, this, false));
  server->on("/wifisave", std::bind(&WiFiManager::handleWifiSave, this));
  server->on("/generate_204", std::bind(&WiFiManager::handle204, this));  //Android/Chrome OS captive portal check.
  server->on("/fwlink", std::bind(&WiFiManager::handleRoot, this));  //Microsoft captive portal. Maybe not needed. Might be handled by notFound handler.
  server->onNotFound (std::bind(&WiFiManager::handleNotFound, this));
  server->begin(); // Web server start
  DEBUG_WM(F("HTTP server started"));
}

boolean WiFiManager::autoConnect() {
  String ssid = "ESP" + String(ESP.getChipId());
  return autoConnect(ssid.c_str(), NULL);
}

boolean WiFiManager::autoConnect(char const *apName) {
  return autoConnect(apName, NULL);
}

boolean WiFiManager::autoConnect(char const *apName, char const *apPasswd) {
  DEBUG_WM(F(""));
  DEBUG_WM(F("AutoConnect"));

  // read eeprom for ssid and pass
  String ssid = getSSID();
  String pass = getPassword();
  //use SDK functions to get SSID and pass
  //String ssid = WiFi.SSID();
  //String pass = WiFi.psk();

  WiFi.mode(WIFI_STA);
  connectWifi(ssid, pass);
  int s = WiFi.status();
  if (s == WL_CONNECTED) {
    DEBUG_WM(F("IP Address:"));
    DEBUG_WM(WiFi.localIP());
    //connected
    return true;
  }

  //not connected
  //setup AP
  WiFi.mode(WIFI_AP);
  //notify we entered AP mode
  if ( _apcallback != NULL) {
    _apcallback();
  }
  connect = false;
  begin(apName, apPasswd);

  bool looping = true;
  while (timeout == 0 || millis() < start + timeout) {
    //DNS
    dnsServer->processNextRequest();
    //HTTP
    server->handleClient();


    if (connect) {
      delay(2000);
      DEBUG_WM(F("Connecting to new AP"));
      connect = false;
      //ssid = getSSID();
      //pass = getPassword();
      connectWifi(_ssid, _pass);
      int s = WiFi.status();
      if (s != WL_CONNECTED) {
        DEBUG_WM(F("Failed to connect."));
        //not connected, should retry everything
        //ESP.reset();
        //delay(1000);
        //return false;
      } else {
        //connected
        WiFi.mode(WIFI_STA);
        //notify that configuration has changed and any optional parameters should be saved
        if ( _savecallback != NULL) {
          //todo: check if any custom parameters actually exist, and check if they really changed maybe
          _savecallback();
        }
        break;
      }

    }
    yield();
  }

  server.reset();
  dnsServer.reset();
  return  WiFi.status() == WL_CONNECTED;
}


void WiFiManager::connectWifi(String ssid, String pass) {
  DEBUG_WM(F("Connecting as wifi client..."));
  //WiFi.disconnect();
  WiFi.begin(ssid.c_str(), pass.c_str());
  int connRes = WiFi.waitForConnectResult();
  DEBUG_WM ("Connection result: ");
  DEBUG_WM ( connRes );
}


String WiFiManager::getSSID() {
  if (_ssid == "") {
    DEBUG_WM(F("Reading SSID"));
    _ssid = WiFi.SSID();//getEEPROMString(0, 32);
    DEBUG_WM(F("SSID: "));
    DEBUG_WM(_ssid);
  }
  return _ssid;
}

String WiFiManager::getPassword() {
  if (_pass == "") {
    DEBUG_WM(F("Reading Password"));
    _pass = WiFi.psk();//getEEPROMString(32, 64);
    DEBUG_WM("Password: " + _pass);
    //DEBUG_WM(_pass);
  }
  return _pass;
}

/*
  String WiFiManager::getEEPROMString(int start, int len) {
  EEPROM.begin(512);
  delay(10);
  String string = "";
  for (int i = _eepromStart + start; i < _eepromStart + start + len; i++) {
    //DEBUG_WM(i);
    string += char(EEPROM.read(i));
  }
  EEPROM.end();
  return string;
  }
*/
/*
  void WiFiManager::setEEPROMString(int start, int len, String string) {
  EEPROM.begin(512);
  delay(10);
  int si = 0;
  for (int i = _eepromStart + start; i < _eepromStart + start + len; i++) {
    char c;
    if (si < string.length()) {
      c = string[si];
      //DEBUG_WM(F("Wrote: ");
      //DEBUG_WM(c);
    } else {
      c = 0;
    }
    EEPROM.write(i, c);
    si++;
  }
  EEPROM.end();
  DEBUG_WM(F("Wrote " + string);
  }*/

String WiFiManager::urldecode(const char *src)
{
  String decoded = "";
  char a, b;
  while (*src) {
    if ((*src == '%') &&
        ((a = src[1]) && (b = src[2])) &&
        (isxdigit(a) && isxdigit(b))) {
      if (a >= 'a')
        a -= 'a' - 'A';
      if (a >= 'A')
        a -= ('A' - 10);
      else
        a -= '0';
      if (b >= 'a')
        b -= 'a' - 'A';
      if (b >= 'A')
        b -= ('A' - 10);
      else
        b -= '0';

      decoded += char(16 * a + b);
      src += 3;
    } else if (*src == '+') {
      decoded += ' ';
      *src++;
    } else {
      decoded += *src;
      *src++;
    }
  }
  decoded += '\0';

  return decoded;
}

void WiFiManager::resetSettings() {
  DEBUG_WM(F("settings invalidated"));
  DEBUG_WM(F("THIS MAY CAUSE AP NOT TO STRT UP PROPERLY. YOU NEED TO COMMENT IT OUT AFTER ERASING THE DATA."));
  WiFi.disconnect(true);
  //delay(200);
}

void WiFiManager::setTimeout(unsigned long seconds) {
  timeout = seconds * 1000;
}

void WiFiManager::setDebugOutput(boolean debug) {
  _debug = debug;
}

void WiFiManager::setAPConfig(IPAddress ip, IPAddress gw, IPAddress sn) {
  _ip = ip;
  _gw = gw;
  _sn = sn;
}


/** Handle root or redirect to captive portal */
void WiFiManager::handleRoot() {
  DEBUG_WM(F("Handle root"));
  if (captivePortal()) { // If caprive portal redirect instead of displaying the page.
    return;
  }

  server->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server->sendHeader("Pragma", "no-cache");
  server->sendHeader("Expires", "-1");
  server->send(200, "text/html", ""); // Empty content inhibits Content-length header so we have to close the socket ourselves.

  String head = HTTP_HEAD;
  head.replace("{v}", "Options");
  server->sendContent(head);
  server->sendContent_P(HTTP_SCRIPT);
  server->sendContent_P(HTTP_STYLE);
  server->sendContent_P(HTTP_HEAD_END);

  //server->sendContent(F("<h1>"));
  String title = "<h1>";
  title += _apName;
  title += "</h1>";
  server->sendContent(title);
  server->sendContent(F("<h3>WiFiManager</h3>"));


  server->sendContent_P(HTTP_PORTAL_OPTIONS);
  server->sendContent_P(HTTP_END);

  server->client().stop(); // Stop is needed because we sent no content length
}

/** Wifi config page handler */
void WiFiManager::handleWifi(bool scan) {
  server->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server->sendHeader("Pragma", "no-cache");
  server->sendHeader("Expires", "-1");
  server->send(200, "text/html", ""); // Empty content inhibits Content-length header so we have to close the socket ourselves.


  String head = HTTP_HEAD;
  head.replace("{v}", "Config ESP");
  server->sendContent(head);
  server->sendContent_P(HTTP_SCRIPT);
  server->sendContent_P(HTTP_STYLE);
  server->sendContent_P(HTTP_HEAD_END);

  if (scan) {
    int n = WiFi.scanNetworks();
    DEBUG_WM(F("Scan done"));
    if (n == 0) {
      DEBUG_WM(F("No networks found"));
      server->sendContent("No networks found. Refresh to scan again.");
    }
    else {
      for (int i = 0; i < n; ++i)
      {
        DEBUG_WM(WiFi.SSID(i));
        DEBUG_WM(WiFi.RSSI(i));
        String item = FPSTR(HTTP_ITEM);
        String rssiQ;
        rssiQ += getRSSIasQuality(WiFi.RSSI(i));
        item.replace("{v}", WiFi.SSID(i));
        item.replace("{r}", rssiQ);
        if (WiFi.encryptionType(i) != ENC_TYPE_NONE) {
          item.replace("{i}", FPSTR(HTTP_ITEM_PADLOCK));
        } else {
          item.replace("{i}", "");
        }
        //DEBUG_WM(item);
        server->sendContent(item);
        delay(0);
      }
      server->sendContent("<br/>");
    }
  }

  server->sendContent_P(HTTP_FORM_START);
  char parLength[2];
  // add the extra parameters to the form
  for (int i = 0; i < _paramsCount; i++) {
    if (_params[i] == NULL) {
      break;
    }

    String pitem = FPSTR(HTTP_FORM_PARAM);
    pitem.replace("{i}", _params[i]->getID());
    pitem.replace("{n}", _params[i]->getID());
    pitem.replace("{p}", _params[i]->getPlaceholder());
    snprintf(parLength, 2, "%d", _params[i]->getValueLength());
    pitem.replace("{l}", parLength);
    pitem.replace("{v}", _params[i]->getValue());

    server->sendContent(pitem);
  }
  if (_params[0] != NULL) {
    server->sendContent_P("<br/>");
  }

  server->sendContent_P(HTTP_FORM_END);
  server->sendContent_P(HTTP_END);
  server->client().stop();

  DEBUG_WM(F("Sent config page"));
}

/** Handle the WLAN save form and redirect to WLAN config page again */
void WiFiManager::handleWifiSave() {
  DEBUG_WM(F("WiFi save"));

  //SAVE/connect here
  _ssid = urldecode(server->arg("s").c_str());
  _pass = urldecode(server->arg("p").c_str());

  //parameters
  for (int i = 0; i < _paramsCount; i++) {
    if (_params[i] == NULL) {
      break;
    }
    //read parameter
    String value = urldecode(server->arg(_params[i]->getID()).c_str());
    //store it in array
    value.toCharArray(_params[i]->_value, _params[i]->_length);
    DEBUG_WM(F("Parameter"));
    DEBUG_WM(_params[i]->getID());
    DEBUG_WM(value);
  }

  server->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server->sendHeader("Pragma", "no-cache");
  server->sendHeader("Expires", "-1");
  server->send(200, "text/html", ""); // Empty content inhibits Content-length header so we have to close the socket ourselves.

  String head = HTTP_HEAD;
  head.replace("{v}", "Credentials Saved");
  server->sendContent(head);
  server->sendContent_P(HTTP_SCRIPT);
  server->sendContent_P(HTTP_STYLE);
  server->sendContent_P(HTTP_HEAD_END);

  server->sendContent_P(HTTP_SAVED);

  server->sendContent_P(HTTP_END);
  server->client().stop();

  DEBUG_WM(F("Sent wifi save page"));

  connect = true; //signal ready to connect/reset
}

void WiFiManager::handle204() {
  DEBUG_WM(F("204 No Response"));
  server->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server->sendHeader("Pragma", "no-cache");
  server->sendHeader("Expires", "-1");
  server->send ( 204, "text/plain", "");
}

void WiFiManager::handleNotFound() {
  if (captivePortal()) { // If captive portal redirect instead of displaying the error page.
    return;
  }
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server->uri();
  message += "\nMethod: ";
  message += ( server->method() == HTTP_GET ) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server->args();
  message += "\n";

  for ( uint8_t i = 0; i < server->args(); i++ ) {
    message += " " + server->argName ( i ) + ": " + server->arg ( i ) + "\n";
  }
  server->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server->sendHeader("Pragma", "no-cache");
  server->sendHeader("Expires", "-1");
  server->send ( 404, "text/plain", message );
}


/** Redirect to captive portal if we got a request for another domain. Return true in that case so the page handler do not try to handle the request again. */
boolean WiFiManager::captivePortal() {
  if (!isIp(server->hostHeader()) ) {
    DEBUG_WM(F("Request redirected to captive portal"));
    server->sendHeader("Location", String("http://") + toStringIp(server->client().localIP()), true);
    server->send ( 302, "text/plain", ""); // Empty content inhibits Content-length header so we have to close the socket ourselves.
    server->client().stop(); // Stop is needed because we sent no content length
    return true;
  }
  return false;
}

//start up config portal callback
void WiFiManager::setAPCallback( void (*func)(void) ) {
  _apcallback = func;
}

//start up save config callback
void WiFiManager::setSaveConfigCallback( void (*func)(void) ) {
  _savecallback = func;
}



template <typename Generic>
void WiFiManager::DEBUG_WM(Generic text) {
  if (_debug) {
    Serial.print("*WM: ");
    Serial.println(text);
  }
}


int WiFiManager::getRSSIasQuality(int RSSI) {
  int quality = 0;

  if (RSSI <= -100) {
    quality = 0;
  } else if (RSSI >= -50) {
    quality = 100;
  } else {
    quality = 2 * (RSSI + 100);
  }
  return quality;
}

/** Is this an IP? */
boolean WiFiManager::isIp(String str) {
  for (int i = 0; i < str.length(); i++) {
    int c = str.charAt(i);
    if (c != '.' && (c < '0' || c > '9')) {
      return false;
    }
  }
  return true;
}

/** IP to String? */
String WiFiManager::toStringIp(IPAddress ip) {
  String res = "";
  for (int i = 0; i < 3; i++) {
    res += String((ip >> (8 * i)) & 0xFF) + ".";
  }
  res += String(((ip >> 8 * 3)) & 0xFF);
  return res;
}


