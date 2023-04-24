// syslog stuff
#include "mysyslog.h"
#include <my_secrets.h>
static WiFiUDP udpClient;
Syslog syslog(udpClient, MY_SYSLOG_SERVER, 514, "templog", "templog", LOG_DAEMON);
