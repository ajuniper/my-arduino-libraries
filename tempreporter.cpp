#include <Arduino.h>
#include <mywifi.h>
#include "time.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#define ADAFRUIT_DHT11
#ifdef ADAFRUIT_DHT11
#include <DHT.h>
#define myDHT11_t DHT
#else
#include <DHT11.h>
#define myDHT11_t DHT11
#endif

#ifdef ESP8266
#include <ESP8266HTTPClient.h>
#else
#include <HTTPClient.h>
#endif
#include <mysyslog.h>
#include "myconfig.h"
#include "tempreporter.h"

#include <my_secrets.h>

/*
Config nodes:
        remap.ADDR = label
        pin.[ds18b20|dht11] = number
        temprep.poll = seconds
        temprep.submit = seconds
        tr-enable.NAMEORADDR = 0/1
*/
// how frequently we take readings
#define INTERVAL_SAMPLE 5
int interval_sample = INTERVAL_SAMPLE;
// how frequently we report readings
#define INTERVAL_REPORT 60
int interval_report = INTERVAL_REPORT;

//Your influx Domain name with URL path or IP address with path
static const char* serverName = MY_INFLUX_DB;
static const char* authtoken = MY_INFLUX_AUTHTOKEN;

class mysensor {
    public:
        mysensor(const char * t="temperature") :
            lastReading(0.0), type(t), enable(true) {
            // no address known at this time
        };
        mysensor(String &a, const char * t="temperature") :
            lastReading(0.0), type(t), enable(true) {
            setAddr(a.c_str());
        };
        virtual ~mysensor() {};
        float getReading() { return lastReading; }
        const String & getName() const { return str; }
        virtual const String & getAddr() const { return realAddress; }
        const char * getType() const { return type; }
        virtual void updateReading() = 0;
        void setName(const String & s) { str = s; }
        void setEnable(bool e) { enable = e; }
        bool getEnable() const { return enable; }
    protected:
        float lastReading;
        void setAddr(const char *a) {
            if (a != NULL && *a != 0) {
                realAddress = a;
                // load the remapping if present
                str = MyCfgGetString("remap",a,a);
            }
        }
    private:
        // admin label
        String str;
        // string representation of real hardware address
        String realAddress;
        // last read value
        const char * type;
        // is reporting enabled for this channel?
        bool enable;
};
class mysensor_ds18b20 : public mysensor {
    public:
        mysensor_ds18b20(DallasTemperature * s, const DeviceAddress &a) : bus(s) {
            memcpy(da, a, sizeof(DeviceAddress));
            char x[21];
            sprintf(x,"%02x-%02x%02x%02x%02x%02x%02x%02x",
                    a[0],a[1],
                    a[2],a[3],
                    a[4],a[5],
                    a[6],a[7]);
            setAddr(x);
        }
        virtual ~mysensor_ds18b20() {};
        virtual void updateReading() {
            lastReading = bus->getTempC(da);
        };
    private:
        DeviceAddress da;
        DallasTemperature * bus;
};
class mysensor_dht11_temp : public mysensor {
    public:
        mysensor_dht11_temp(int pin, myDHT11_t * d) : dht11(d) {
            char a[20];
            sprintf(a,"dht11.t.%d",pin);
            setAddr(a);
        }
        virtual void updateReading() {
            // TODO error handling
            lastReading = dht11->readTemperature();
            if (isnan(lastReading)) { lastReading = 999; }
        };
        virtual ~mysensor_dht11_temp() {}
    private:
        myDHT11_t * dht11;
};
class mysensor_dht11_humidity : public mysensor {
    public:
        mysensor_dht11_humidity(int pin, myDHT11_t * d) : dht11(d), mysensor("humidity") {
            char a[20];
            sprintf(a,"dht11.h.%d",pin);
            setAddr(a);
        }
        virtual void updateReading() {
            // TODO error handling
            lastReading = dht11->readHumidity();
            if (isnan(lastReading)) { lastReading = 999; }
        };
        virtual ~mysensor_dht11_humidity() {}
    private:
        myDHT11_t * dht11;
};

// fake sensor has no address just a name and value
class mysensor_fake : public mysensor {
    public:
        mysensor_fake(const String & name) {
            setName(name);
        } ;
        virtual ~mysensor_fake() {};
        void setReading(float v) {
            lastReading = v;
        };
        virtual void updateReading() { };
};


// Our Dallas Temperature setup
static DallasTemperature * sensors = NULL;

// dht11 sensor
static myDHT11_t * dht11 = NULL;

static const int max_sensors = 10;
static mysensor * sensorAddrs[max_sensors];

// Number of real temperature devices found
static int numberOfDevices = 0;

static const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>Temperature Sensors</title>
  <meta http-equiv="refresh" content="%REFRESHTIME%">
</head>
<body>
  <h1>Temperature Sensors at %TIMENOW%</h1>
  <table border="1">
  <tr><th align="left">Sensor</th><th>Reading</th></tr>
  %TEMPPLACEHOLDER%
  </table>
</body>
</html>
)rawliteral";

static String processor(const String& var){
    //Serial.println(var);
    if(var == "TEMPPLACEHOLDER"){
        String temps = "";
        DeviceAddress da; 
        char das[20];
        // Loop through each device, print out temperature data
        for(int i=0;i<max_sensors; i++){
            if (sensorAddrs[i] == NULL) {
                // invalid, skip
                continue;
            }
            float tempC = sensorAddrs[i]->getReading();
            temps+="<tr><td>";
            String a = sensorAddrs[i]->getName();
            String b = sensorAddrs[i]->getAddr();
            temps+=a;
            if (b.isEmpty()) {
                temps+=" (fake)";
            } else if (b != a) {
                temps+=" (" + b + ")";
            }
            temps+="</td><td>";
            temps+=tempC;
            temps+="</td></tr>";
        }
        return temps;
    } else if (var == "TIMENOW") {
        struct tm timeinfo;
        time_t epoch = time(NULL);
        localtime_r(&epoch, &timeinfo);
#define dt_len 30
        char dt[dt_len];
        ssize_t l = strftime(dt,dt_len,"%F %T",&timeinfo);
        return String(dt);
    } else if (var == "REFRESHTIME") {
        return String(interval_sample);
    }
    return String();
}

static void add_sensor(mysensor * s, bool isColdBoot) {
    sensorAddrs[numberOfDevices] = s;
    char msgbuf[80];
    sprintf(msgbuf,"Device %d address %s %s", numberOfDevices, s->getAddr().c_str(),s->getName().c_str());
    Serial.println(msgbuf);
    if (isColdBoot) {
        syslogf(msgbuf);
    }
    ++numberOfDevices;
    // set the enable flag
    // prefer config based on name but use based on addr if no name config
    // default to true
    s->setEnable(MyCfgGetInt("tr",s->getName(),MyCfgGetInt("tr",s->getAddr(),true)));
}

static const char * handleInterval(const char * name, const String & id, int &value) {
    if (id == "poll") {
        // all ok, save the value
        interval_sample = value;
        return NULL;
    } else if (id == "submit") {
        // all ok, save the value
        interval_report = value;
        return NULL;
    } else {
        return "interval type not recognised";
    }
}

static const char * handleEnable(const char * name, const String & id, int &value) {
    int i;
    String x = id;
    // we only consider real sensors in this loop
    for(i=0; i<numberOfDevices; ++i) {
        // is this the sensor we are looking for?
        if ((x == sensorAddrs[i]->getName()) ||
            (x == sensorAddrs[i]->getAddr())) {
            // found the sensor, save the remap
            sensorAddrs[i]->setEnable(value);
            return NULL;
        }
    }

    // if we didn't find an existing sensor then let it be saved anyway
    // as the sensor may appear later
    return NULL;
}

static const char * handleConfigPin(const char * name, const String & id, int &value) {
    if (id == "dht11") {
        // all ok, save the value
        return NULL;
    } else if (id == "ds18b20") {
        // all ok, save the value
        return NULL;
    } else {
        return "sensor type not recognised";
    }
}

static const char * handleConfigRemap(const char * name, const String & id, String &value) {
    int i;
    String x = id;
    // we only consider real sensors in this loop
    for(i=0; i<numberOfDevices; ++i) {
        // is this the sensor we are looking for?
        if ((x == sensorAddrs[i]->getName()) ||
            (x == sensorAddrs[i]->getAddr())) {
            // found the sensor, save the remap
            sensorAddrs[i]->setName(value);
            return NULL;
        }
    }

    // if we didn't find an existing sensor then let it be saved anyway
    // as the sensor may appear later
    return NULL;
}

static void serve_root_get(AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html, processor);
}

static void serve_sensor_get(AsyncWebServerRequest * request) {
    String x;
    AsyncWebServerResponse *response = nullptr;
    // GET /sensor?id=XXX
    if (request->hasParam("id")) {
        x = request->getParam("id")->value();
        float tempC = TR_get(x);
        if (tempC < 200) {
            x = tempC;
            response = request->beginResponse(200, "text/plain", x);
        } else {
            response = request->beginResponse(404, "text/plain", "Sensor id not found");
        }
    } else {
        response = request->beginResponse(400, "text/plain", "Sensor id missing");
    }
    response->addHeader("Connection", "close");
    request->send(response);
}

static void serve_sensor_fake(AsyncWebServerRequest * request) {
    AsyncWebServerResponse *response = nullptr;
    // GET /fake?id=XXX&temp=x.xx
    if (!request->hasParam("id")) {
        response = request->beginResponse(400, "text/plain", "Sensor id missing");
    } else if (!request->hasParam("temp")) {
        response = request->beginResponse(400, "text/plain", "Temperature missing");
    } else {
        String x;
        float temp;
        int i;
        x = request->getParam("temp")->value();
        for (i=0; i<x.length(); ++i) {
            // only accept integer values
            if (!isDigit(x[i])) {
                response = request->beginResponse(400, "text/plain", "Cannot parse temperature");
                break;
            }
        }

        if (response != nullptr) {
            response = request->beginResponse(400, "text/plain", "Invalid temperature");
        } else {
            temp = atof(x.c_str());
            x = request->getParam("id")->value();
            mysensor_fake * f = NULL;
            for (i=0; i<max_sensors; ++i) {
                if (sensorAddrs[i] == NULL) {
                    // found empty slot
                    f = new mysensor_fake(x);
                    add_sensor(f,true);
                } else if (!(sensorAddrs[i]->getAddr().isEmpty())) {
                    // fakes do not have an address
                    continue;
                } else if (sensorAddrs[i]->getName() != x) {
                    // name does not match
                    continue;
                } else {
                    // found it
                    f = static_cast<mysensor_fake *>(sensorAddrs[i]);
                }

                // current sensor is the one we want
                f->setReading(temp);
                break;
            }
            if (f == NULL) {
                response = request->beginResponse(400, "text/plain", "Too many fakes");
            } else {
                // all ok, report the parsed value
                serve_sensor_get(request);
                return;
            }
        }
    }
    response->addHeader("Connection", "close");
    request->send(response);
}

static time_t next_report = 0;
time_t TR_report_data(void)
{
    time_t now = time(NULL);

    // wait for time to be known
    if (now < 1000000000) {
        return 1;
    }

    // no point continuing if there are no devices connected
    if (numberOfDevices == 0) {
        return 60;
    }

    // called back too early, tell the caller to wait again
    if (next_report > now) {
        return (next_report - now);
    }

    if (sensors) {
        sensors->requestTemperatures(); // Send the command to get temperatures
    }

    // Loop through each real device, record temperature data
    for(int i=0;i<numberOfDevices; i++){
        sensorAddrs[i]->updateReading();
    }

    if (next_report == 0 || now >= next_report) {
        //Check WiFi connection status
        while (WiFi.status()!= WL_CONNECTED) {
            Serial.println("Wifi not connected!");
            delay(500);
        }

        char post_data[80 * numberOfDevices];
        char * buf = post_data;
        for(int i=0;i<numberOfDevices; i++){
            // only submit if name has been provided
            if ((sensorAddrs[i]->getEnable()) &&
                (sensorAddrs[i]->getAddr() != sensorAddrs[i]->getName())) {
                buf += sprintf(buf, "%s,t=%s value=%f %ld000000000\n", sensorAddrs[i]->getType(), sensorAddrs[i]->getName().c_str(),sensorAddrs[i]->getReading(),now); 
            }
        }
        // time to report temperatures
        if (next_report == 0) {
            next_report = now;
        }
        next_report = next_report + interval_report;
        
        // only submit if there are readings to submit
        if (buf != post_data) {
            WiFiClient client;
            HTTPClient http;

            // curl -H "Authorization: Token xxx==" -i -XPOST "${influx}${db}" --data-binary @-
            // Your Domain name with URL path or IP address with path
            http.begin(client, serverName);
            // Specify content-type header
            http.addHeader("Authorization", authtoken);
            // Send HTTP POST request
            int httpResponseCode = http.POST(post_data);
            // Free resources
            http.end();
        }
    }

    // report time to next sample
    // if interval sample < 0 then use interval_report
    if (interval_sample < 1) {
        return interval_report+now;
    } else {
        return interval_sample+now;
    }
}

#ifndef ESP8266
static void TR_reporting_task(void *)
{
    while (1) {
        time_t next = TR_report_data();
        time_t delaay = next - time(NULL);
        if (delaay > 0) {
            delay(delaay*1000);
        }
    }
}
#endif

void TR_init(AsyncWebServer & server, bool run_in_loop, bool isColdBoot){
    char msgbuf[80];
    int pin = -1;

    interval_sample = MyCfgGetInt("temprep","poll",INTERVAL_SAMPLE);
    interval_report = MyCfgGetInt("temprep","submit",INTERVAL_REPORT);

    pin = MyCfgGetInt("pin","ds18b20",-1);
    if (pin != -1) {
        sensors = new DallasTemperature(new OneWire(pin));

        // Start up the sensor library
        sensors->begin();

        // Grab a count of devices on the wire
        int n = sensors->getDeviceCount();
        if (n > max_sensors) { n = max_sensors; }

        // locate devices on the bus
        sprintf(msgbuf,"Started with %d devices",n);
        Serial.println(msgbuf);
        if (isColdBoot) {
            syslogf(msgbuf);
        }

        // Loop through each device, print out address
        for(int i=0;i<n; i++){
            DeviceAddress da;
            // Search the wire for address
            if(sensors->getAddress(da, i)){
                add_sensor(new mysensor_ds18b20(sensors, da), isColdBoot);
            } else {
                sprintf(msgbuf,"Ghost device at %d", i);
                Serial.println(msgbuf);
                syslogf(msgbuf);
            }
        }
    }

    pin = MyCfgGetInt("pin","dht11",-1);
    if (pin != -1) {
#ifdef ADAFRUIT_DHT11
        dht11 = new DHT(pin, DHT11);
        dht11->begin();
#else
        dht11 = new DHT11(pin);
#endif
        add_sensor(new mysensor_dht11_temp(pin, dht11), isColdBoot);
        add_sensor(new mysensor_dht11_humidity(pin, dht11), isColdBoot);
    }

    if (numberOfDevices == 0) {
        syslogf("No sensors found, are pins defined?");
    }
    // only create readers once we are ready

    // Route for root / web page
    server.on("/temperatures", HTTP_GET, serve_root_get);
    server.on("/api", HTTP_GET, serve_sensor_get);
    server.on("/fake", HTTP_GET, serve_sensor_fake);

    // register our config change handlers
    MyCfgRegisterInt("pin",&handleConfigPin);
    MyCfgRegisterString("remap",&handleConfigRemap);
    MyCfgRegisterInt("temprep",&handleInterval);
    MyCfgRegisterInt("tr",&handleEnable);

#ifndef ESP8266
    // ESP8266 must always run in loop
    // TODO use ticker for ESP8266
    if (!run_in_loop) {
        // temperature logging
        xTaskCreate(TR_reporting_task, "TR", 10000, NULL, 1, NULL);
    }
#endif
}

float TR_get(const String & name) {
    int i;
    for(i=0; i<numberOfDevices; ++i) {
        if (name == sensorAddrs[i]->getName()) {
            return sensorAddrs[i]->getReading();
        }
    }
    return 999;
}
