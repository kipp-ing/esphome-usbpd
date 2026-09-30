/* Fake FUSB302 for the host sim (docs/spec.md §8.1 F1-F7): register model,
 * RX/TX FIFO, per-CC BC_LVL, flush/reset semantics, TX burst capture with
 * auto-ack, controllable clock, tick-stamped log capture.
 * Implements struct pd_port_ops. */
#ifndef FAKE_PORT_H
#define FAKE_PORT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "pd_sm.h"

#define FAKE_TX_HIST 128
#define FAKE_BURST_MAX 64

typedef enum {            /* F6 auto-ack modes, applied per TX burst */
    ACK_NONE = 0,
    ACK_TXSENT,           /* Interrupta.I_TXSENT */
    ACK_GCRC,             /* push a GoodCRC frame with the burst's MessageID */
    ACK_BOTH,             /* both of the above (default) */
    ACK_RETRYFAIL,        /* Interrupta.I_RETRYFAIL */
    ACK_COLL,             /* Interrupt.I_COLLISION once, then acts as BOTH */
} fake_ack_t;

typedef struct {
    uint32_t tick;
    uint8_t b[FAKE_BURST_MAX];
    size_t n;
} fake_burst_t;

typedef struct {
    bool active;
    uint32_t at;
    fake_ack_t mode;
    uint8_t id;
} fake_pending_ack_t;

typedef struct {
    uint32_t now;
    uint8_t reg[256];
    uint8_t cc_lvl[3];                          /* F1: [1] CC1, [2] CC2 BC_LVL (0..3) */
    uint8_t rx[1024]; size_t rx_len, rx_pos;    /* bytes the SM reads from FIFOS */
    fake_burst_t tx[FAKE_TX_HIST];              /* F5: ring of TX bursts */
    uint32_t ntx;                               /* total bursts written */
    uint8_t wlast[256];                         /* last value written per register */
    uint32_t wcount[256];                       /* writes per register */
    uint32_t sw_resets, pd_resets;              /* F4 */
    fake_ack_t ack_mode;                        /* F6 */
    uint32_t ack_delay;
    bool coll_done;
    fake_pending_ack_t pend[8];
    uint32_t last_rxpush;                       /* tick of the last test RX push */
    uint32_t reads, writes;   /* register-level accesses (FIFO bytes counted singly) */
    uint32_t xfers;           /* I2C transactions (a FIFO burst is one) */
    uint32_t bus_bytes;       /* bytes on the wire incl. address/register bytes */
    int fail_next;                              /* >0: next N accesses return -1 */
} fake_port_t;

void fake_port_init(fake_port_t *f, struct pd_port_ops *ops);
void fake_rx_push(fake_port_t *f, const uint8_t *b, size_t n);
void fake_tick(fake_port_t *f);                 /* apply auto-acks due at f->now */
void fake_set_ack(fake_port_t *f, fake_ack_t mode, uint32_t delay);
const fake_burst_t *fake_last_tx(const fake_port_t *f);   /* NULL if none */
uint8_t fake_status0(const fake_port_t *f);     /* Status0 as the SM would read it */

/* ---- F7: tick-stamped log capture (installed as the log sink + clock) ---- */
void logcap_install(fake_port_t *f);
void logcap_clear(void);
void logcap_echo(int on);                       /* also print lines to stdout */
void logcap_dump(void);                         /* print all captured lines */
int logcap_nlines_total(void);
const char *logcap_line(int i);
uint32_t logcap_tick(int i);
int logcap_count(const char *needle);           /* lines containing substring */
/* First line index >= from whose ev is exactly `ev` (NULL = any) and that contains
 * every non-NULL fragment; -1 if none. */
int logcap_find(int from, const char *ev, const char *f1, const char *f2);
int logcap_nlines(const char *ev, const char *f1, const char *f2);

#endif
