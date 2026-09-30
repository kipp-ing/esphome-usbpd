/* JSONL logger — one JSON object per line, always "t" (ms since boot) and "ev".
 * Pure C (no ESP-IDF includes): shared by firmware and the host sim.
 * The platform injects a clock and an output sink.
 *
 * Usage:
 *   log_line_t l; log_begin(&l, "i2c.err"); log_hex8(&l, "reg", 0x42); log_int(&l, "rc", -1); log_end(&l);
 *
 * Thread-safety: each line is built in the caller's log_line_t and handed to the
 * sink in ONE call, so lines never interleave as long as the sink write is atomic
 * (true for a single poll task; the firmware runs everything in one task).
 */
#ifndef USBPD_LOG_H
#define USBPD_LOG_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define LOG_LINE_MAX 768

typedef uint32_t (*log_clock_fn)(void);
typedef void (*log_sink_fn)(const char *line, size_t len); /* line includes trailing '\n' */

typedef struct {
    char buf[LOG_LINE_MAX];
    size_t n;
    size_t mark;      /* start of the field being written */
    bool overflow;
} log_line_t;

void log_set_clock(log_clock_fn fn);
void log_set_sink(log_sink_fn fn);   /* NULL -> stdout */
uint32_t log_now_ms(void);

void log_begin(log_line_t *l, const char *ev);
void log_int(log_line_t *l, const char *key, int32_t v);
void log_uint(log_line_t *l, const char *key, uint32_t v);
void log_bool(log_line_t *l, const char *key, bool v);
void log_str(log_line_t *l, const char *key, const char *v);        /* JSON-escaped */
void log_hex8(log_line_t *l, const char *key, uint8_t v);           /* "0x1a" */
void log_hex(log_line_t *l, const char *key, const uint8_t *p, size_t n); /* "a1b2c3" */
void log_raw(log_line_t *l, const char *key, const char *json);     /* pre-formatted JSON value */
void log_end(log_line_t *l);                                         /* emits the line */

/* one-liners */
void log_ev(const char *ev);
void log_ev_str(const char *ev, const char *key, const char *v);

#endif
