// config access
// goes via preferences library
#include <Arduino.h>
#include <Preferences.h>
Preferences prefs;
void MyCfgInit(const char * ns) {
    prefs.begin(ns, false);
}
