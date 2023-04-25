#include <Arduino.h>
#include <Update.h>
#include <mywebserver.h>
#include <mysyslog.h>

static void serve_update_page(AsyncWebServerRequest *request, const String & msg, bool connclose=false) {
    String m = "<html><body>";
    if (!msg.isEmpty()) {
        m+="<p>";
        m+=msg;
        m+="</p>";
    }
    m+="<form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form><p/><p><a href=\"/reboot\">Reboot</a></p><p>Build date: ";
    m+=__DATE__;
    m+=" ";
    m+=__TIME__;
    m+="</p></body></html>";
    AsyncWebServerResponse *response = request->beginResponse(200, "text/html", m);
    if (connclose) {
        response->addHeader("Connection", "close");
    }
    request->send(response);
}

static void serve_update_get(AsyncWebServerRequest *request) {
    serve_update_page(request,"");
}

static void serve_update_post(AsyncWebServerRequest *request){
    String m="Update completed ";
    if (!Update.hasError()) {
        m+="OK";
    } else {
        m+="badly: ";
        m+=Update.errorString();
    }
    syslog.logf(LOG_DAEMON|LOG_ERR,m.c_str());
    serve_update_page(request, m, true);
}

static void serve_update_post_body(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    if(!index){
        Serial.printf("Update Start: %s\n", filename.c_str());
        syslog.logf(LOG_DAEMON|LOG_INFO,"Update Start: %s", filename.c_str());
        if(!Update.begin())
        {
            Update.printError(Serial);
            syslog.logf(LOG_DAEMON|LOG_INFO,"Update failed: %s",Update.errorString());
        }
    }
    if(!Update.hasError()){
        if(Update.write(data, len) != len){
            Update.printError(Serial);
            syslog.logf(LOG_DAEMON|LOG_INFO,"Update failed: %s",Update.errorString());
        }
    }
    if(final){
        if(Update.end(true)){
            Serial.printf("Update Success: %uB\n", index+len);
            syslog.logf(LOG_DAEMON|LOG_INFO,"Update success");
        } else {
            Update.printError(Serial);
            syslog.logf(LOG_DAEMON|LOG_INFO,"Update failed: %s",Update.errorString());
        }
    }
}

static void serve_reboot_get(AsyncWebServerRequest *request) {
    request->send(200, "text/html", "<html><body>Rebooting... bye bye...</body></html>");
    syslog.logf(LOG_DAEMON|LOG_CRIT,"Restarting");
    Serial.printf("Restarting");
    delay(1000);
    ESP.restart();
}

void UD_init(AsyncWebServer & server)
{
    // firmware updates
    //   // https://github.com/me-no-dev/ESPAsyncWebServer#file-upload-handling
    server.on("/update", HTTP_GET, serve_update_get);
    server.on("/update", HTTP_POST, serve_update_post,serve_update_post_body);  
    server.on("/reboot", HTTP_GET, serve_reboot_get);
}
