#include "console.h"
#include "fusb302.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LINE_MAX_ 96

static pd_sm_t *s_sm;
static char s_line[LINE_MAX_];
static size_t s_len;
static bool s_overlong;

static const uint8_t s_dump_regs[] = { FUSB_REG_LIST };

void console_init(pd_sm_t *sm) { s_sm = sm; }

static bool parse_u(const char *s, unsigned long max, unsigned long *out)
{
    if (!s || !*s) return false;
    char *e;
    unsigned long v = strtoul(s, &e, 0);
    if (*e || v > max) return false;
    *out = v;
    return true;
}

static void reply_err(const char *cmd, const char *err, const char *line)
{
    log_line_t l;
    log_begin(&l, "cmd");
    log_str(&l, "cmd", cmd);
    log_bool(&l, "ok", false);
    log_str(&l, "err", err);
    log_str(&l, "line", line);
    log_end(&l);
}

static void cmd_regs(void)
{
    char json[LOG_LINE_MAX - 64];
    size_t n = 0;
    bool ok = true;
    json[n++] = '{';
    for (size_t i = 0; i < sizeof s_dump_regs; i++) {
        uint8_t v = 0;
        int rc = fusb302_read(s_dump_regs[i], &v);
        if (rc) ok = false;
        if (rc) n += (size_t)snprintf(json + n, sizeof json - n, "%s\"0x%02x\":null", i ? "," : "", s_dump_regs[i]);
        else    n += (size_t)snprintf(json + n, sizeof json - n, "%s\"0x%02x\":\"0x%02x\"", i ? "," : "", s_dump_regs[i], v);
    }
    json[n++] = '}';
    json[n] = 0;
    log_line_t l;
    log_begin(&l, "regs");
    log_raw(&l, "v", json);
    log_bool(&l, "ok", ok);
    log_end(&l);
}

void console_exec(const char *line)
{
    char buf[LINE_MAX_];
    strncpy(buf, line, sizeof buf - 1);
    buf[sizeof buf - 1] = 0;
    char *argv[4] = {0};
    int argc = 0;
    for (char *tok = strtok(buf, " \t"); tok && argc < 4; tok = strtok(NULL, " \t")) argv[argc++] = tok;
    if (argc == 0) return;

    log_line_t l;
    unsigned long a, v;
    if (!strcmp(argv[0], "help")) {
        log_begin(&l, "cmd");
        log_str(&l, "cmd", "help");
        log_bool(&l, "ok", true);
        log_str(&l, "usage", "regs | rd <reg> | wr <reg> <val> | pd want <mV> | reboot");
        log_end(&l);
    } else if (!strcmp(argv[0], "regs") && argc == 1) {
        cmd_regs();
    } else if (!strcmp(argv[0], "rd") && argc == 2) {
        if (!parse_u(argv[1], 0xFF, &a)) { reply_err("rd", "bad_arg", line); return; }
        uint8_t val = 0;
        int rc = fusb302_read((uint8_t)a, &val);
        log_begin(&l, "cmd");
        log_str(&l, "cmd", "rd");
        log_hex8(&l, "a", (uint8_t)a);
        if (!rc) log_hex8(&l, "v", val);
        log_bool(&l, "ok", rc == 0);
        log_end(&l);
    } else if (!strcmp(argv[0], "wr") && argc == 3) {
        if (!parse_u(argv[1], 0xFF, &a) || !parse_u(argv[2], 0xFF, &v)) { reply_err("wr", "bad_arg", line); return; }
        int rc = fusb302_write((uint8_t)a, (uint8_t)v);
        log_begin(&l, "cmd");
        log_str(&l, "cmd", "wr");
        log_hex8(&l, "a", (uint8_t)a);
        log_hex8(&l, "v", (uint8_t)v);
        log_bool(&l, "ok", rc == 0);
        log_end(&l);
    } else if (!strcmp(argv[0], "pd") && argc == 3 && !strcmp(argv[1], "want")) {
        if (!parse_u(argv[2], 48000, &v)) { reply_err("pd want", "bad_arg", line); return; }
        pd_sm_set_want(s_sm, (uint16_t)v);
        log_begin(&l, "cmd");
        log_str(&l, "cmd", "pd want");
        log_uint(&l, "mv", (uint32_t)v);
        log_bool(&l, "ok", true);
        log_end(&l);
    } else if (!strcmp(argv[0], "reboot") && argc == 1) {
        log_begin(&l, "cmd");
        log_str(&l, "cmd", "reboot");
        log_bool(&l, "ok", true);
        log_end(&l);
        vTaskDelay(pdMS_TO_TICKS(50));   /* let the reply drain */
        esp_restart();
    } else {
        reply_err(argv[0], "unknown", line);
    }
}

void console_feed(char c)
{
    if (c == '\r' || c == '\n') {
        if (s_overlong) reply_err("?", "line_too_long", "");
        else if (s_len) { s_line[s_len] = 0; console_exec(s_line); }
        s_len = 0;
        s_overlong = false;
        return;
    }
    if (!isprint((unsigned char)c)) return;
    if (s_len + 1 >= sizeof s_line) { s_overlong = true; return; }
    s_line[s_len++] = c;
}
