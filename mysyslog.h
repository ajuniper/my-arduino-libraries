// syslog stuff
#include <Syslog.h>
extern void syslogf(uint16_t pri, const char *fmt, ...);
extern void syslogf(const char *fmt, ...);
extern void SyslogInit(const char * name);
