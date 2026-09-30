/* Table-driven PD sink state machine (docs/spec.md §3-§5), L3 policy + Type-C.
 * Pure C: no ESP-IDF includes. Hardware only via struct pd_port_ops so the
 * host sim (test/host) can supply a fake FUSB302. The L1 driver (pd_drv.c)
 * keeps its protocol state in this struct too. */
#ifndef USBPD_PD_SM_H
#define USBPD_PD_SM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "pd_msg.h"
#include "log.h"

/* ---- hardware interface ------------------------------------------------ */
struct pd_port_ops {
    void *ctx;
    int (*reg_read)(void *ctx, uint8_t reg, uint8_t *val);          /* 0 = ok */
    int (*reg_read_burst)(void *ctx, uint8_t reg, uint8_t *buf, size_t n); /* auto-increment, never past 0x42 */
    int (*reg_write)(void *ctx, uint8_t reg, uint8_t val);
    int (*fifo_read)(void *ctx, uint8_t *buf, size_t n);             /* RX FIFO */
    int (*fifo_write)(void *ctx, const uint8_t *buf, size_t n);      /* TX FIFO, one burst */
    uint32_t (*now_ms)(void *ctx);                                   /* monotonic ms */
};

/* ---- states (§3) -------------------------------------------------------- */
typedef enum {
    PD_ST_UNATTACHED = 0,
    PD_ST_ATTACH_WAIT,
    PD_ST_ATTACHED,
    PD_ST_STARTUP,
    PD_ST_DISCOVERY,
    PD_ST_WAIT_CAPS,
    PD_ST_EVAL_CAP,
    PD_ST_SELECT_CAP,
    PD_ST_TRANS_SINK,
    PD_ST_READY,
    PD_ST_GIVE_SINK_CAP,
    PD_ST_SEND_SOFT_RESET,
    PD_ST_SOFT_RESET,
    PD_ST_HARD_RESET,
    PD_ST_TRANS_DEFAULT,
    PD_ST_DISABLED,
    PD_ST__COUNT,
    PD_ST_ANY  = 0xFE,   /* table wildcard: matches every state */
    PD_ST_SAME = 0xFF,   /* table next: internal transition, no state change */
} pd_state_t;

/* ---- events (§4.1) ------------------------------------------------------ */
typedef enum {
    PD_EV_NONE = 0,
    PD_EV_VBUS_ON,
    PD_EV_VBUS_OFF,
    PD_EV_CC_TICK,
    PD_EV_DEBOUNCE,
    PD_EV_DONE,
    PD_EV_TIMEOUT,
    PD_EV_SINKREQ,
    PD_EV_TX_SENT,
    PD_EV_TX_FAIL,
    PD_EV_HARD_SENT,
    PD_EV_HR_RX,
    PD_EV_GIVE_UP,
    PD_EV_WANT,
    PD_EV_RX_SRC_CAPS,
    PD_EV_RX_ACCEPT,
    PD_EV_RX_REJECT,
    PD_EV_RX_WAIT,
    PD_EV_RX_PS_RDY,
    PD_EV_RX_SOFT_RESET,
    PD_EV_RX_GET_SINK_CAP,
    PD_EV_RX_PING,
    PD_EV_RX_GOTOMIN,
    PD_EV_RX_UNEXPECTED,
    PD_EV_RX_UNSUPPORTED,
    PD_EV__COUNT,
} pd_event_t;

/* ---- timers (§5). Order matters: CC is serviced before TC in one tick. --- */
typedef enum {
    PD_TMR_CC = 0,        /* CC sample, 5 ms -> CC_TICK */
    PD_TMR_TC,            /* tCCDebounce, 100 ms -> DEBOUNCE */
    PD_TMR_PE,            /* policy-engine timer -> TIMEOUT */
    PD_TMR_SINKREQ,       /* tSinkRequest, 100 ms -> SINKREQ */
    PD_TMR__COUNT,
} pd_timer_id_t;

typedef struct {
    bool running;
    bool keep_on_exit;    /* false: cancelled automatically on state change */
    uint32_t deadline;
    pd_event_t ev;        /* event posted on expiry */
} pd_timer_t;

/* ---- machine ------------------------------------------------------------ */
#define PD_EVQ_LEN 16
#define PD_MAX_OBJ 7

typedef struct pd_sm pd_sm_t;
typedef bool (*pd_guard_fn)(pd_sm_t *sm);
typedef void (*pd_action_fn)(pd_sm_t *sm);

typedef struct {
    uint8_t state;        /* pd_state_t or PD_ST_ANY */
    uint8_t event;        /* pd_event_t */
    pd_guard_fn guard;    /* NULL = always */
    pd_action_fn action;  /* NULL = none; runs before the state change */
    uint8_t next;         /* pd_state_t or PD_ST_SAME */
} pd_transition_t;

typedef struct {
    const char *name;
    pd_action_fn on_entry;  /* NULL = none */
    pd_action_fn on_exit;
} pd_state_desc_t;

struct pd_sm {
    const struct pd_port_ops *ops;
    pd_state_t state;
    uint32_t entered_ms;
    pd_timer_t tmr[PD_TMR__COUNT];
    pd_event_t evq[PD_EVQ_LEN];
    uint8_t evq_head, evq_tail;
    uint32_t evq_drops;
    uint32_t transitions;

    /* inputs / cached hardware view */
    uint32_t want_mv;
    bool vbus, vbus_known;
    uint8_t st0, st1;           /* this step's Status0 / Status1 */

    /* CC sampler (§3.1) */
    uint8_t meas;               /* 1 or 2 */
    uint8_t lvl[3];             /* [1],[2]; 0xFF = no sample */
    uint8_t n[3];
    uint8_t orient;             /* 0 none, 1 CC1, 2 CC2 */

    /* last received PE message + stored source caps */
    uint16_t rx_hdr;
    uint8_t rx_ndo, rx_type;
    uint32_t rx_obj[PD_MAX_OBJ];
    uint32_t caps[PD_MAX_OBJ];
    uint8_t caps_n;

    /* policy */
    pd_sel_t sel;
    bool have_contract;
    uint8_t con_pos;
    uint32_t con_mv, con_ma;
    uint8_t hr_count;
    bool reject_fallback, sinkreq_pending;

    /* L1 driver / protocol layer (§2) */
    uint8_t tx_msgid;
    int8_t rx_last_id;          /* -1 = none */
    bool tx_pending, tx_coll;
    uint8_t tx_id;
    uint32_t tx_deadline;
    uint8_t tx_buf[PD_TX_BURST_MAX];
    size_t tx_len;
};

void pd_sm_init(pd_sm_t *sm, const struct pd_port_ops *ops);
void pd_sm_post(pd_sm_t *sm, pd_event_t ev);
bool pd_sm_dispatch(pd_sm_t *sm, pd_event_t ev);   /* true if a table row fired */
void pd_sm_step(pd_sm_t *sm);                      /* poll hw + timers + drain queue */
void pd_sm_set_want(pd_sm_t *sm, uint32_t mv);

void pd_timer_start(pd_sm_t *sm, pd_timer_id_t id, uint32_t ms, pd_event_t ev);
void pd_timer_stop(pd_sm_t *sm, pd_timer_id_t id);

const char *pd_sm_state_name(pd_state_t s);
const char *pd_sm_event_name(pd_event_t e);

/* hb fields owned by the SM (§7): state, vbus, want, mv. */
void pd_sm_hb_fields(const pd_sm_t *sm, log_line_t *l);

/* exposed for tests */
extern const pd_transition_t pd_sm_table[];
extern const size_t pd_sm_table_len;

#endif
