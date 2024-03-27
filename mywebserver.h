#include <ESPAsyncWebServer.h>
extern AsyncWebServer server;
extern void WS_init(const char * default_page);

// create redirect back to root page
extern AsyncWebServerResponse * redirect_to_root(AsyncWebServerRequest * request);
