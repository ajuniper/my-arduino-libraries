#include <Arduino.h>
#include <mywifi.h>
#include "time.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#ifdef ESP8266
#include <ESP8266HTTPClient.h>
#else
#include <HTTPClient.h>
#endif
#include <mysyslog.h>
#include <LittleFS.h>
#include "tempreporter.h"

#include <my_secrets.h>

// how frequently we take readings
#define INTERVAL_SAMPLE 5
// how frequently we report readings
#define INTERVAL_REPORT 60

//Your influx Domain name with URL path or IP address with path
static const char* serverName = MY_INFLUX_DB;
static const char* authtoken = MY_INFLUX_AUTHTOKEN;

// Our Dallas Temperature setup
static DallasTemperature * sensors = NULL;

static const int max_sensors = 10;
struct sensorAddr {
    String str;
    String realAddress;
    DeviceAddress da;
    float lastReading;
};
static sensorAddr sensorAddrs[max_sensors];

// Number of real temperature devices found
static int numberOfDevices = 0;

static const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>Temperature Sensors</title>
  <meta http-equiv="refresh" content="60">
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
            if (sensorAddrs[i].str.isEmpty()) {
                // invalid, skip
                continue;
            }
            float tempC = sensorAddrs[i].lastReading;
            temps+="<tr><td>";
            temps+=sensorAddrs[i].str;
            if (sensorAddrs[i].realAddress.isEmpty()) {
                temps+=" (fake)";
            } else if (sensorAddrs[i].str != sensorAddrs[i].realAddress) {
                temps+=" (" + sensorAddrs[i].realAddress + ")";
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
    }
    return String();
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
    // GET /sensor?id=XXX&temp=x.xx
    if (!request->hasParam("id")) {
        response = request->beginResponse(400, "text/plain", "Sensor id missing");
    } else if (!request->hasParam("temp")) {
        response = request->beginResponse(400, "text/plain", "Temperature missing");
    } else {
        String x;
        float temp;
        int i;
        x = request->getParam("id")->value();
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
            for (i=numberOfDevices; i<max_sensors; ++i) {
                if ((sensorAddrs[i].str.isEmpty()) ||
                    (sensorAddrs[i].str == x)) {
                    sensorAddrs[i].str = x;
                    sensorAddrs[i].lastReading = temp;
                    break;
                }
            }
            if (i == numberOfDevices) {
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

static void serve_remap_get(AsyncWebServerRequest *request) {
    String x,y;
    AsyncWebServerResponse *response = nullptr;
    // 0=not found, 1=add remap, 2=report current remap
    int action = 0;
    // GET /remap?id=XXX&to=YYY
    if (!request->hasParam("id")) {
        response = request->beginResponse(400, "text/plain", "Sensor id missing");
    } else {
        if (!LittleFS.begin()) {
            syslog.logf("filesystem begin failed");
        }

        x = request->getParam("id")->value();
        if (request->hasParam("to")) {
            y = request->getParam("to")->value();
        }
        int i;
        // we only consider real sensors in this loop
        for(i=0; i<numberOfDevices; ++i) {
            // is this the sensor we are looking for?
            if ((x == sensorAddrs[i].str) || (x == sensorAddrs[i].realAddress)) {
                if (request->hasParam("to")) {
                    action = 1;
                    sensorAddrs[i].str = y;
                    x = sensorAddrs[i].realAddress;
                    break;
                } else {
                    // no "to" parameter, just report current remap
                    x = sensorAddrs[i].realAddress + " " + sensorAddrs[i].str;
                    response = request->beginResponse(200, "text/plain", x);
                    action = 2;
                }
                break;
            }
        }

        // if we didn't find an existing sensor then check if the id looks like
        // a sensor address - if it does then we save the remap file for later
        if ((action == 0) && (x.length() == 17)) {
            action = 2;
            for (i=0; i<17; ++i) {
                if (i == 2) {
                    if (x[i] != '-') {
                        action = 0;
                        break;
                    }
                } else {
                    if (!isHexadecimalDigit(x[i])) {
                        action = 0;
                        break;
                    }
                }
            }
            // is the remap id a valid sensor ID?
            if (action == 2) {
                // valid remap id
                if (request->hasParam("to")) {
                    // and we have a new name so we will save that
                    action = 1;
                } else {
                    // no "to" parameter, just report current remap from file (if any)
                    y = "/" + x;
                    File f = LittleFS.open(y.c_str(), "r");
                    if (f) {
                        while (f.available()) {
                            y = f.readString();
                        }
                        f.close();
                    } else {
                        y = "(no remap)";
                    }
                    x += " " + y + " (sensor not present)";
                    response = request->beginResponse(200, "text/plain", x);
                }
            }
        }

        // must save rewrite, new value is in y
        if (action == 1) {
            String fn = "/" + x;
            // dont care about failure
            LittleFS.remove(fn);
            // is a new value specified
            if (!y.isEmpty()) {
                File f = LittleFS.open(fn,"w");
                if (f) {
                    f.print(y);
                    f.close();
                    x = "Remapping "+x+" to "+y;
                    response = request->beginResponse(200, "text/plain", x);
                    syslog.logf(x.c_str());
                } else {
                    response = request->beginResponse(500, "text/plain", "Failed to create remap file "+fn);
                }
            } else {
                // no remap string given, just remove any current remap
                y = "Remap removed for "+x;
                response = request->beginResponse(200, "text/plain", y);
            }
        }

        // no need to keep the FS open
        LittleFS.end();

        if (action == 0) {
            response = request->beginResponse(404, "text/plain", "Sensor id "+x+" not found");
        }
    }
    response->addHeader("Connection", "close");
    request->send(response);
}

#ifdef ESP8266
void loop()
#else
static void reporting_task(void *)
#endif
{
    time_t next_report = 0;

    while (1) {
        time_t now = time(NULL);

        //Check WiFi connection status
        while (WiFi.status()!= WL_CONNECTED) {
            Serial.println("Wifi not connected!");
            delay(500);
        }

        // no point continuing if there are no devices connected
        if (numberOfDevices == 0) {
            delay(10000);
            continue;
        }

        sensors->requestTemperatures(); // Send the command to get temperatures

        // Loop through each real device, record temperature data
        for(int i=0;i<numberOfDevices; i++){
            // Search the wire for address
            float tempC = sensors->getTempC(sensorAddrs[i].da);
            sensorAddrs[i].lastReading = tempC;
        }

        if (now > next_report) {
            char post_data[80 * numberOfDevices];
            char * buf = post_data;
            for(int i=0;i<numberOfDevices; i++){
                buf += sprintf(buf, "temperature,t=%s value=%f %ld000000000\n", sensorAddrs[i].str.c_str(),sensorAddrs[i].lastReading,now); 
            }
            // time to report temperatures
            next_report = now + INTERVAL_REPORT;
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

        int d = INTERVAL_SAMPLE+now-time(NULL);
        if (d > 0) {
            delay(d*1000);
        }
    }
}

void TR_init(AsyncWebServer & server, int onewire_pin){
    char msgbuf[80];

    sensors = new DallasTemperature(new OneWire(onewire_pin));

    // Start up the sensor library
    sensors->begin();

    // Grab a count of devices on the wire
    numberOfDevices = sensors->getDeviceCount();
    if (numberOfDevices > max_sensors) { numberOfDevices = max_sensors; }

    // locate devices on the bus
    sprintf(msgbuf,"Started with %d devices",numberOfDevices);
    Serial.println(msgbuf);
    syslog.logf(msgbuf);

    // don't care if no FS, open will fail and no remap possible
    if (!LittleFS.begin()) {
        sprintf(msgbuf,"filesystem begin failed, try format");
        Serial.println(msgbuf);
        syslog.logf(msgbuf);
        if (!LittleFS.format()) {
            sprintf(msgbuf,"filesystem begin failed");
            Serial.println(msgbuf);
            syslog.logf(msgbuf);
        } else if (!LittleFS.begin()) {
            sprintf(msgbuf,"second filesystem begin failed");
            Serial.println(msgbuf);
            syslog.logf(msgbuf);
        }
    }

    // Loop through each device, print out address
    for(int i=0;i<numberOfDevices; i++){
        // Search the wire for address
        if(sensors->getAddress(sensorAddrs[i].da, i)){
            char s[21];
            sprintf(s,"/%02x-%02x%02x%02x%02x%02x%02x%02x",
                    sensorAddrs[i].da[0],sensorAddrs[i].da[1],
                    sensorAddrs[i].da[2],sensorAddrs[i].da[3],
                    sensorAddrs[i].da[4],sensorAddrs[i].da[5],
                    sensorAddrs[i].da[6],sensorAddrs[i].da[7]);
            sensorAddrs[i].str = (s+1);
            sensorAddrs[i].realAddress = (s+1);
            File f = LittleFS.open(s, "r");
            if (f) {
                while (f.available()) {
                    sensorAddrs[i].str = f.readString();
                }
                f.close();
            }
            sprintf(msgbuf,"Device %d address %s %s", i, (s+1), sensorAddrs[i].str.c_str());
        } else {
            sprintf(msgbuf,"Ghost device at %d", i);
        }
        Serial.println(msgbuf);
        syslog.logf(msgbuf);
    }
    // no need to keep the FS open
    LittleFS.end();

    // only create readers once we are ready

    // Route for root / web page
    server.on("/temperatures", HTTP_GET, serve_root_get);
    server.on("/api", HTTP_GET, serve_sensor_get);
    server.on("/remap", HTTP_GET, serve_remap_get);
    server.on("/fake", HTTP_GET, serve_sensor_fake);

#ifndef ESP8266
    // temperature logging
    xTaskCreate(reporting_task, "TR", 10000, NULL, 1, NULL);
#endif
}

float TR_get(const String & name) {
    int i;
    for(i=0; i<max_sensors; ++i) {
        if (sensorAddrs[i].str.isEmpty()) {
            continue;
        }
        if (name == sensorAddrs[i].str) {
            return sensorAddrs[i].lastReading;
        }
    }
    return 999;
}
