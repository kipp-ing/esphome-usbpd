/* JSONL logger — one JSON object per line, always "t" (ms since boot) and "ev".
 * Pure C (no ESP-IDF includes): shared by firmware and the host sim.
 * The platform injects a clock and an output sink.
 *
 * Usage:
 *   log_line_t l; log_begin(&l, "i2c.err"); log_hex8(&l, "reg", 0x42); log_int(&l, "rc", -1); log_end(&l);
 *
 * Thread-safety: each line is built in a buffer and handed to the sink in ONE
 * call, so lines never interleave as long as the sink write is atomic.
 *
 * Stack budget: log_line_t is a small handle (~16 B). The 768-byte line buffer
 * comes from a static pool of LOG_POOL_N slots (acquired in log_begin, released
 * in log_end), NOT from the caller's stack — a 768-B buffer per nested caller
 * overflowed the 3.5 KB main task stack (M4 bench fail, pd_drv.c log_rx).
 * The pool is NOT thread-safe: use the logger from ONE task only (the firmware
 * runs everything in app_main). Every log_begin must be paired with log_end.
 * Pool exhausted (more than LOG_POOL_N lines open at once) -> the line is
 * dropped and counted in log_pool_miss().
 */
#ifndef USBPD_LOG_H
#define USBPD_LOG_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define LOG_LINE_MAX 768
#define LOG_POOL_N 4        /* max lines open at once (nesting depth) */

typedef uint32_t (*log_clock_fn)(void);
typedef void (*log_sink_fn)(const char *line, size_t len); /* line includes trailing '\n' */

typedef struct {
    char *buf;        /* pool slot, NULL if the pool was exhausted */
    size_t n;
    size_t mark;      /* start of the field being written */
    int8_t slot;
    bool overflow;
} log_line_t;

void log_set_clock(log_clock_fn fn);
void log_set_sink(log_sink_fn fn);   /* NULL -> stdout */
uint32_t log_now_ms(void);
uint32_t log_pool_miss(void);       /* lines dropped because the pool was exhausted */
unsigned log_pool_busy(void);       /* slots currently held (0 between lines; >0 = a leaked log_begin) */

void log_begin(log_line_t *l, const char *ev);
void log_int(log_line_t *l, const char *key, int32_t v);
void log_uint(log_line_t *l, const char *key, uint32_t v);
void log_bool(log_line_t *l, const char *key, bool v);
void log_str(log_line_t *l, const char *key, const char *v);        /* JSON-escaped */
void log_hex8(log_line_t *l, const char *key, uint8_t v);           /* "0x1a" */
void log_hex(log_line_t *l, const char *key, const uint8_t *p, size_t n); /* "a1b2c3" */
void log_raw(log_line_t *l, const char *key, const char *json);     /* pre-formatted JSON value */
/* spec §7 formats: hex numbers as strings with UPPER-case digits, "0x1042" */
void log_hexn(log_line_t *l, const char *key, uint32_t v, int digits); /* "0x%0*X" */
void log_bytes(log_line_t *l, const char *key, const uint8_t *p, size_t n);   /* "12 12 0A" */
void log_hex32_arr(log_line_t *l, const char *key, const uint32_t *v, size_t n); /* ["0x0801912C",...] */
void log_uint_arr(log_line_t *l, const char *key, const uint32_t *v, size_t n);  /* [5000,9000] */
void log_end(log_line_t *l);                                         /* emits the line */

/* one-liners */
void log_ev(const char *ev);
void log_ev_str(const char *ev, const char *key, const char *v);

#endif
