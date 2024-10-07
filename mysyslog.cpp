// syslog stuff
#include <Arduino.h>
#include "mysyslog.h"
#include <my_secrets.h>
#include <WiFiUdp.h>
static WiFiUDP udpClient;
Syslog *syslog = NULL;
// TODO why do these syslogf variants screw up the passed arguments
// first arg always becomes 26 or 33
void syslogf(uint16_t pri, const char *fmt, ...) {
    if (syslog) {
        va_list args;
        va_start(args, fmt);
        syslog->vlogf(pri, fmt, args);
        va_end(args);
    }
}
void syslogf(const char *fmt, ...) {
    if (syslog) {
        va_list args;
        va_start(args, fmt);
        syslog->vlogf(LOG_DAEMON, fmt, args);
        va_end(args);
    }
}
void SyslogInit(const char * name) {
    syslog = new Syslog(udpClient, MY_SYSLOG_SERVER, 514, name, name, LOG_DAEMON, SYSLOG_PROTO_BSD);
}
