//////////////////////////////////////////////////////////////////////////////
//
// Time setup
#include <Arduino.h>
#include "mytime.h"

#ifdef ESP8266
#include "sntp.h"
#else
#include "esp_sntp.h"
#endif
#include "time.h"
#include <Wire.h>
#include <ErriezDS1302.h>
#include <errno.h>
#include <string.h>
#include <my_secrets.h>

ErriezDS1302 * rtc = NULL;
static bool have_rtc = false;

#ifdef ESP8266
// resync every hour
uint32_t sntp_update_delay_MS_rfc_not_less_than_15000() { return 60*60*1000; }

static void timeSyncCallback(bool from_sntp)
{
    if (from_sntp) {
        // reset the RTC based on NTP
        rtc->setEpoch(time(NULL));
    }
}
#else
static void timeSyncCallback(struct timeval *tv)
{
    // reset the RTC based on NTP
    rtc->setEpoch(tv->tv_sec);
}
#endif

void mytime_setup(const char * tz, int pin_clk, int pin_data, int pin_rst)
{
    setenv("TZ", tz, 1);
    tzset();

    // create RTC object
    rtc = new ErriezDS1302(pin_clk, pin_data, pin_rst);

    // Initialize I2C
    Wire.begin();
    Wire.setClock(100000);

    // Initialize RTC
    int i = 0;
    while (i++ < 3) {
        if (rtc->begin()) {
            have_rtc = true;
            break;
        }
        delay(1000);
    }
    if (have_rtc == false) {
        Serial.println("No RTC!");
    } else {
#ifdef ESP8266
        settimeofday_cb(timeSyncCallback);
#else
        sntp_set_time_sync_notification_cb(timeSyncCallback);
#endif
        if (!rtc->isRunning()) {
            // Enable oscillator - time not trustworthy
            Serial.println("RTC is not running yet");
            rtc->clockEnable(true);
        } else {
            Serial.println("RTC is running");
#ifdef ESP8266
            struct timeval now;
            now.tv_sec = rtc->getEpoch();
            now.tv_usec = 0;
            if (settimeofday(&now,NULL))
#else
            // get date from RTC and set cpu clock
            struct timespec now;
            now.tv_sec = rtc->getEpoch();
            now.tv_nsec = 0;
            if (clock_settime(CLOCK_REALTIME,&now))
#endif
            {
                Serial.println("Failed to set time from RTC");
                Serial.println(strerror(errno));
            }
        }
    }

#ifndef ESP8266
    // resync every hour
    sntp_set_sync_interval(3600*000);
#endif

    // SNTP gets started when Wifi connects
    configTzTime(tz, MY_NTP_SERVER1, MY_NTP_SERVER2, MY_NTP_SERVER3);
}
