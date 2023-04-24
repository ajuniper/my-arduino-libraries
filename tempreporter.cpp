#include <WiFi.h>
#include "time.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include <HTTPClient.h>
#include <mysyslog.h>
#include <SPIFFS.h>
#include "tempreporter.h"

#include <my_secrets.h>

//Your influx Domain name with URL path or IP address with path
static const char* serverName = MY_INFLUX_DB;
static const char* authtoken = MY_INFLUX_AUTHTOKEN;

// Our Dallas Temperature setup
static DallasTemperature * sensors = NULL;

static const int max_sensors = 10;
struct sensorAddr {
    String str;
    DeviceAddress da;
};
static sensorAddr sensorAddrs[max_sensors];

// Number of temperature devices found
static int numberOfDevices = 0;

static const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>Temperature Sensors</title>
</head>
<body>
  <h1>Temperature Sensors</h1>
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
        for(int i=0;i<numberOfDevices; i++){
            // Search the wire for address
            float tempC = sensors->getTempC(sensorAddrs[i].da);
            temps+="<tr><td>";
            temps+=sensorAddrs[i].str;
            temps+="</td><td>";
            temps+=tempC;
            temps+="</td></tr>";
        }
        return temps;
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
        int i;
        for(i=0; i<numberOfDevices; ++i) {
            if (x == sensorAddrs[i].str) {
                float tempC = sensors->getTempC(sensorAddrs[i].da);
                x = tempC;
                response = request->beginResponse(200, "text/plain", x);
                break;
            }
        }
        if (i == numberOfDevices) {
            response = request->beginResponse(404, "text/plain", "Sensor id not found");
        }
    } else {
        response = request->beginResponse(400, "text/plain", "Sensor id missing");
    }
    response->addHeader("Connection", "close");
    request->send(response);
}

static void serve_remap_get(AsyncWebServerRequest *request) {
    String x,y;
    AsyncWebServerResponse *response = nullptr;
    // GET /remap?id=XXX&to=YYY
    if (!request->hasParam("id")) {
        response = request->beginResponse(400, "text/plain", "Sensor id missing");
    } else if (!request->hasParam("to")) {
        // TODO report current remap
        response = request->beginResponse(400, "text/plain", "New ID missing");
    } else {
        x = request->getParam("id")->value();
        y = request->getParam("to")->value();
        int i;
        for(i=0; i<numberOfDevices; ++i) {
            if (x == sensorAddrs[i].str) {
                SPIFFS.remove(x);
                File f = SPIFFS.open(x,"w");
                if (f) {
                    f.print(y);
                    close(f);
                    sensorAddrs[i].str = y;
                    response = request->beginResponse(204, "text/plain", y);
                } else {
                    response = request->beginResponse(500, "text/plain", "Failed to open remap file");
                }
                break;
            }
        }
        if (i == numberOfDevices) {
            response = request->beginResponse(404, "text/plain", "Sensor id not found");
        }
    }
    response->addHeader("Connection", "close");
    request->send(response);
}

static void reporting_task(void *)
{
    while (1) {
        time_t now = time(NULL);

        //Check WiFi connection status
        while (WiFi.status()!= WL_CONNECTED) {
            Serial.println("Wifi not connected!");
            delay(500);
        }

        sensors->requestTemperatures(); // Send the command to get temperatures
        char post_data[80 * numberOfDevices];
        char * buf = post_data;

        // Loop through each device, print out temperature data
        for(int i=0;i<numberOfDevices; i++){
            // Search the wire for address
            float tempC = sensors->getTempC(sensorAddrs[i].da);
            buf += sprintf(buf, "temperature,t=%s value=%f %ld000000000\n", sensorAddrs[i].str.c_str(),tempC,now); 
        }

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

        int d = 60+now-time(NULL);
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
    sprintf(msgbuf,"Found %d devices",numberOfDevices);
    Serial.println(msgbuf);
    syslog.logf("Started with %d devices",numberOfDevices);

    // don't care if no FS, open will fail and no remap possible
    SPIFFS.begin();

    // Loop through each device, print out address
    for(int i=0;i<numberOfDevices; i++){
        // Search the wire for address
        if(sensors->getAddress(sensorAddrs[i].da, i)){
            char s[20];
            sprintf(s,"%02x-%02x%02x%02x%02x%02x%02x%02x",
                    sensorAddrs[i].da[0],sensorAddrs[i].da[1],
                    sensorAddrs[i].da[2],sensorAddrs[i].da[3],
                    sensorAddrs[i].da[4],sensorAddrs[i].da[5],
                    sensorAddrs[i].da[6],sensorAddrs[i].da[7]);
            sensorAddrs[i].str = s;
            File f = SPIFFS.open(s, "r");
            if (f) {
                while (f.available()) {
                    int c = f.read();
                    if (c >= ' ') {
                        sensorAddrs[i].str += ((char)f.read());
                    } else {
                        break;
                    }
                }
                f.close();
            }
            sprintf(msgbuf,"Device %d address %s %s", i, s, sensorAddrs[i].str.c_str());
        } else {
            sprintf(msgbuf,"Ghost device at %d", i);
        }
        Serial.println(msgbuf);
        syslog.logf(msgbuf);
    }
    // only create readers once we are ready

    // Route for root / web page
    server.on("/temperatures", HTTP_GET, serve_root_get);
    server.on("/api", HTTP_GET, serve_sensor_get);
    server.on("/remap", HTTP_GET, serve_remap_get);

    // temperature logging
    xTaskCreate(reporting_task, "TR", 10000, NULL, 1, NULL);
}

float TR_get(const String & name) {
    int i;
    for(i=0; i<numberOfDevices; ++i) {
        if (name == sensorAddrs[i].str) {
            return sensors->getTempC(sensorAddrs[i].da);
        }
    }
    return 0;
}
