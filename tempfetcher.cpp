#include <Arduino.h>
#include "time.h"
#include <my_secrets.h>
#include <mysyslog.h>
#include "myconfig.h"
#ifdef ESP8266
#include <Ticker.h>
#include <WiFiClientSecureBearSSL.h>
#include <ESP8266HTTPClient.h>
#define SECURE_CLIENT BearSSL::WiFiClientSecure
#else
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#define SECURE_CLIENT WiFiClientSecure
#endif
#include <ArduinoStreamParser.h>
#include "JsonHandler.h"

// TODO fix ESP8266 mode to have a ticker called from loop

/*
Config nodes:
        fcst.rate (hours)
        fcst.ahead (hours)
        weather.url
*/

// how frequently we take readings
#define INTERVAL_SAMPLE 60 // 1 hour
static int interval_sample = INTERVAL_SAMPLE; // minutes
static int forecast_lookahead = 12; // hours
static int forecast_lookbehind = 6; // hours

// https://api.open-meteo.com/v1/forecast?latitude=50.50000&longitude=-1.000000&hourly=temperature_2m&timezone=GMT&timeformat=unixtime&past_days=0&forecast_days=2
// look for .hourly.time[] > now and up to e.g. 6 hours ahead
// pick lowest matching .hourly.temperature_2m[]
String weather_url = "https://api.open-meteo.com/v1/forecast?latitude=" MY_LATITUDE "&longitude=" MY_LONGDITUDE "&hourly=temperature_2m&timezone=GMT&timeformat=unixtime&past_days=1&forecast_days=2";

int forecast_low_temp = 10;
int historic_low_temp = 10;
time_t temp_fetch_time = 0;
#ifndef ESP8266
static TaskHandle_t fetchtask_handle = NULL;
#endif

class tempMatcher {
    private:
        int min_index = 999;
        int max_index = -1;
        float lowest_temp = 999.0;
        int earliest = 0;
        int latest = 0;
        bool saw_value = false;
    public:
        tempMatcher() {}
        void reset(time_t from, time_t to) {
            earliest = from;
            latest = to;
            min_index = 999;
            max_index = -1;
            lowest_temp = 999.0;
            saw_value = false;
        }
        bool getTemp(int &temp) {
            // do not persist the lowest value we saw if we
            // didn't actually see one
            if (saw_value) {
                temp = round(lowest_temp);
            }
            return saw_value;
        }
        void processTime(int i, int x) {
            if ((x >= earliest) && (min_index == 999)) {
                min_index = i;
            }
            if (x <= latest) {
                max_index = i;
            }
        }
        void processValue(int i, float x) {
            if ((i >= min_index) &&
                (i <= max_index) &&
                (x < lowest_temp)) {
                lowest_temp = x;
                saw_value = true;
            }
        }
};

class WeatherForecast: public JsonHandler {

    private:
        bool in_times = false;
        bool in_temps = false;
        tempMatcher forecast;
        tempMatcher historic;
        bool finished = false;
        time_t now = 0;

    public:
        WeatherForecast() {
            now = time(NULL);
        }

        virtual void startDocument() {
            forecast.reset(now, now + (forecast_lookahead*3600));
            historic.reset(now - (forecast_lookbehind*3600), now);
        };

        virtual void startArray(ElementPath path) {
            char fullPath[200] = "";	
            path.toString(fullPath);
            if (strcmp(fullPath,"hourly.time") == 0) {
                in_times = true;
            } else if (strcmp(fullPath,"hourly.temperature_2m") == 0) {
                in_temps = true;
            }
        };

        virtual void startObject(ElementPath path) { };

        virtual void endArray(ElementPath path) {
            in_times = false;
            in_temps = false;
        };

        virtual void endObject(ElementPath path) { };

        virtual void endDocument() {
            // copy lowest temp to exported value
            finished = false;
            finished |= forecast.getTemp(forecast_low_temp);
            finished |= historic.getTemp(historic_low_temp);
            if (!finished) {
                syslogf("No useful temperatures seen!");
            } else {
                temp_fetch_time = now;
            }
        };

        bool status() const {
            return finished;
        }

        virtual void value(ElementPath path, ElementValue value) {
            if (in_times) {
                int i = path.getIndex();
                int x = value.getInt();
                forecast.processTime(i,x);
                historic.processTime(i,x);
            } else if (in_temps) {
                int i = path.getIndex();
                float x = value.getFloat();
                forecast.processValue(i,x);
                historic.processValue(i,x);
            }
        };

        virtual void whitespace(char c) {};
};

// go and read the forecast temperature
static bool TF_get_forecast()
{
    bool ret = false;
    HTTPClient http;
    std::unique_ptr<SECURE_CLIENT>client(new SECURE_CLIENT);
    client->setInsecure();
    ArudinoStreamParser parser;
    WeatherForecast custom_handler;
    parser.setHandler(&custom_handler);
    http.begin(*client, weather_url);
    http.useHTTP10();
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK)
    {
        http.writeToStream(&parser);
        ret = custom_handler.status();
    } else {
        syslogf("Failed to retrieve forecast, status %d",httpCode);
    }
    http.end();
    return ret;
}

#ifdef ESP8266
// ticker for ESP8266
// TODO reschedule early if failed
// see /c/Users/arepi/AppData/Local/Arduino15/packages/esp8266/hardware/esp8266/3.1.2/cores/esp8266/Schedule.h
Ticker TF_reporting_ticker;
static void ticker_task() {
    TF_get_forecast();
}

static void schedule_get_forecast(int when) {
    TF_reporting_ticker.attach(when, ticker_task);
}

#else
// task wrapper for ESP32
static void TF_reporting_task(void *)
{
    time_t last = 0;
    bool ret;
    // wait a while for networking
    delay(15000);
    int waittime;

    while (1) {
        ret = TF_get_forecast();
        time_t now = time(NULL);
        waittime = 0;
        if (ret) {
            if (last == 0) { last = now; }
            last += (interval_sample * 60);
            // if we took too long then reset the cycle
            if (last <= now) {
                last = now + (interval_sample * 60);
            }
            if (last > now) {
                waittime = (last - now)*1000;
            }
        } else {
            // failed to get the temperature, try again in a minute
            waittime = 60000;
        }

        // 0 indicates timeout, else signalled
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(waittime))) {
            // we were signalled so reset the last time counter
            last = 0;
        }
    }
}
#endif

static const char * handleConfigInt(const char * name, const String & id, int &value) {
    const char * ret = NULL;
    if (id == "rate") {
        // all ok, save the value (input is minutes, save as seconds)
        interval_sample = value;
    } else if (id == "ahead") {
        // all ok, save the value
        forecast_lookahead = value;
    } else if (id == "behind") {
        // all ok, save the value
        forecast_lookbehind = value;
    } else {
        ret = "forecast value not recognised";
    }
    if (ret == NULL) {
        // config changed so retrieve temperatures again
#ifdef ESP8266
        schedule_get_forecast(1);
#else
        xTaskNotifyGive( fetchtask_handle );
#endif
    }
    return ret;
}

static const char * handleConfigUrl(const char * name, const String & id, String &value) {
    if (id == "url") {
        weather_url = value;
        return NULL;
    } else {
        return "weather url not recognised";
    }
}

void TF_init() {
    interval_sample = MyCfgGetInt("fcst","rate",interval_sample);
    forecast_lookahead = MyCfgGetInt("fcst","ahead",forecast_lookahead);
    forecast_lookbehind = MyCfgGetInt("fcst","behind",forecast_lookbehind);
    weather_url = MyCfgGetString("weather","url",weather_url);

#ifdef ESP8266
    schedule_get_forecast(interval_sample * 60);
#else
    xTaskCreate(TF_reporting_task, "TF", 10000, NULL, 1, &fetchtask_handle);
#endif

    // register our config change handlers
    MyCfgRegisterInt("fcst",&handleConfigInt);
    MyCfgRegisterString("weather",&handleConfigUrl);
}
