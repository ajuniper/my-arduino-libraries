// config access
// goes via preferences library
#include <Arduino.h>
#include <myconfig.h>
#include <mywebserver.h>
#include <Preferences.h>
#include <map>
#include <mysyslog.h>
Preferences prefs;

std::map<String, MyCfgCbInt> config_int;
std::map<String, MyCfgCbString> config_string;

static void serve_config_get(AsyncWebServerRequest * request) {
    String x,y;
    AsyncWebServerResponse *response = nullptr;
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
                if (prefs.getBytesLength(x.c_str()) > 0) {
                    response = request->beginResponse(200, "text/plain", String(prefs.getInt(x.c_str(),0)));
                } else {
                    response = request->beginResponse(404, "text/plain", "not set");
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
                        response = request->beginResponse(200, "text/plain", String(z));
                    }
                } else {
                    response = request->beginResponse(400, "text/plain", e);
                }
            }

        } else if (config_string.find(x) != config_string.end()) {
            if (!request->hasParam("value")) {
                // respond with current value
                x += "." + y;
                if (prefs.getBytesLength(x.c_str()) > 0) {
                    response = request->beginResponse(200, "text/plain", prefs.getString(x.c_str()));
                } else {
                    response = request->beginResponse(404, "text/plain", "not set");
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
                        response = request->beginResponse(200, "text/plain", z.c_str());
                    }
                } else {
                    response = request->beginResponse(400, "text/plain", e);
                }
            }

        } else {
            response = request->beginResponse(400, "text/plain", "Config name not found");
        }
    }
    response->addHeader("Connection", "close");
    request->send(response);
}

void MyCfgRegisterInt(const char * name, MyCfgCbInt cb) {
    config_int[name] = cb;
}

void MyCfgRegisterString(const char * name, MyCfgCbString cb) {
    config_string[name] = cb;
}

void MyCfgInit(const char * ns) {
    prefs.begin(ns, false);
    server.on("/config", HTTP_GET, serve_config_get);
}

int MyCfgGetInt(const char * name, const String & id, int def) {
    String i = name;
    i += "." + id;
    return prefs.getInt(i.c_str(),def);
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
        return false;
    } else {
        return true;
    }
}
bool MyCfgPutString(const char * name, const String & id, const String & value) {
    String i = name;
    i += "." + id;
    if (prefs.putString(i.c_str(),value) != value.length()) {
        return false;
    } else {
        return true;
    }
}

