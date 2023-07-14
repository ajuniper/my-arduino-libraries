#include <Arduino.h>
#ifdef ESP8266
#include <ESPAsyncTCP.h>
#else
#include <AsyncTCP.h>
#endif
#include "mywebserver.h"
AsyncWebServer server(80);

static const char * default_page = NULL;
static void serve_root_get(AsyncWebServerRequest *request) {
  AsyncWebServerResponse *response = request->beginResponse(302, "text/plain", "OK");
  response->addHeader("Location",default_page);
  request->send(response);
}

void WS_init(const char * d) {
    // Route for root / web page
    default_page = d;
    server.on("/", HTTP_GET, serve_root_get);
    server.begin();
}
