// config access
// goes via preferences library
#include <Arduino.h>
#include <myconfig.h>
#include <mywebserver.h>
#include <Preferences.h>
#include <map>
#include <mysyslog.h>
#include <mywifi.h>

Preferences prefs;

std::map<String, MyCfgCbInt> config_int;
std::map<String, MyCfgCbFloat> config_float;
std::map<String, MyCfgCbString> config_string;
bool redirectToRoot = false;

#ifdef ESP32
// https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/storage/nvs_flash.html
#include "nvs.h"
#include "nvs_flash.h"
static void serve_config_list(AsyncWebServerRequest * request) {
    String ls;
    nvs_iterator_t it = nvs_entry_find("nvs", NULL, NVS_TYPE_ANY);
    while(it != NULL) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        ls += info.namespace_name;
        ls += "/";
        ls += info.key;
        ls += " (";
        ls += String(info.type);
        ls += ")\n";
        // TODO print content size
        it = nvs_entry_next(it);
    }
    nvs_release_iterator(it);
    AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", ls);
    request->send(response);
}
#endif

static void serve_config_clear(AsyncWebServerRequest * request) {
    AsyncWebServerResponse *response = request->beginResponse(200, "text/html", "<html><head><meta http-equiv=\"refresh\" content=\"10; url=/\"></head><body>Config cleared; rebooting... bye bye...</body></html>");
    response->addHeader("Connection", "close");
    // TODO how to defer the restart until the response has been sent?
    request->send(response);
    prefs.clear();
    prefs.end();
    syslogf(LOG_DAEMON|LOG_CRIT,"Config clear, restarting");
    Serial.printf("Config cleared, restarting");
    WIFI_going_for_reboot();
    delay(1000);
    ESP.restart();
}

static void serve_config_get(AsyncWebServerRequest * request) {
    String x,y;
    AsyncWebServerResponse *response = nullptr;
    String respStr;
    // GET /config?name=XX&id=YY
    // GET /config?name=XX&id=YY&value=ZZ
    if (!request->hasParam("name")) {
        response = request->beginResponse(400, "text/plain", "Config name is missing");
    } else if (!request->hasParam("id")) {
        response = request->beginResponse(400, "text/plain", "Config id is missing");
    } else {
        x = request->getParam("name")->value();
        y = request->getParam("id")->value();

        if (config_int.find(x) != config_int.end()) {
            if (!request->hasParam("value")) {
                // respond with current value
                x += "." + y;
                if (prefs.isKey(x.c_str())) {
                    response = request->beginResponse(200, "text/plain", String(prefs.getInt(x.c_str(),0)));
                } else {
                    x = x + " not set";
                    response = request->beginResponse(404, "text/plain", x.c_str());
                }
            } else {
                // set new int value
                int z = request->getParam("value")->value().toInt();
                const char * e = (config_int.find(x)->second)(x.c_str(),y,z);
                if (e == NULL) {
                    x += "." + y;
                    if (prefs.putInt(x.c_str(),z) != 4) {
                        response = request->beginResponse(500, "text/plain", "failed to save preference");
                    } else {
                        syslogf("Set %s to %d",x,z);
                        respStr = String(z);
                    }
                } else {
                    response = request->beginResponse(400, "text/plain", e);
                }
            }
        } else if (config_float.find(x) != config_float.end()) {
            if (!request->hasParam("value")) {
                // respond with current value
                x += "." + y;
                if (prefs.isKey(x.c_str())) {
                    response = request->beginResponse(200, "text/plain", String(prefs.getFloat(x.c_str(),0)));
                } else {
                    x = x + " not set";
                    response = request->beginResponse(404, "text/plain", x.c_str());
                }
            } else {
                // set new float value
                float z = request->getParam("value")->value().toFloat();
                const char * e = (config_float.find(x)->second)(x.c_str(),y,z);
                if (e == NULL) {
                    x += "." + y;
                    if (prefs.putFloat(x.c_str(),z) != 4) {
                        response = request->beginResponse(500, "text/plain", "failed to save preference");
                    } else {
                        syslogf("Set %s to %f",x,z);
                        respStr = String(z);
                    }
                } else {
                    response = request->beginResponse(400, "text/plain", e);
                }
            }

        } else if (config_string.find(x) != config_string.end()) {
            if (!request->hasParam("value")) {
                // respond with current value
                x += "." + y;
                if (prefs.isKey(x.c_str())) {
                    response = request->beginResponse(200, "text/plain", prefs.getString(x.c_str()));
                } else {
                    x = x + " not set";
                    response = request->beginResponse(404, "text/plain", x.c_str());
                }
            } else {
                // set new string value
                String z = request->getParam("value")->value();
                const char * e = (config_string.find(x)->second)(x.c_str(),y,z);
                if (e == NULL) {
                    x += "." + y;
                    if (prefs.putString(x.c_str(),z) != z.length()) {
                        response = request->beginResponse(500, "text/plain", "failed to save preference");
                    } else {
                        syslogf("Set %s to %d",x,z);
                        respStr = z;
                    }
                } else {
                    response = request->beginResponse(400, "text/plain", e);
                }
            }

        } else {
            response = request->beginResponse(400, "text/plain", "Config name not found");
        }
    }
    if (response == nullptr) {
        if (redirectToRoot) {
            response = redirect_to_root(request, respStr.c_str());
        } else {
            response = request->beginResponse(200, "text/plain", respStr.c_str());
        }
    }
    response->addHeader("Connection", "close");
    request->send(response);
}

void MyCfgRegisterInt(const char * name, MyCfgCbInt cb) {
    config_int[name] = cb;
}

void MyCfgRegisterFloat(const char * name, MyCfgCbFloat cb) {
    config_float[name] = cb;
}

void MyCfgRegisterString(const char * name, MyCfgCbString cb) {
    config_string[name] = cb;
}

void MyCfgInit(bool _redirectToRoot, const char * ns) {
    redirectToRoot = _redirectToRoot;
    Serial.printf("Starting config in namespace \"%s\"\n",ns?ns:"none");
    if (prefs.begin(ns, false) == false) {
        Serial.println("Failed to begin preferences library!");
    }
    server.on("/config", HTTP_GET, serve_config_get);
    server.on("/configreset", HTTP_GET, serve_config_clear);
#ifdef ESP32
    server.on("/configlist", HTTP_GET, serve_config_list);
#endif
}

int MyCfgGetInt(const char * name, const String & id, int def) {
    String i = name;
    i += "." + id;
    return prefs.getInt(i.c_str(),def);
}
float MyCfgGetFloat(const char * name, const String & id, float def) {
    String i = name;
    i += "." + id;
    return prefs.getFloat(i.c_str(),def);
}
String MyCfgGetString(const char * name, const String & id, const String & def) {
    String i = name;
    i += "." + id;
    return prefs.getString(i.c_str(),def);
}
bool MyCfgPutInt(const char * name, const String & id, int value) {
    String i = name;
    i += "." + id;
    if (prefs.putInt(i.c_str(),value) != 4) {
        syslogf(LOG_DAEMON|LOG_CRIT,"Config save %s failed",i.c_str());
        return false;
    } else {
        return true;
    }
}
bool MyCfgPutFloat(const char * name, const String & id, float value) {
    String i = name;
    i += "." + id;
    if (prefs.putFloat(i.c_str(),value) != 4) {
        syslogf(LOG_DAEMON|LOG_CRIT,"Config save %s failed",i.c_str());
        return false;
    } else {
        return true;
    }
}
bool MyCfgPutString(const char * name, const String & id, const String & value) {
    String i = name;
    i += "." + id;
    if (prefs.putString(i.c_str(),value) != value.length()) {
        syslogf(LOG_DAEMON|LOG_CRIT,"Config save %s failed",i.c_str());
        return false;
    } else {
        return true;
    }
}

