#include "pd_sm.h"
#include "fusb302_regs.h"
#include "log.h"

/* ---- placeholder timing ------------------------------------------------- */
#define T_ATTACH_DEBOUNCE_MS 100   /* PLACEHOLDER — real value from docs/spec.md */

/* ---- names -------------------------------------------------------------- */
static const char *const ev_names[PD_EV__COUNT] = {
    [PD_EV_NONE] = "none",
    [PD_EV_VBUS_ON] = "vbus_on",
    [PD_EV_VBUS_OFF] = "vbus_off",
    [PD_EV_TIMEOUT] = "timeout",
    [PD_EV_WANT] = "want",
};

/* ---- actions / guards (placeholders) ------------------------------------ */
static void enter_attach_wait(pd_sm_t *sm)
{
    pd_timer_start(sm, PD_TMR_STATE, T_ATTACH_DEBOUNCE_MS, PD_EV_TIMEOUT);
}

static bool g_vbus_present(pd_sm_t *sm) { return sm->vbus; }

static void act_log_want(pd_sm_t *sm)
{
    log_line_t l;
    log_begin(&l, "sm.want");
    log_uint(&l, "mv", sm->want_mv);
    log_str(&l, "state", pd_sm_state_name(sm->state));
    log_end(&l);
}

static const pd_state_desc_t states[PD_ST__COUNT] = {
    [PD_ST_UNATTACHED]  = { "Unattached",  NULL, NULL },
    [PD_ST_ATTACH_WAIT] = { "AttachWait",  enter_attach_wait, NULL },
    [PD_ST_ATTACHED]    = { "Attached",    NULL, NULL },
};

/* ---- transition table (PLACEHOLDER) -------------------------------------
 * First matching row wins: (state == cur || state == ANY) && event && guard. */
const pd_transition_t pd_sm_table[] = {
    /* state              event            guard           action        next */
    { PD_ST_UNATTACHED,  PD_EV_VBUS_ON,   NULL,           NULL,         PD_ST_ATTACH_WAIT },
    { PD_ST_ATTACH_WAIT, PD_EV_VBUS_OFF,  NULL,           NULL,         PD_ST_UNATTACHED },
    { PD_ST_ATTACH_WAIT, PD_EV_TIMEOUT,   g_vbus_present, NULL,         PD_ST_ATTACHED },
    { PD_ST_ATTACH_WAIT, PD_EV_TIMEOUT,   NULL,           NULL,         PD_ST_UNATTACHED },
    { PD_ST_ATTACHED,    PD_EV_VBUS_OFF,  NULL,           NULL,         PD_ST_UNATTACHED },
    { PD_ST_ANY,         PD_EV_WANT,      NULL,           act_log_want, PD_ST_SAME },
};
const size_t pd_sm_table_len = sizeof pd_sm_table / sizeof pd_sm_table[0];

/* ---- mechanism ---------------------------------------------------------- */
const char *pd_sm_state_name(pd_state_t s)
{
    return (unsigned)s < PD_ST__COUNT ? states[s].name : "?";
}
const char *pd_sm_event_name(pd_event_t e)
{
    return (unsigned)e < PD_EV__COUNT && ev_names[e] ? ev_names[e] : "?";
}

static uint32_t now(pd_sm_t *sm) { return sm->ops->now_ms(sm->ops->ctx); }

void pd_timer_start(pd_sm_t *sm, pd_timer_id_t id, uint32_t ms, pd_event_t ev)
{
    pd_timer_t *t = &sm->tmr[id];
    t->running = true;
    t->keep_on_exit = false;
    t->deadline = now(sm) + ms;
    t->ev = ev;
}

void pd_timer_stop(pd_sm_t *sm, pd_timer_id_t id) { sm->tmr[id].running = false; }

void pd_sm_post(pd_sm_t *sm, pd_event_t ev)
{
    uint8_t nt = (uint8_t)((sm->evq_tail + 1) % PD_EVQ_LEN);
    if (nt == sm->evq_head) {
        sm->evq_drops++;
        log_line_t l;
        log_begin(&l, "sm.qfull");
        log_str(&l, "on", pd_sm_event_name(ev));
        log_uint(&l, "drops", sm->evq_drops);
        log_end(&l);
        return;
    }
    sm->evq[sm->evq_tail] = ev;
    sm->evq_tail = nt;
}

static void log_transition(pd_state_t from, pd_state_t to, pd_event_t ev)
{
    log_line_t l;
    log_begin(&l, "sm");
    log_str(&l, "from", pd_sm_state_name(from));
    log_str(&l, "to", pd_sm_state_name(to));
    log_str(&l, "on", pd_sm_event_name(ev));
    log_end(&l);
}

bool pd_sm_dispatch(pd_sm_t *sm, pd_event_t ev)
{
    for (size_t i = 0; i < pd_sm_table_len; i++) {
        const pd_transition_t *r = &pd_sm_table[i];
        if (r->event != ev) continue;
        if (r->state != PD_ST_ANY && r->state != sm->state) continue;
        if (r->guard && !r->guard(sm)) continue;

        pd_state_t from = sm->state;
        pd_state_t to = r->next == PD_ST_SAME ? from : (pd_state_t)r->next;
        log_transition(from, to, ev);
        if (r->action) r->action(sm);
        if (r->next != PD_ST_SAME) {
            if (states[from].on_exit) states[from].on_exit(sm);
            for (int k = 0; k < PD_TMR__COUNT; k++)
                if (!sm->tmr[k].keep_on_exit) sm->tmr[k].running = false;
            sm->state = to;
            sm->entered_ms = now(sm);
            sm->transitions++;
            if (states[to].on_entry) states[to].on_entry(sm);
        }
        return true;
    }
    return false;
}

void pd_sm_init(pd_sm_t *sm, const struct pd_port_ops *ops)
{
    *sm = (pd_sm_t){0};
    sm->ops = ops;
    sm->state = PD_ST_UNATTACHED;
    sm->entered_ms = now(sm);
    log_line_t l;
    log_begin(&l, "sm.init");
    log_str(&l, "state", pd_sm_state_name(sm->state));
    log_uint(&l, "rows", (uint32_t)pd_sm_table_len);
    log_end(&l);
    if (states[sm->state].on_entry) states[sm->state].on_entry(sm);
}

void pd_sm_set_want(pd_sm_t *sm, uint16_t mv)
{
    sm->want_mv = mv;
    pd_sm_post(sm, PD_EV_WANT);
}

/* PLACEHOLDER input poll: VBUSOK edge -> event. Real inputs come from the spec. */
static void poll_hw(pd_sm_t *sm)
{
    uint8_t st0;
    if (sm->ops->reg_read(sm->ops->ctx, FUSB_STATUS0, &st0) != 0) return;
    bool v = (st0 & FUSB_ST0_VBUSOK) != 0;
    if (!sm->vbus_known || v != sm->vbus) {
        sm->vbus = v;
        sm->vbus_known = true;
        pd_sm_post(sm, v ? PD_EV_VBUS_ON : PD_EV_VBUS_OFF);
    }
}

static void service_timers(pd_sm_t *sm)
{
    uint32_t t = now(sm);
    for (int k = 0; k < PD_TMR__COUNT; k++) {
        pd_timer_t *tm = &sm->tmr[k];
        if (tm->running && (int32_t)(t - tm->deadline) >= 0) {
            tm->running = false;
            pd_sm_post(sm, tm->ev);
        }
    }
}

void pd_sm_step(pd_sm_t *sm)
{
    poll_hw(sm);
    service_timers(sm);
    /* drain; bound the loop so a self-posting action can't spin forever */
    for (int n = 0; n < PD_EVQ_LEN && sm->evq_head != sm->evq_tail; n++) {
        pd_event_t ev = sm->evq[sm->evq_head];
        sm->evq_head = (uint8_t)((sm->evq_head + 1) % PD_EVQ_LEN);
        if (!pd_sm_dispatch(sm, ev)) {
            log_line_t l;
            log_begin(&l, "sm.unhandled");
            log_str(&l, "state", pd_sm_state_name(sm->state));
            log_str(&l, "on", pd_sm_event_name(ev));
            log_end(&l);
        }
    }
}
