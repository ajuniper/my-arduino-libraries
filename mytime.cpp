//////////////////////////////////////////////////////////////////////////////
//
// Time setup
#include <Arduino.h>
#include "mytime.h"

#include "esp_sntp.h"
#include "time.h"
#include <Wire.h>
#include <ErriezDS1302.h>
#include <errno.h>
#include <string.h>
#include <my_secrets.h>

ErriezDS1302 * rtc = NULL;
static bool have_rtc = false;

static void timeSyncCallback(struct timeval *tv)
{
    // reset the RTC based on NTP
    rtc->setEpoch(tv->tv_sec);
}

void mytime_setup(const char * tz, int pin_clk, int pin_data, int pin_rst)
{
    setenv("TZ", tz, 1);

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
        sntp_set_time_sync_notification_cb(timeSyncCallback);
        if (!rtc->isRunning()) {
            // Enable oscillator - time not trustworthy
            Serial.println("RTC is not running yet");
            rtc->clockEnable(true);
        } else {
            Serial.println("RTC is running");
            // get date from RTC and set cpu clock
            struct timespec now;
            now.tv_sec = rtc->getEpoch();
            now.tv_nsec = 0;
            if (clock_settime(CLOCK_REALTIME,&now)) {
                Serial.println("Failed to set time from RTC");
                Serial.println(strerror(errno));
            }
        }
    }

    // resync every hour
    sntp_set_sync_interval(3600*000);

    // SNTP gets started when Wifi connects
    //configTime(0, 0, MY_NTP_SERVER1, MY_NTP_SERVER2, MY_NTP_SERVER3);
}
