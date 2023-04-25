// common wifi stuff
#include <Arduino.h>
#include <WiFi.h>
#include <my_secrets.h>
#include <mywifi.h>
#include <mysyslog.h>
static const char* ssid     = MY_WIFI_SSID;
static const char* password = MY_WIFI_PASSWORD;

static int disconnecttime = 0;

static void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info){
    Serial.print("WiFi lost connection. Reason: ");
    Serial.println(info.wifi_sta_disconnected.reason);
    if (disconnecttime == 0) {
        disconnecttime = time(NULL);
    }
    WiFi.reconnect();
}

static void WiFiStationConnected(WiFiEvent_t event, WiFiEventInfo_t info){
    if (disconnecttime != 0) {
        syslog.logf("Wifi connected again after %d seconds", time(NULL) - disconnecttime);
        disconnecttime = 0;
    }
}

void WIFI_init() {
    Serial.print("Connecting");
    WiFi.begin(ssid, password);
    WiFi.onEvent(WiFiStationDisconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    WiFi.onEvent(WiFiStationConnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("WiFi connected.");
    Serial.println(WiFi.localIP());

    // Init and get the time
    setenv("TZ", MY_TIMEZONE, 1);
    configTime(0, 0, MY_NTP_SERVER1, MY_NTP_SERVER2, MY_NTP_SERVER3);
}
