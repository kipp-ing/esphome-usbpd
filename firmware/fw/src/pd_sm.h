/* Table-driven PD sink state machine — SKELETON (phase 1).
 * Pure C: no ESP-IDF includes. Hardware only via struct pd_port_ops so the
 * host sim (test/host) can supply a fake FUSB302.
 *
 * States/events/transitions below are PLACEHOLDERS proving the mechanism;
 * the real ones come from docs/spec.md.
 */
#ifndef USBPD_PD_SM_H
#define USBPD_PD_SM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ---- hardware interface ------------------------------------------------ */
struct pd_port_ops {
    void *ctx;
    int (*reg_read)(void *ctx, uint8_t reg, uint8_t *val);          /* 0 = ok */
    int (*reg_write)(void *ctx, uint8_t reg, uint8_t val);
    int (*fifo_read)(void *ctx, uint8_t *buf, size_t n);             /* RX FIFO */
    int (*fifo_write)(void *ctx, const uint8_t *buf, size_t n);      /* TX FIFO */
    uint32_t (*now_ms)(void *ctx);                                   /* monotonic ms */
};

/* ---- states / events (placeholders) ------------------------------------ */
typedef enum {
    PD_ST_UNATTACHED = 0,
    PD_ST_ATTACH_WAIT,
    PD_ST_ATTACHED,
    PD_ST__COUNT,
    PD_ST_ANY  = 0xFE,   /* table wildcard: matches every state */
    PD_ST_SAME = 0xFF,   /* table next: internal transition, no state change */
} pd_state_t;

typedef enum {
    PD_EV_NONE = 0,
    PD_EV_VBUS_ON,
    PD_EV_VBUS_OFF,
    PD_EV_TIMEOUT,        /* generic state timer expired */
    PD_EV_WANT,           /* console "pd want <mV>" */
    PD_EV__COUNT,
} pd_event_t;

/* ---- timers ------------------------------------------------------------- */
typedef enum {
    PD_TMR_STATE = 0,     /* generic per-state timer */
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
    /* inputs / cached hardware view */
    uint16_t want_mv;
    bool vbus;
    bool vbus_known;
    uint32_t transitions;
};

void pd_sm_init(pd_sm_t *sm, const struct pd_port_ops *ops);
void pd_sm_post(pd_sm_t *sm, pd_event_t ev);
bool pd_sm_dispatch(pd_sm_t *sm, pd_event_t ev);   /* true if a table row fired */
void pd_sm_step(pd_sm_t *sm);                      /* poll hw + timers + drain queue */
void pd_sm_set_want(pd_sm_t *sm, uint16_t mv);

void pd_timer_start(pd_sm_t *sm, pd_timer_id_t id, uint32_t ms, pd_event_t ev);
void pd_timer_stop(pd_sm_t *sm, pd_timer_id_t id);

const char *pd_sm_state_name(pd_state_t s);
const char *pd_sm_event_name(pd_event_t e);

/* exposed for tests */
extern const pd_transition_t pd_sm_table[];
extern const size_t pd_sm_table_len;

#endif
