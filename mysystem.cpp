// various system type stuff
#include <Arduino.h>
#include <mysystem.h>
#include <mywebserver.h>
#include "LittleFS.h"
#include "SPIFFS.h"
#include <mysyslog.h>
#ifdef ESP8266
#include <Esp.h>
#else
#include <esp_system.h>
#include <esp_core_dump.h>
#include <esp_partition.h>
#include <Ticker.h>

// there seems to be issues sometimes where the boot loader will hang if there is an existing
// core dump present
// so during boot, if we find a core dump, we copy that to FFS and clear the coredump partition
// and serve the crash dump from there instead
// we need a ticker to copy from the coredump partition in the hopes that we have real time
// available when we do it
Ticker coredump_save_ticker;
static void do_coredump_save() {
    if (time(NULL) < 10000000) {
        // we do not have time yet, wait a while
        coredump_save_ticker.once_ms(1000, do_coredump_save);
        return;
    }
    size_t out_addr;
    size_t out_size;
    esp_err_t ret = esp_core_dump_image_get(&out_addr, &out_size);
    esp_partition_iterator_t partition_it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, "coredump");
    if (ret != ESP_OK) {
        // no coredump present
        //Serial.printf("no coredump present\n");
    } else if (partition_it == NULL) {
        // no coredump partition present
        //Serial.printf("no coredump partition present\n");
    } else {
        const esp_partition_t * partition = esp_partition_get(partition_it);
        Serial.printf("Copying coredump size %d to littlefs\n",out_size);
        // remove any pre-existing coredump
        LittleFS.remove("/coredump.bin");
        File file = LittleFS.open("/coredump.bin", FILE_WRITE);
        if (!file) {
            // failed to open file for writing
            syslogf(LOG_DAEMON | LOG_WARNING, "Failed to open coredump file for writing");
        } else {
            unsigned char buf[512];
            out_addr = 0;
            while (out_size > 0) {
                size_t toread = min(512U, out_size);
                esp_err_t e = esp_partition_read(partition, out_addr, buf, toread);
                if (e != ESP_OK) {
                    // something broke during read
                    break;
                }
                if (file.write(buf,toread) != toread) {
                    // something broke during write
                    syslogf(LOG_DAEMON | LOG_WARNING, "Failed to write to coredump file");
                    break;
                }
                out_size -= toread;
                out_addr += toread;

            }
            file.close();
            syslogf(LOG_DAEMON | LOG_WARNING, "Wrote coredump file OK");

            // erase coredump partition
            esp_err_t z = esp_core_dump_image_erase();
            //Serial.printf("erased core dump %d\n",z);
            z = esp_partition_erase_range(partition,0,partition->size);
            //Serial.printf("erased partition %d\n",z);
        }
    }
}

// https://github.com/espressif/esp-idf/blob/v5.4/components/esp_system/include/esp_system.h
// https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/mem_alloc.html
#include <esp_heap_caps.h>

// pip install .
// PATH=$PATH:../xtensa-esp-elf-gdb/bin/ /c/Users/arepi/AppData/Local/Packages/PythonSoftwareFoundation.Python.3.12_qbz5n2kfra8p0/LocalCache/local-packages/Python312/Scripts/esp-coredump.exe  info_corefile -c ../coredump_test/core.dump ../coredump_test/build/esp32.esp32.esp32/coredump_test.ino.elf
//
static void serve_core_get(AsyncWebServerRequest *request) {
    bool erase = false;
    String x;
    AsyncWebServerResponse *response = nullptr;
    if (!LittleFS.exists("/coredump.bin")) {
        // likely no coredump present
        x = "no coredump\n";
        response = request->beginResponse(404, "text/plain", x);
    };

    if ((response == nullptr) && (request->hasParam("erase"))) {
        x = request->getParam("erase")->value();
        //Serial.printf("erase param %s\n",x.c_str());
        if (x == "true") {
            //Serial.printf("Wiping coredump\n");
            LittleFS.remove("/coredump.bin");
            response = request->beginResponse(200, "text/plain", "Coredump erased");
        } else {
            response = request->beginResponse(200, "text/plain", "Coredump not erased");
        }
    }

    if (response == nullptr) {
        // serve coredump
        File coredump = LittleFS.open("/coredump.bin", FILE_READ);
        if (!coredump) {
            // WTF?
            x = "Failed to open /coredump.bin\n";
            response = request->beginResponse(404, "text/plain", x);
        } else {
            // set multipart filename
            // Content-Disposition: attachment; filename="coredump-xxyyzz.bin"
            // coredump.getLastWrite() gives time_t
            char fakename[60];
            struct tm timeinfo;
            //getLocalTime(&timeinfo);
            time_t filetime = coredump.getLastWrite();
            localtime_r(&filetime, &timeinfo);
            strftime(fakename, 60, "/coredump-%Y%m%d-%H%M%S.bin",&timeinfo);
            response = request->beginResponse(coredump, fakename, "application/octet-stream", true);
        }
    }

    response->addHeader("Connection", "close");
    request->send(response);
}
#endif

static const char * filesystem = "unknown";

static void serve_makefs(AsyncWebServerRequest *request) {
    bool littleFS = true;
    String x;
    AsyncWebServerResponse *response = nullptr;

    if (request->hasParam("type")) {
        x = request->getParam("type")->value();
        if (x == "littlefs") {
            littleFS = true;
        } else if (x == "spiffs") {
            littleFS = false;
        } else {
            x = "Filesystem type " + request->getParam("type")->value() + " not recognised";
            response = request->beginResponse(400, "text/plain", x);
        }
    } else {
        response = request->beginResponse(400, "text/plain", "FS type parameter missing");
    }

    if (response == nullptr) {
        SPIFFS.end();
        LittleFS.end();
        bool ret = false;
        if (littleFS) {
            LittleFS.format();
            ret = LittleFS.begin();
        } else {
            SPIFFS.format();
            ret = SPIFFS.begin();
        }

        if (ret) {
            x = "Created ";
            x += (littleFS)?"littlefs":"SPIFFS";
            x += " filesystem ok";
            response = request->beginResponse(200, "text/plain", x);
        } else {
            response = request->beginResponse(500, "text/plain", "Failed to create filesystem");
        }
    }
    response->addHeader("Connection", "close");
    request->send(response);
}

static void serve_status_get(AsyncWebServerRequest *request) {
    String x("<html><head><title>System Status</title></head><body><pre>");
    AsyncWebServerResponse *response = nullptr;
    x += "\nSystem time: ";
    x += String(time(NULL));
    x += "\nReset reason: ";
#ifdef ESP8266
    x += ESP.getResetReason();
#else
    x += String(esp_reset_reason());
    x += "\nCore dump check: ";
    x += esp_core_dump_image_check();
#endif
    x += "\nHeap total free bytes: ";
#ifdef ESP8266
    x += String(ESP.getFreeHeap());
#else
    x += String(esp_get_free_heap_size());
    x += "\nHeap minimum free bytes: ";
    x += String(esp_get_minimum_free_heap_size());

#endif
    x += "\n";

    x += "\nFilesystem: ";
    x += filesystem;

    //char b[1024];
    //vTaskGetRunTimeStats(b);
    //x += b;
    x += "\n</pre></body></html>";
    response = request->beginResponse(200, "text/plain", x);
    response->addHeader("Connection", "close");
    request->send(response);
}

void SYS_init() {
    if (LittleFS.begin()) {
        // we have a littlefs
        filesystem="littlefs";
    } else if (SPIFFS.begin()) {
        // we have SPIFFS
        filesystem="spiffs";
    }

#ifndef ESP8266
    esp_core_dump_init();
    server.on("/coredump",HTTP_GET, serve_core_get);

    if (LittleFS.begin()) {
        // littlefs filesystem is present, copy any core dump to that
        do_coredump_save();
    }
#endif
    server.on("/status",HTTP_GET, serve_status_get);
    server.on("/makefs",HTTP_GET, serve_makefs);
}
