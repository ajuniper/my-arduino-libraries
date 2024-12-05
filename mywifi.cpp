// common wifi stuff
#include <Arduino.h>
#include <my_secrets.h>
#include <mywifi.h>
#include <mysyslog.h>
#include <myconfig.h>
#include <Ticker.h>

/*
Config nodes:
        wifi.hostname
        wifi.ssid
        wifi.password
*/

#ifndef ESP8266
// ESP32 does not have a separate scheduler queue
#define once_ms_scheduled once_ms
#endif

// https://www.esp32.com/viewtopic.php?f=19&t=18979&sid=d768b1ce7fcbc02976e94a404c4c5e9f&start=10
bool scanAndConnectToStrongestNetwork() {
    int i_strongest = -1;
    int32_t rssi_strongest = -100;
    String wifi_ssid = MyCfgGetString("wifi","ssid",MY_WIFI_SSID);
    String wifi_pass = MyCfgGetString("wifi","password",MY_WIFI_PASSWORD);
    Serial.printf("Start scanning for SSID %s\r\n", wifi_ssid);

    // TODO what is required here?
    WiFi.mode(WIFI_STA);
    //delay(1000);

    // WiFi.scanNetworks will return the number of networks found
    // -1 = already in progress / otherwise busy
    // -2 = scan not started / scan failed
    int n = WiFi.scanNetworks();
    Serial.println("Scan done.");

    if (n == 0) {
        Serial.println("No networks found!");
        return false;
    }

    Serial.printf("%d networks found:\r\n", n);
    for (int i = 0; i < n; ++i) {
        // Print SSID and RSSI for each network found
        Serial.printf("%d: BSSID: %s  %2ddBm, %3d%%  %9s  %s\r\n", i, WiFi.BSSIDstr(i).c_str(), WiFi.RSSI(i), constrain(2 * (WiFi.RSSI(i) + 100), 0, 100), (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "open" : "encrypted", WiFi.SSID(i).c_str());
        if ((String(wifi_ssid) == String(WiFi.SSID(i)) && (WiFi.RSSI(i)) > rssi_strongest)) {
            rssi_strongest = WiFi.RSSI(i);
            i_strongest = i;
        }
    }

    if (i_strongest < 0) {
        Serial.printf("No network with SSID %s found!\r\n", wifi_ssid);
#if 0
        WiFi.scanDelete();
        return false;
#else
        // connect to whatever we can find
#endif
    }

    Serial.printf("SSID match found at %d. Connecting...\r\n", i_strongest);
    WiFi.begin(wifi_ssid, wifi_pass, 0, (i_strongest<0)?NULL:WiFi.BSSID(i_strongest));
    WiFi.scanDelete();
    return true;
}

// must use tickers because not allowed to do anything in callback
Ticker wifi_connected_ticker;
Ticker wifi_disconnected_ticker;
Ticker wifi_gotip_ticker;

#ifdef ESP8266
WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;
WiFiEventHandler wifiGotIpHandler;
#else

#endif

static int disconnecttime = 0;
static bool going_for_reboot = false;
static bool wifiColdBoot = true;

// called from ticker handler and not interrupt thread
static void wifi_connected() {
    if (disconnecttime != 0) {
        syslogf("Wifi connected again after %d seconds", time(NULL) - disconnecttime);
        disconnecttime = 0;
    }
}
// called from ticker handler and not interrupt thread
static void wifi_gotip() {
    // at boot there will have been no prior disconnect
    // Init and get the time
    Serial.println(WiFi.localIP());
    if (wifiColdBoot) {
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
#if 0
    // TODO fix this when understand correct sequence
    // always ends up returning -2 from scan request
    WiFi.disconnect(false);
    delay(1000);
    WiFi.begin("lolllool","loooooooool");
    //WiFi.mode(WIFI_OFF);
    delay(1000);
    if (!scanAndConnectToStrongestNetwork()) {
        // didn't find anything to connect to, try again in a second
        wifi_disconnected_ticker.once_ms_scheduled(1000,wifi_disconnected);
    }
#else
    WiFi.reconnect();
#endif
}

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
    wifi_connected_ticker.once_ms_scheduled(0,wifi_gotip);
}

#ifdef ESP8266
static void WiFiConnected(const WiFiEventStationModeGotIP& event){
#else
static void WiFiConnected(WiFiEvent_t event, WiFiEventInfo_t info){
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

void WIFI_init(const char * hostname, bool wait_for_wifi, bool isColdBoot) {
    wifiColdBoot = isColdBoot;
    String h = MyCfgGetString("wifi","hostname",String(hostname?hostname:""));
    if (h.length() > 0) {
        WiFi.setHostname(h.c_str());
    }
    disconnecttime = time(NULL);
    if (!scanAndConnectToStrongestNetwork()) {
        // didn't find anything to connect to, try again in a second
        wifi_disconnected_ticker.once_ms_scheduled(1000,wifi_disconnected);
    }
#ifdef ESP8266
    wifiGotIpHandler = WiFi.onStationModeConnected(WiFiGotIP);
    wifiConnectHandler = WiFi.onStationModeGotIP(WiFiConnected);
    wifiDisconnectHandler = WiFi.onStationModeDisconnected(WiFiStationDisconnected);
#else
    WiFi.onEvent(WiFiStationDisconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    WiFi.onEvent(WiFiGotIP, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
    WiFi.onEvent(WiFiConnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED);
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
