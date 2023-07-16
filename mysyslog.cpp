// syslog stuff
#include <Arduino.h>
#include "mysyslog.h"
#include <my_secrets.h>
#include <WiFiUdp.h>
static WiFiUDP udpClient;
extern const char * syslog_name;
Syslog syslog(udpClient, MY_SYSLOG_SERVER, 514, syslog_name, syslog_name, LOG_DAEMON);
