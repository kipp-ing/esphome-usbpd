// bench/hello — rig probe. JSONL on the USB-Serial-JTAG console.
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"

#ifndef BUILD_ID
#define BUILD_ID ""
#endif

static unsigned long now_ms(void) { return (unsigned long)(esp_timer_get_time() / 1000); }

static void rx_task(void *arg) {
    char line[128];
    size_t n = 0;
    for (;;) {
        int c = fgetc(stdin);
        if (c == EOF) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
        if (c == '\r') continue;
        if (c == '\n') {
            line[n] = 0;
            // strip quotes/backslashes so the echo stays valid JSON
            for (size_t i = 0; i < n; i++) if (line[i] == '"' || line[i] == '\\') line[i] = '_';
            printf("{\"t\":%lu,\"ev\":\"cmd\",\"line\":\"%s\"}\n", now_ms(), line);
            n = 0;
        } else if (n < sizeof(line) - 1) {
            line[n++] = (char)c;
        }
    }
}

void app_main(void) {
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    usb_serial_jtag_driver_install(&cfg);
    usb_serial_jtag_vfs_use_driver();
    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IOLBF, 0);
    const char *bid = BUILD_ID[0] ? BUILD_ID : "dev";
    printf("{\"t\":%lu,\"ev\":\"boot\",\"build\":\"%s\",\"fw\":\"bench-hello\",\"reset\":%d}\n",
           now_ms(), bid, (int)esp_reset_reason());
    xTaskCreate(rx_task, "rx", 3072, NULL, 5, NULL);
    unsigned n = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        printf("{\"t\":%lu,\"ev\":\"hb\",\"n\":%u}\n", now_ms(), ++n);
    }
}
