// various system type stuff
#include <Arduino.h>
#include <mysystem.h>
#include <mywebserver.h>
#include <esp_system.h>
#include <esp_core_dump.h>
#include <esp_partition.h>

// https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/mem_alloc.html
#include <esp_heap_caps.h>

// pip install .
// PATH=$PATH:../xtensa-esp-elf-gdb/bin/ /c/Users/arepi/AppData/Local/Packages/PythonSoftwareFoundation.Python.3.12_qbz5n2kfra8p0/LocalCache/local-packages/Python312/Scripts/esp-coredump.exe  info_corefile -c ../coredump_test/core.dump ../coredump_test/build/esp32.esp32.esp32/coredump_test.ino.elf 
//
static void _serve_core_get(AsyncWebServerRequest *request) {
    String x;
    AsyncWebServerResponse *response = nullptr;

    // esp_err_t esp_core_dump_image_get(size_t* out_addr, size_t *out_size);
    size_t out_addr;
    size_t out_size;
    esp_err_t ret = esp_core_dump_image_get(&out_addr, &out_size);
    esp_partition_iterator_t partition_it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, "coredump");
    if (ret != ESP_OK) {
        x = "no coredump: ";
        x+= String(ret);
        x+= "\n";
        response = request->beginResponse(404, "text/plain", x);
    } else if (partition_it == NULL) {
        x = "no coredump partition\n";
        response = request->beginResponse(400, "text/plain", x);
    } else {
        const esp_partition_t * partition = esp_partition_get(partition_it);
        response = request->beginResponse(
            "application/octet-stream",
            out_size,
            [partition, out_size](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
                //Write up to "maxLen" bytes into "buffer" and return the amount written.
                size_t tosend = min(maxLen, out_size-index);
                //memcpy(buffer,((const unsigned char *)out_addr)+index,tosend);
                esp_err_t e = esp_partition_read(partition, index, buffer, tosend);
                if (e != ESP_OK) {
                    // abort the transfer
                    tosend = 0;
                }
                return tosend;
        });
    }
    response->addHeader("Connection", "close");
    request->send(response);
}
static void serve_core_get(AsyncWebServerRequest *request) {
    bool erase = false;
    if (request->hasParam("erase")) {
        String x = request->getParam("erase")->value();
        if (x == "true") {
            erase = true;
        }
    }
    _serve_core_get(request);
    if (erase) {
        esp_core_dump_image_erase();
    }
}

static void serve_status_get(AsyncWebServerRequest *request) {
    String x("<html><head><title>System Status</title></head><body><pre>");
    AsyncWebServerResponse *response = nullptr;
    x += "\nReset reason: ";
    x += String(esp_reset_reason());
    x += "\nHeap total free bytes: ";
    x += String(esp_get_free_heap_size());
    x += "\nHeap minimum free bytes: ";
    x += String(esp_get_minimum_free_heap_size());
    x += "\n";

    //char b[1024];
    //vTaskGetRunTimeStats(b);
    //x += b;
    x += "\n</pre></body></html>";
    response = request->beginResponse(200, "text/plain", x);
    response->addHeader("Connection", "close");
    request->send(response);
}

void SYS_init() {
    esp_core_dump_init();
    server.on("/coredump",HTTP_GET, serve_core_get);
    server.on("/status",HTTP_GET, serve_status_get);
}
