/* usbPD firmware entry: I2C @ 400 kHz, boot/device-id log, 1 ms poll loop
 * driving the SM, 1 s heartbeat, console on USB-Serial-JTAG. Single task. */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"

#include "log.h"
#include "fusb302.h"
#include "console.h"
#include "pd_sm.h"

#ifndef BUILD_ID
#define BUILD_ID "dev"
#endif

#define HB_PERIOD_MS 1000
#define POLL_TICKS   1          /* CONFIG_FREERTOS_HZ=1000 -> 1 ms */

static uint32_t s_log_drops;

/* ---- platform glue for log.c --------------------------------------------- */
static uint32_t clock_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

/* Whole line or nothing, never block the poll loop: if the host isn't reading,
 * the TX ring fills and lines are dropped (counted, reported on next hb). */
static void usj_sink(const char *line, size_t n)
{
    if (usb_serial_jtag_write_bytes(line, n, 0) != (int)n) s_log_drops++;
}

/* ---- pd_port_ops over the FUSB302 HAL ------------------------------------ */
static int p_rd(void *c, uint8_t r, uint8_t *v) { (void)c; return fusb302_read(r, v); }
static int p_wr(void *c, uint8_t r, uint8_t v) { (void)c; return fusb302_write(r, v); }
static int p_frd(void *c, uint8_t *b, size_t n) { (void)c; return fusb302_fifo_read(b, n); }
static int p_fwr(void *c, const uint8_t *b, size_t n) { (void)c; return fusb302_fifo_write(b, n); }
static uint32_t p_now(void *c) { (void)c; return clock_ms(); }

static const struct pd_port_ops s_ops = {
    .ctx = NULL, .reg_read = p_rd, .reg_write = p_wr,
    .fifo_read = p_frd, .fifo_write = p_fwr, .now_ms = p_now,
};

static pd_sm_t s_sm;

static const char *reset_reason_str(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:   return "poweron";
    case ESP_RST_EXT:       return "ext";
    case ESP_RST_SW:        return "sw";
    case ESP_RST_PANIC:     return "panic";
    case ESP_RST_INT_WDT:   return "int_wdt";
    case ESP_RST_TASK_WDT:  return "task_wdt";
    case ESP_RST_WDT:       return "wdt";
    case ESP_RST_DEEPSLEEP: return "deepsleep";
    case ESP_RST_BROWNOUT:  return "brownout";
    case ESP_RST_SDIO:      return "sdio";
    case ESP_RST_USB:       return "usb";
    case ESP_RST_JTAG:      return "jtag";
    default:                return "unknown";
    }
}

void app_main(void)
{
    usb_serial_jtag_driver_config_t uc = { .tx_buffer_size = 4096, .rx_buffer_size = 256 };
    usb_serial_jtag_driver_install(&uc);
    usb_serial_jtag_vfs_use_driver();   /* stray printf/ESP_LOG go through the same driver */

    log_set_clock(clock_ms);
    log_set_sink(usj_sink);

    log_line_t l;
    log_begin(&l, "boot");
    log_str(&l, "build", BUILD_ID);
    log_str(&l, "reset_reason", reset_reason_str(esp_reset_reason()));
    log_uint(&l, "i2c_hz", FUSB302_I2C_HZ);
    log_end(&l);

    uint8_t id = 0;
    int rc = fusb302_init_bus();
    if (!rc) rc = fusb302_device_id(&id);
    log_begin(&l, "fusb.id");
    log_bool(&l, "ok", rc == 0);
    if (!rc) {
        log_hex8(&l, "id", id);
        log_uint(&l, "ver", id >> 4);
        log_uint(&l, "rev", id & 0x0F);
    }
    log_end(&l);

    pd_sm_init(&s_sm, &s_ops);
    console_init(&s_sm);

    uint32_t next_hb = clock_ms();
    uint32_t reported_drops = 0;
    for (;;) {
        uint8_t in[32];
        int n = usb_serial_jtag_read_bytes(in, sizeof in, 0);
        for (int i = 0; i < n; i++) console_feed((char)in[i]);

        pd_sm_step(&s_sm);

        uint32_t now = clock_ms();
        if ((int32_t)(now - next_hb) >= 0) {
            next_hb += HB_PERIOD_MS;
            if ((int32_t)(now - next_hb) >= 0) next_hb = now + HB_PERIOD_MS; /* don't burst after a stall */
            log_begin(&l, "hb");
            log_str(&l, "state", pd_sm_state_name(s_sm.state));
            log_uint(&l, "i2c_errs", fusb302_err_count());
            if (s_log_drops != reported_drops) {
                log_uint(&l, "log_drops", s_log_drops);
                reported_drops = s_log_drops;
            }
            log_end(&l);
        }
        vTaskDelay(POLL_TICKS);
    }
}
