// common wifi stuff
#include <Arduino.h>
#include <my_secrets.h>
#include <mywifi.h>
#include <mysyslog.h>
#include <myconfig.h>
#include <Ticker.h>

#ifndef ESP8266
// ESP32 does not have a separate scheduler queue
#define once_ms_scheduled once_ms
#endif

/*
Config nodes:
        wifi.hostname
        wifi.ssid
        wifi.password
*/

static int disconnecttime = 0;
static bool going_for_reboot = false;

// called from ticker handler and not interrupt thread
static void wifi_connected() {
    if (disconnecttime != 0) {
        syslogf("Wifi connected again after %d seconds", time(NULL) - disconnecttime);
        disconnecttime = 0;
    } else {
        // at boot there will have been no prior disconnect
        // Init and get the time
        Serial.println(WiFi.localIP());
        syslogf(LOG_DAEMON | LOG_WARNING, "started");
    }
}
static void wifi_disconnected() {
    // do not try reconnecting if rebooting
    if (going_for_reboot == true) { return; }

    Serial.println("WiFi lost connection");
    if (disconnecttime == 0) {
        disconnecttime = time(NULL);
    }
    WiFi.reconnect();
}

// must use tickers because not allowed to do anything in callback
Ticker wifi_connected_ticker;
Ticker wifi_disconnected_ticker;

#ifdef ESP8266
WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;
#else

#endif

#ifdef ESP8266
static void WiFiStationDisconnected(const WiFiEventStationModeDisconnected& event){
#else
static void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info){
#endif
    wifi_disconnected_ticker.once_ms_scheduled(0,wifi_disconnected);
}

#ifdef ESP8266
static void WiFiGotIP(const WiFiEventStationModeGotIP& event){
#else
static void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info){
#endif
    wifi_connected_ticker.once_ms_scheduled(0,wifi_connected);
}

static const char * handleConfig(const char * name, const String & id, String &value) {
    // TODO reconfigure with new values
    if (id == "hostname") {
        // all ok, save the value
        return NULL;
    } else if (id == "ssid") {
        // all ok, save the value
        return NULL;
    } else if (id == "password") {
        // all ok, save the value
        return NULL;
    } else {
        return "config type not recognised";
    }
}

void WIFI_init(const char * hostname, bool wait_for_wifi) {
    String h = MyCfgGetString("wifi","hostname",String(hostname?hostname:""));
    if (h.length() > 0) {
        WiFi.setHostname(h.c_str());
    }
    String ssid = MyCfgGetString("wifi","ssid",MY_WIFI_SSID);
    String password = MyCfgGetString("wifi","password",MY_WIFI_PASSWORD);
    WiFi.begin(ssid.c_str(), password.c_str());
#ifdef ESP8266
    wifiConnectHandler = WiFi.onStationModeGotIP(WiFiGotIP);
    wifiDisconnectHandler = WiFi.onStationModeDisconnected(WiFiStationDisconnected);
#else
    WiFi.onEvent(WiFiStationDisconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    WiFi.onEvent(WiFiGotIP, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
#endif
    MyCfgRegisterString("wifi",&handleConfig);
    if (wait_for_wifi) {
        Serial.print("Connecting");
        while (WiFi.status() != WL_CONNECTED) {
            delay(500);
            Serial.print(".");
        }
    }
    Serial.println(WiFi.localIP());
}

void WIFI_going_for_reboot() {
    going_for_reboot = true;
}
