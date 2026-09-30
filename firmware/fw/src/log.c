#include "log.h"
#include <stdio.h>
#include <string.h>

static log_clock_fn s_clock;
static log_sink_fn s_sink;

void log_set_clock(log_clock_fn fn) { s_clock = fn; }
void log_set_sink(log_sink_fn fn) { s_sink = fn; }
uint32_t log_now_ms(void) { return s_clock ? s_clock() : 0; }

static void put(log_line_t *l, const char *s, size_t n)
{
    /* reserve 16 bytes for ,"trunc":true}\n\0 */
    if (l->overflow) return;
    if (l->n + n + 16 > LOG_LINE_MAX) { l->overflow = true; l->n = l->mark; return; } /* drop partial field */
    memcpy(l->buf + l->n, s, n);
    l->n += n;
}
static void puts_(log_line_t *l, const char *s) { put(l, s, strlen(s)); }

static void put_escaped(log_line_t *l, const char *s)
{
    put(l, "\"", 1);
    for (; s && *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { char e[2] = {'\\', (char)c}; put(l, e, 2); }
        else if (c == '\n') put(l, "\\n", 2);
        else if (c == '\r') put(l, "\\r", 2);
        else if (c == '\t') put(l, "\\t", 2);
        else if (c < 0x20) { char e[8]; int k = snprintf(e, sizeof e, "\\u%04x", c); put(l, e, (size_t)k); }
        else put(l, (const char *)&c, 1);
    }
    put(l, "\"", 1);
}

static void key(log_line_t *l, const char *k)
{
    l->mark = l->n;
    put(l, ",", 1);
    put_escaped(l, k);
    put(l, ":", 1);
}

void log_begin(log_line_t *l, const char *ev)
{
    l->n = 0;
    l->mark = 0;
    l->overflow = false;
    char tmp[24];
    int k = snprintf(tmp, sizeof tmp, "{\"t\":%lu", (unsigned long)log_now_ms());
    put(l, tmp, (size_t)k);
    key(l, "ev");
    put_escaped(l, ev);
}

void log_int(log_line_t *l, const char *k, int32_t v)
{
    char tmp[16]; int n = snprintf(tmp, sizeof tmp, "%ld", (long)v);
    key(l, k); put(l, tmp, (size_t)n);
}
void log_uint(log_line_t *l, const char *k, uint32_t v)
{
    char tmp[16]; int n = snprintf(tmp, sizeof tmp, "%lu", (unsigned long)v);
    key(l, k); put(l, tmp, (size_t)n);
}
void log_bool(log_line_t *l, const char *k, bool v) { key(l, k); puts_(l, v ? "true" : "false"); }
void log_str(log_line_t *l, const char *k, const char *v) { key(l, k); put_escaped(l, v ? v : ""); }
void log_hex8(log_line_t *l, const char *k, uint8_t v)
{
    char tmp[8]; int n = snprintf(tmp, sizeof tmp, "\"0x%02x\"", v);
    key(l, k); put(l, tmp, (size_t)n);
}
void log_hex(log_line_t *l, const char *k, const uint8_t *p, size_t n)
{
    static const char hx[] = "0123456789abcdef";
    key(l, k);
    put(l, "\"", 1);
    for (size_t i = 0; i < n; i++) { char b[2] = {hx[p[i] >> 4], hx[p[i] & 15]}; put(l, b, 2); }
    put(l, "\"", 1);
}
void log_raw(log_line_t *l, const char *k, const char *json) { key(l, k); puts_(l, json); }

void log_end(log_line_t *l)
{
    if (l->overflow) {
        /* never emit truncated JSON: mark it instead */
        const char *m = ",\"trunc\":true";
        size_t mn = strlen(m);
        memcpy(l->buf + l->n, m, mn); /* 16-byte reserve in put() guarantees room */
        l->n += mn;
    }
    l->buf[l->n++] = '}';
    l->buf[l->n++] = '\n';
    l->buf[l->n] = 0;
    if (s_sink) s_sink(l->buf, l->n);
    else { fwrite(l->buf, 1, l->n, stdout); fflush(stdout); }
}

void log_ev(const char *ev) { log_line_t l; log_begin(&l, ev); log_end(&l); }
void log_ev_str(const char *ev, const char *k, const char *v)
{
    log_line_t l; log_begin(&l, ev); log_str(&l, k, v); log_end(&l);
}
