// wifi stuff
#ifdef ESP8266
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif
extern void WIFI_init(const char * hostname = NULL, bool wait_for_wifi = false);
