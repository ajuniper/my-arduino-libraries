// common wifi stuff
#include <Arduino.h>
#include <my_secrets.h>
#include <mywifi.h>
#include <mysyslog.h>

#ifdef ESP8266
WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;
#else

#endif

static const char* ssid     = MY_WIFI_SSID;
static const char* password = MY_WIFI_PASSWORD;

static int disconnecttime = 0;

#ifdef ESP8266
static void WiFiStationDisconnected(const WiFiEventStationModeDisconnected& event){
    Serial.println("WiFi lost connection");
#else
static void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info){
    Serial.print("WiFi lost connection. Reason: ");
    Serial.println(info.wifi_sta_disconnected.reason);
#endif
    if (disconnecttime == 0) {
        disconnecttime = time(NULL);
    }
    WiFi.reconnect();
}

#ifdef ESP8266
static void WiFiStationConnected(const WiFiEventStationModeGotIP& event){
#else
static void WiFiStationConnected(WiFiEvent_t event, WiFiEventInfo_t info){
#endif
    if (disconnecttime != 0) {
        syslog.logf("Wifi connected again after %d seconds", time(NULL) - disconnecttime);
        disconnecttime = 0;
    }
}

void WIFI_init() {
    Serial.print("Connecting");
    WiFi.begin(ssid, password);
#ifdef ESP8266
    wifiConnectHandler = WiFi.onStationModeGotIP(WiFiStationConnected);
    wifiDisconnectHandler = WiFi.onStationModeDisconnected(WiFiStationDisconnected);
#else
    WiFi.onEvent(WiFiStationDisconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    WiFi.onEvent(WiFiStationConnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED);
#endif
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("WiFi connected.");
    Serial.println(WiFi.localIP());

    // Init and get the time
    configTime(0, 0, MY_NTP_SERVER1, MY_NTP_SERVER2, MY_NTP_SERVER3);
    setenv("TZ", MY_TIMEZONE, 1);
    tzset();
}
