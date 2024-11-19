
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

/*
Config nodes:
        fcst.rate (hours)
        fcst.ahead (hours)
        weather.url
*/

// how frequently we take readings
#define INTERVAL_SAMPLE 60
static int interval_sample = INTERVAL_SAMPLE;
static int forecast_lookahead = 12; // hours

// https://api.open-meteo.com/v1/forecast?latitude=50.50000&longitude=-1.000000&hourly=temperature_2m&timezone=GMT&timeformat=unixtime&past_days=0&forecast_days=2
// look for .hourly.time[] > now and up to e.g. 6 hours ahead
// pick lowest matching .hourly.temperature_2m[]
String weather_url = "https://api.open-meteo.com/v1/forecast?latitude=" MY_LATITUDE "&longitude=" MY_LONGDITUDE "&hourly=temperature_2m&timezone=GMT&timeformat=unixtime&past_days=0&forecast_days=2";

int forecast_low_temp = 10;

class WeatherForecast: public JsonHandler {

    private:
        bool in_times = false;
        bool in_temps = false;
        int min_index = 999;
        int max_index = -1;
        float lowest_temp = 999.0;
        time_t now,limit;
        bool finished = false;

    public:
        virtual void startDocument() {
            now = time(NULL);
            limit = now + (forecast_lookahead*3600);
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
            forecast_low_temp = round(lowest_temp);
            finished = true;
        };

        bool status() const {
            return finished;
        }

        virtual void value(ElementPath path, ElementValue value) {
            if (in_times) {
                if ((value.getInt() >= now) && (min_index == 999)) {
                    min_index = path.getIndex();
                    Serial.println(min_index);
                }
                if (value.getInt() <= limit) {
                    max_index = path.getIndex();
                    Serial.println(max_index);
                }
            } else if (in_temps) {
                if ((path.getIndex() >= min_index) &&
                    (path.getIndex() <= max_index) &&
                    (value.getFloat() < lowest_temp)) {
                    lowest_temp = value.getFloat();
                    Serial.println(lowest_temp);
                }
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
    }
    http.end();
    return ret;
}

#ifdef ESP8266
// ticker for ESP8266
Ticker TF_reporting_ticker;
#else
// task wrapper for ESP32
static void TF_reporting_task(void *)
{
    time_t last = 0;
    bool ret;
    // wait a while for networking
    delay(15000);

    while (1) {
        ret = TF_get_forecast();
        time_t now = time(NULL);
        if (ret) {
            if (last == 0) { last = now; }
            last += (interval_sample * 3600);
            // if we took too long then reset the cycle
            if (last <= now) {
                last = now + (interval_sample * 3600);
            }
            if (last > now) {
                delay((last - now)*1000);
            }
        } else {
            // failed to get the temperature, try again in a minute
            delay(60000);
        }
    }
}
#endif

static const char * handleConfigInt(const char * name, const String & id, int &value) {
    if (id == "rate") {
        // all ok, save the value
        interval_sample = value;
#ifdef ESP8266
        TF_reporting_ticker.attach(interval_sample, TF_get_forecast);
#endif
        return NULL;
    } else if (id == "ahead") {
        // all ok, save the value
        forecast_lookahead = value;
        return NULL;
    } else {
        return "forecast value not recognised";
    }
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
    weather_url = MyCfgGetString("weather","url",weather_url);

#ifdef ESP8266
    TF_reporting_ticker.attach(interval_sample * 3600, TF_get_forecast);
#else
    xTaskCreate(TF_reporting_task, "TF", 10000, NULL, 1, NULL);
#endif

    // register our config change handlers
    MyCfgRegisterInt("fcst",&handleConfigInt);
    MyCfgRegisterString("weather",&handleConfigUrl);
}
