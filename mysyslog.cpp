// syslog stuff
#include <Arduino.h>
#include "mysyslog.h"
#include <my_secrets.h>
#include <WiFiUdp.h>
static WiFiUDP udpClient;
Syslog syslog(udpClient, MY_SYSLOG_SERVER, 514, "templog", "templog", LOG_DAEMON);
