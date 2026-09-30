/* Scenario engine for the host sim (docs/spec.md §8).
 *
 * Two ways to drive it:
 *  1. Incremental C API (most tests): scn_begin() / scn_boot() / scn_tick(n) /
 *     stimuli helpers / SCN_CHECK(...) / scn_end().
 *  2. Table runner scn_run(): a timeline of ops (SETREG, SETCC, RXPUSH, ACK,
 *     EXPECT_* ...) played with a 1 ms tick, built on the same engine.
 *
 * Tick model: scn_tick() advances fake time by 1 ms, applies due auto-acks
 * (fake_tick) and runs one pd_sm_step(). Stimuli applied between ticks are
 * seen by the next step. Boot (pd_sm_init) runs at t = 0. */
#ifndef SCENARIO_H
#define SCENARIO_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "pd_sm.h"
#include "fake_port.h"

typedef struct {
    fake_port_t f;
    struct pd_port_ops ops;
    pd_sm_t sm;
    const char *name;
    int fails;
    uint32_t io_max;          /* max I2C transactions in one step */
    uint32_t bytes_max;       /* max I2C bus bytes in one step */
} scn_t;

extern scn_t S;
extern uint32_t g_io_max_all, g_bytes_max_all;   /* across all scenarios */
extern int scn_verbose;

void scn_begin(const char *name);
void scn_boot(void);
void scn_tick(uint32_t n);
/* Ticks until the sm is in `st` (max `limit` ticks); returns ticks used or -1. */
int  scn_until_state(pd_state_t st, uint32_t limit);
int  scn_end(void);           /* prints ok/FAIL (+ log on failure); returns fails */
void scn_fail(const char *file, int line, const char *what);
#define SCN_CHECK(c) do { if (!(c)) scn_fail(__FILE__, __LINE__, #c); } while (0)

/* stimuli */
void scn_vbus(bool on);
void scn_setcc(uint8_t lvl1, uint8_t lvl2);
void scn_setbits(uint8_t reg, uint8_t v);
void scn_clrbits(uint8_t reg, uint8_t v);
void scn_push(const uint8_t *b, size_t n);
/* Push an SOP frame: E0 hdr objs... crc x4. */
void scn_push_msg(uint16_t hdr, const uint32_t *obj, uint8_t ndo);
/* Source header (R3, Source, DFP): 0x01A0 | type | id<<9 | ndo<<12. */
uint16_t scn_src_hdr(uint8_t type, uint8_t id, uint8_t ndo);

/* checks */
bool scn_last_tx_is(const uint8_t *b, size_t n);
bool scn_tx_within(uint32_t ticks);   /* last burst tick - last RX push tick <= ticks */
/* line index of the first `a` match < first `b` match (both must exist) */
bool scn_log_order(const char *ev_a, const char *fa, const char *ev_b, const char *fb);

/* common prologue §8.2 ("attach CC1"): boot, SETCC(2,0), VBUSOK, 120 ticks */
void scn_prologue(void);
/* D1 frame (§6.5) with message id `id` */
void scn_push_d1(uint8_t id);
/* prologue + D1 + Accept + PS_RDY -> Ready with a 5 V contract (T07 end state).
 * Source message ids used: 0, 1, 2. */
void scn_contract_5v(void);

/* ---- table runner ------------------------------------------------------- */
typedef enum {
    SCN_END = 0,
    SCN_SETREG,        /* reg[reg] = val */
    SCN_SETBITS,       /* reg[reg] |= val */
    SCN_CLRBITS,       /* reg[reg] &= ~val */
    SCN_SETCC,         /* cc_lvl[1] = val, cc_lvl[2] = val2 (F1) */
    SCN_RXPUSH,        /* append bytes[0..n) to RX FIFO */
    SCN_POST,          /* pd_sm_post(event) */
    SCN_WANT,          /* pd_sm_set_want(val16) */
    SCN_ACK,           /* fake_set_ack(val, val16 delay) (F6) */
    SCN_EXPECT_STATE,  /* sm.state == state */
    SCN_EXPECT_LOG,    /* captured log contains text */
    SCN_EXPECT_REG,    /* reg[reg] == val (what the SM wrote) */
    SCN_EXPECT_TX,     /* last TX burst == bytes[0..n) (F5) */
    SCN_EXPECT_NTX,    /* bursts written == val16 (F5) */
    SCN_EXPECT_LOG_ORDER, /* first line containing text before first containing text2 (F7) */
    SCN_EXPECT_TX_WITHIN, /* last burst tick - last RX push tick <= val16 (F7) */
} scn_op_t;

typedef struct {
    uint32_t at_ms;    /* absolute sim time the step applies at */
    scn_op_t op;
    uint8_t reg, val, val2;
    uint16_t val16;
    pd_state_t state;
    pd_event_t event;
    const char *text, *text2;
    uint8_t bytes[64];
    size_t n;
} scn_step_t;

/* Boots, then runs until the last step's time (+ tail_ms). Stimuli at time t
 * are applied before the step of tick t; expectations are checked after it.
 * Steps at t = 0 are applied before boot. Returns fails. */
int scn_run(const char *name, const scn_step_t *steps, uint32_t tail_ms);

#endif
