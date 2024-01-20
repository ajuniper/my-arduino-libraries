#include <ESPAsyncWebServer.h>
extern void TR_init(AsyncWebServer & server, bool run_in_loop=false, bool isColdBoot=true);
extern float TR_get(const String & name);
// call this to report samples, do not call before return value
time_t TR_report_data();
