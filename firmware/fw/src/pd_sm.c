/* L3 policy + Type-C sink state machine (docs/spec.md §3-§5). */
#include "pd_sm.h"
#include "pd_drv.h"
#include "fusb302_regs.h"
#include "log.h"

/* ---- timing (§5) --------------------------------------------------------- */
#define T_CC_SAMPLE      5
#define T_CC_DEBOUNCE    100
#define T_DISCOVERY      2500
#define T_WAIT_CAPS      600
#define T_SENDER_RESP    30
#define T_PS_TRANSITION  550
#define T_SINK_REQUEST   100
#define T_HR_GUARD       10
#define T_TRANS_DEFAULT  1000
#define N_HARD_RESET     2          /* hr_count > 2 -> give up */

/* ---- names -------------------------------------------------------------- */
static const char *const ev_names[PD_EV__COUNT] = {
    [PD_EV_NONE] = "NONE",
    [PD_EV_VBUS_ON] = "VBUS_ON",
    [PD_EV_VBUS_OFF] = "VBUS_OFF",
    [PD_EV_CC_TICK] = "CC_TICK",
    [PD_EV_DEBOUNCE] = "DEBOUNCE",
    [PD_EV_DONE] = "DONE",
    [PD_EV_TIMEOUT] = "TIMEOUT",
    [PD_EV_SINKREQ] = "SINKREQ",
    [PD_EV_TX_SENT] = "TX_SENT",
    [PD_EV_TX_FAIL] = "TX_FAIL",
    [PD_EV_HARD_SENT] = "HARD_SENT",
    [PD_EV_HR_RX] = "HR_RX",
    [PD_EV_GIVE_UP] = "GIVE_UP",
    [PD_EV_WANT] = "WANT",
    [PD_EV_RX_SRC_CAPS] = "RX_SRC_CAPS",
    [PD_EV_RX_ACCEPT] = "RX_ACCEPT",
    [PD_EV_RX_REJECT] = "RX_REJECT",
    [PD_EV_RX_WAIT] = "RX_WAIT",
    [PD_EV_RX_PS_RDY] = "RX_PS_RDY",
    [PD_EV_RX_SOFT_RESET] = "RX_SOFT_RESET",
    [PD_EV_RX_GET_SINK_CAP] = "RX_GET_SINK_CAP",
    [PD_EV_RX_PING] = "RX_PING",
    [PD_EV_RX_GOTOMIN] = "RX_GOTOMIN",
    [PD_EV_RX_UNEXPECTED] = "RX_UNEXPECTED",
    [PD_EV_RX_UNSUPPORTED] = "RX_UNSUPPORTED",
};

static uint32_t now(pd_sm_t *sm) { return sm->ops->now_ms(sm->ops->ctx); }
static void wr(pd_sm_t *sm, uint8_t reg, uint8_t v) { drv_write(sm, reg, v); }
static void clear_int(pd_sm_t *sm)
{
    uint8_t d;
    drv_read(sm, FUSB_INTERRUPTA, &d);
    drv_read(sm, FUSB_INTERRUPTB, &d);
    drv_read(sm, FUSB_INTERRUPT, &d);
}
static void log_why(const char *ev, const char *why)
{
    log_line_t l;
    log_begin(&l, ev);
    log_str(&l, "why", why);
    log_end(&l);
}
static int lvl_i(uint8_t v) { return v == 0xFF ? -1 : v; }
static void cc_clear(pd_sm_t *sm)
{
    sm->lvl[1] = sm->lvl[2] = 0xFF;
    sm->n[1] = sm->n[2] = 0;
}

/* ---- guards (§4.2) ------------------------------------------------------ */
static bool g_vbus(pd_sm_t *sm) { return sm->vbus; }
static bool g_cc_ok(pd_sm_t *sm)
{
    bool a = sm->lvl[1] != 0, b = sm->lvl[2] != 0;
    return a != b && sm->n[1] >= 2 && sm->n[2] >= 2;
}
static bool g_cc_nok(pd_sm_t *sm) { return !g_cc_ok(sm); }
static bool g_contract(pd_sm_t *sm) { return sm->have_contract; }
static bool g_no_contract(pd_sm_t *sm) { return !sm->have_contract; }
static bool g_tx_pending(pd_sm_t *sm) { return sm->tx_pending; }
static bool g_no_tx_pending(pd_sm_t *sm) { return !sm->tx_pending; }
static bool g_caps(pd_sm_t *sm) { return sm->caps_n >= 1; }

/* ---- actions (§4.2) ----------------------------------------------------- */
static void a_cc_sample(pd_sm_t *sm)
{
    if ((sm->st0 & FUSB_ST0_ACTIVITY) == 0) {
        uint8_t bc = sm->st0 & FUSB_ST0_BC_LVL_MASK, m = sm->meas;
        if (bc == sm->lvl[m]) { if (sm->n[m] < 255) sm->n[m]++; }
        else { sm->lvl[m] = bc; sm->n[m] = 1; }
        wr(sm, FUSB_SWITCHES0, m == 1 ? 0x0B : 0x07);
        sm->meas = m == 1 ? 2 : 1;
    }
    pd_timer_start(sm, PD_TMR_CC, T_CC_SAMPLE, PD_EV_CC_TICK);
}
static void a_cc_retry(pd_sm_t *sm)
{
    log_line_t l;
    log_begin(&l, "cc.retry");
    log_int(&l, "lvl1", lvl_i(sm->lvl[1]));
    log_int(&l, "lvl2", lvl_i(sm->lvl[2]));
    log_uint(&l, "n1", sm->n[1]);
    log_uint(&l, "n2", sm->n[2]);
    log_end(&l);
    cc_clear(sm);
    pd_timer_start(sm, PD_TMR_TC, T_CC_DEBOUNCE, PD_EV_DEBOUNCE);
}
static void a_orient(pd_sm_t *sm) { sm->orient = sm->lvl[1] != 0 ? 1 : 2; }
static void a_start_sr(pd_sm_t *sm) { pd_timer_start(sm, PD_TMR_PE, T_SENDER_RESP, PD_EV_TIMEOUT); }
static void a_reject_nc(pd_sm_t *sm)
{
    if (sm->sel.mv > 5000) sm->reject_fallback = true;
    log_line_t l;
    log_begin(&l, "pd.rej");
    log_uint(&l, "mv", sm->sel.mv);
    log_end(&l);
}
static void a_wait_c(pd_sm_t *sm) { sm->sinkreq_pending = true; }
static void a_contract(pd_sm_t *sm)
{
    sm->have_contract = true;
    sm->con_pos = sm->sel.pos;
    sm->con_mv = sm->sel.mv;
    sm->con_ma = sm->sel.ma;
    sm->hr_count = 0;
    sm->reject_fallback = false;
    log_line_t l;
    log_begin(&l, "contract");
    log_uint(&l, "mv", sm->con_mv);
    log_uint(&l, "ma", sm->con_ma);
    log_uint(&l, "pos", sm->con_pos);
    log_end(&l);
}
static void a_tx_reject(pd_sm_t *sm) { drv_tx(sm, PD_CTRL_REJECT, 0, NULL); }
static void a_lost_detach(pd_sm_t *sm)
{
    /* Clears have_contract so Unattached entry does not log it a second time
     * (docs/spec-questions.md). */
    if (sm->have_contract) log_why("contract.lost", "detach");
    sm->have_contract = false;
}
static void a_log_want(pd_sm_t *sm)
{
    log_line_t l;
    log_begin(&l, "pd.want");
    log_uint(&l, "mv", sm->want_mv);
    log_end(&l);
}

/* ---- state entries (§3.1) ----------------------------------------------- */
static void e_unattached(pd_sm_t *sm)
{
    wr(sm, FUSB_SWITCHES1, 0x20);
    wr(sm, FUSB_SWITCHES0, 0x03);
    wr(sm, FUSB_CONTROL0, 0x64);
    wr(sm, FUSB_CONTROL1, 0x04);
    wr(sm, FUSB_RESET, FUSB_RESET_PD_RESET);
    if (sm->have_contract) log_why("contract.lost", "detach");
    sm->caps_n = 0;
    sm->have_contract = false;
    sm->orient = 0;
    sm->reject_fallback = false;
    sm->sinkreq_pending = false;
    sm->hr_count = 0;
    proto_reset(sm);
}
static void e_attach_wait(pd_sm_t *sm)
{
    wr(sm, FUSB_SWITCHES0, 0x07);
    sm->meas = 1;
    cc_clear(sm);
    pd_timer_start(sm, PD_TMR_TC, T_CC_DEBOUNCE, PD_EV_DEBOUNCE);
    pd_timer_start(sm, PD_TMR_CC, T_CC_SAMPLE, PD_EV_CC_TICK);
}
static void e_attached(pd_sm_t *sm)
{
    uint8_t sw0 = sm->orient == 2 ? 0x0B : 0x07, sw1 = sm->orient == 2 ? 0x26 : 0x25;
    wr(sm, FUSB_SWITCHES0, sw0);
    wr(sm, FUSB_SWITCHES1, sw1);
    wr(sm, FUSB_CONTROL0, 0x64);
    wr(sm, FUSB_CONTROL1, 0x04);
    wr(sm, FUSB_RESET, FUSB_RESET_PD_RESET);
    clear_int(sm);
    log_line_t l;
    log_begin(&l, "cc");
    log_uint(&l, "cc", sm->orient);
    log_int(&l, "lvl1", lvl_i(sm->lvl[1]));
    log_int(&l, "lvl2", lvl_i(sm->lvl[2]));
    log_end(&l);
    log_begin(&l, "pd.cfg");
    log_hexn(&l, "sw0", sw0, 2);
    log_hexn(&l, "sw1", sw1, 2);
    log_end(&l);
    pd_sm_post(sm, PD_EV_DONE);
}
static void e_startup(pd_sm_t *sm)
{
    proto_reset(sm);
    sm->caps_n = 0;
    sm->have_contract = false;
    pd_sm_post(sm, PD_EV_DONE);
}
static void e_discovery(pd_sm_t *sm)
{
    pd_timer_start(sm, PD_TMR_PE, T_DISCOVERY, PD_EV_TIMEOUT);
    if (sm->vbus) pd_sm_post(sm, PD_EV_DONE);
}
static void e_wait_caps(pd_sm_t *sm) { pd_timer_start(sm, PD_TMR_PE, T_WAIT_CAPS, PD_EV_TIMEOUT); }
static void e_eval_cap(pd_sm_t *sm)
{
    sm->sel = pd_select(sm->caps, sm->caps_n, sm->want_mv, sm->reject_fallback);
    log_line_t l;
    log_begin(&l, "pd.sel");
    log_uint(&l, "want", sm->want_mv);
    log_uint(&l, "pos", sm->sel.pos);
    log_uint(&l, "mv", sm->sel.mv);
    log_uint(&l, "ma", sm->sel.ma);
    log_str(&l, "why", pd_why_name(sm->sel.why));
    log_end(&l);
    pd_sm_post(sm, PD_EV_DONE);
}
static void e_select_cap(pd_sm_t *sm)
{
    uint32_t rdo = pd_rdo_fixed(sm->sel.pos, sm->sel.ma);
    drv_tx(sm, PD_DATA_REQUEST, 1, &rdo);
}
static void e_trans_sink(pd_sm_t *sm) { pd_timer_start(sm, PD_TMR_PE, T_PS_TRANSITION, PD_EV_TIMEOUT); }
static void e_ready(pd_sm_t *sm)
{
    if (sm->sinkreq_pending) {
        sm->sinkreq_pending = false;
        pd_timer_start(sm, PD_TMR_SINKREQ, T_SINK_REQUEST, PD_EV_SINKREQ);
    }
}
static void e_give_sink_cap(pd_sm_t *sm)
{
    uint32_t pdos[2];
    uint8_t n = pd_sink_caps(sm->want_mv, pdos);
    drv_tx(sm, PD_DATA_SINK_CAP, n, pdos);
}
static void log_sr(const char *dir)
{
    log_line_t l;
    log_begin(&l, "pd.sr");
    log_str(&l, "dir", dir);
    log_end(&l);
}
static void e_send_soft_reset(pd_sm_t *sm)
{
    proto_reset(sm);
    drv_tx(sm, PD_CTRL_SOFT_RESET, 0, NULL);
    log_sr("tx");
}
static void e_soft_reset(pd_sm_t *sm)
{
    proto_reset(sm);
    drv_tx(sm, PD_CTRL_ACCEPT, 0, NULL);
    log_sr("rx");
}
static void e_hard_reset(pd_sm_t *sm)
{
    if (sm->hr_count > N_HARD_RESET) { pd_sm_post(sm, PD_EV_GIVE_UP); return; }
    sm->hr_count++;
    drv_hard_reset(sm);
    log_line_t l;
    log_begin(&l, "pd.hr");
    log_str(&l, "dir", "tx");
    log_uint(&l, "n", sm->hr_count);
    log_end(&l);
    pd_timer_start(sm, PD_TMR_PE, T_HR_GUARD, PD_EV_TIMEOUT);
}
static void e_trans_default(pd_sm_t *sm)
{
    wr(sm, FUSB_RESET, FUSB_RESET_PD_RESET);
    wr(sm, FUSB_CONTROL1, 0x04);
    wr(sm, FUSB_CONTROL0, 0x64);
    wr(sm, FUSB_CONTROL3, 0x07);
    proto_reset(sm);
    if (sm->have_contract) log_why("contract.lost", "hardreset");
    sm->have_contract = false;
    sm->caps_n = 0;
    pd_timer_start(sm, PD_TMR_PE, T_TRANS_DEFAULT, PD_EV_TIMEOUT);
}
static void e_disabled(pd_sm_t *sm)
{
    log_line_t l;
    log_begin(&l, "pd.giveup");
    log_uint(&l, "n", sm->hr_count);
    log_end(&l);
    if (sm->have_contract) log_why("contract.lost", "giveup");
    sm->have_contract = false;
}

static const pd_state_desc_t states[PD_ST__COUNT] = {
    [PD_ST_UNATTACHED]      = { "Unattached",    e_unattached, NULL },
    [PD_ST_ATTACH_WAIT]     = { "AttachWait",    e_attach_wait, NULL },
    [PD_ST_ATTACHED]        = { "Attached",      e_attached, NULL },
    [PD_ST_STARTUP]         = { "Startup",       e_startup, NULL },
    [PD_ST_DISCOVERY]       = { "Discovery",     e_discovery, NULL },
    [PD_ST_WAIT_CAPS]       = { "WaitCaps",      e_wait_caps, NULL },
    [PD_ST_EVAL_CAP]        = { "EvalCap",       e_eval_cap, NULL },
    [PD_ST_SELECT_CAP]      = { "SelectCap",     e_select_cap, NULL },
    [PD_ST_TRANS_SINK]      = { "TransSink",     e_trans_sink, NULL },
    [PD_ST_READY]           = { "Ready",         e_ready, NULL },
    [PD_ST_GIVE_SINK_CAP]   = { "GiveSinkCap",   e_give_sink_cap, NULL },
    [PD_ST_SEND_SOFT_RESET] = { "SendSoftReset", e_send_soft_reset, NULL },
    [PD_ST_SOFT_RESET]      = { "SoftReset",     e_soft_reset, NULL },
    [PD_ST_HARD_RESET]      = { "HardReset",     e_hard_reset, NULL },
    [PD_ST_TRANS_DEFAULT]   = { "TransDefault",  e_trans_default, NULL },
    [PD_ST_DISABLED]        = { "Disabled",      e_disabled, NULL },
};

/* ---- transition table (§4.3; 64 lines -> 85 rows) -----------------------
 * First matching row wins: (state == cur || state == ANY) && event && guard. */
#define S(x) PD_ST_##x
#define E(x) PD_EV_##x
const pd_transition_t pd_sm_table[] = {
    /*  #  state             event                guard            action         next */
    /* 1*/ { S(UNATTACHED),  E(VBUS_ON),          NULL,            NULL,          S(ATTACH_WAIT) },
    /* 2*/ { S(UNATTACHED),  E(VBUS_OFF),         NULL,            NULL,          S(SAME) },
           { S(UNATTACHED),  E(HR_RX),            NULL,            NULL,          S(SAME) },
    /* 3*/ { S(ATTACH_WAIT), E(CC_TICK),          NULL,            a_cc_sample,   S(SAME) },
    /* 4*/ { S(ATTACH_WAIT), E(DEBOUNCE),         g_cc_ok,         a_orient,      S(ATTACHED) },
    /* 5*/ { S(ATTACH_WAIT), E(DEBOUNCE),         g_cc_nok,        a_cc_retry,    S(SAME) },
    /* 6*/ { S(ATTACH_WAIT), E(VBUS_OFF),         NULL,            NULL,          S(UNATTACHED) },
    /* 7*/ { S(ATTACH_WAIT), E(HR_RX),            NULL,            NULL,          S(SAME) },
    /* 8*/ { S(ATTACHED),    E(DONE),             NULL,            NULL,          S(STARTUP) },
    /* 9*/ { S(ATTACHED),    E(HR_RX),            NULL,            NULL,          S(SAME) },
    /*10*/ { S(STARTUP),     E(DONE),             NULL,            NULL,          S(DISCOVERY) },
    /*11*/ { S(STARTUP),     E(VBUS_OFF),         NULL,            NULL,          S(SAME) },
    /*12*/ { S(DISCOVERY),   E(DONE),             g_vbus,          NULL,          S(WAIT_CAPS) },
    /*13*/ { S(DISCOVERY),   E(VBUS_ON),          NULL,            NULL,          S(WAIT_CAPS) },
    /*14*/ { S(DISCOVERY),   E(VBUS_OFF),         NULL,            NULL,          S(SAME) },
    /*15*/ { S(DISCOVERY),   E(TIMEOUT),          NULL,            NULL,          S(UNATTACHED) },
    /*16*/ { S(WAIT_CAPS),   E(RX_SRC_CAPS),      NULL,            NULL,          S(EVAL_CAP) },
    /*17*/ { S(WAIT_CAPS),   E(TIMEOUT),          NULL,            NULL,          S(HARD_RESET) },
    /*18*/ { S(WAIT_CAPS),   E(VBUS_ON),          NULL,            NULL,          S(SAME) },
    /*19*/ { S(EVAL_CAP),    E(DONE),             NULL,            NULL,          S(SELECT_CAP) },
    /*20*/ { S(SELECT_CAP),  E(TX_SENT),          NULL,            a_start_sr,    S(SAME) },
    /*21*/ { S(SELECT_CAP),  E(RX_ACCEPT),        NULL,            NULL,          S(TRANS_SINK) },
    /*22*/ { S(SELECT_CAP),  E(RX_REJECT),        g_contract,      NULL,          S(READY) },
    /*23*/ { S(SELECT_CAP),  E(RX_REJECT),        g_no_contract,   a_reject_nc,   S(WAIT_CAPS) },
    /*24*/ { S(SELECT_CAP),  E(RX_WAIT),          g_contract,      a_wait_c,      S(READY) },
    /*25*/ { S(SELECT_CAP),  E(RX_WAIT),          g_no_contract,   NULL,          S(WAIT_CAPS) },
    /*26*/ { S(SELECT_CAP),  E(TIMEOUT),          NULL,            NULL,          S(HARD_RESET) },
    /*27*/ { S(SELECT_CAP),  E(TX_FAIL),          NULL,            NULL,          S(SEND_SOFT_RESET) },
    /*28*/ { S(SELECT_CAP),  E(RX_SRC_CAPS),      g_tx_pending,    NULL,          S(EVAL_CAP) },
    /*29*/ { S(SELECT_CAP),  E(RX_SRC_CAPS),      g_no_tx_pending, NULL,          S(SEND_SOFT_RESET) },
    /*30*/ { S(SELECT_CAP),  E(RX_PS_RDY),        NULL,            NULL,          S(SEND_SOFT_RESET) },
           { S(SELECT_CAP),  E(RX_GET_SINK_CAP),  NULL,            NULL,          S(SEND_SOFT_RESET) },
           { S(SELECT_CAP),  E(RX_GOTOMIN),       NULL,            NULL,          S(SEND_SOFT_RESET) },
           { S(SELECT_CAP),  E(RX_UNEXPECTED),    NULL,            NULL,          S(SEND_SOFT_RESET) },
           { S(SELECT_CAP),  E(RX_UNSUPPORTED),   NULL,            NULL,          S(SEND_SOFT_RESET) },
    /*31*/ { S(SELECT_CAP),  E(RX_PING),          NULL,            NULL,          S(SAME) },
    /*32*/ { S(TRANS_SINK),  E(RX_PS_RDY),        NULL,            a_contract,    S(READY) },
    /*33*/ { S(TRANS_SINK),  E(TIMEOUT),          NULL,            NULL,          S(HARD_RESET) },
    /*34*/ { S(TRANS_SINK),  E(RX_PING),          NULL,            NULL,          S(SAME) },
    /*35*/ { S(TRANS_SINK),  E(RX_SRC_CAPS),      NULL,            NULL,          S(HARD_RESET) },
           { S(TRANS_SINK),  E(RX_ACCEPT),        NULL,            NULL,          S(HARD_RESET) },
           { S(TRANS_SINK),  E(RX_REJECT),        NULL,            NULL,          S(HARD_RESET) },
           { S(TRANS_SINK),  E(RX_WAIT),          NULL,            NULL,          S(HARD_RESET) },
           { S(TRANS_SINK),  E(RX_SOFT_RESET),    NULL,            NULL,          S(HARD_RESET) },
           { S(TRANS_SINK),  E(RX_GET_SINK_CAP),  NULL,            NULL,          S(HARD_RESET) },
           { S(TRANS_SINK),  E(RX_GOTOMIN),       NULL,            NULL,          S(HARD_RESET) },
           { S(TRANS_SINK),  E(RX_UNEXPECTED),    NULL,            NULL,          S(HARD_RESET) },
           { S(TRANS_SINK),  E(RX_UNSUPPORTED),   NULL,            NULL,          S(HARD_RESET) },
    /*36*/ { S(TRANS_SINK),  E(TX_FAIL),          NULL,            NULL,          S(HARD_RESET) },
    /*37*/ { S(READY),       E(RX_SRC_CAPS),      NULL,            NULL,          S(EVAL_CAP) },
    /*38*/ { S(READY),       E(WANT),             g_caps,          a_log_want,    S(EVAL_CAP) },
    /*39*/ { S(READY),       E(SINKREQ),          g_caps,          NULL,          S(EVAL_CAP) },
    /*40*/ { S(READY),       E(RX_GET_SINK_CAP),  NULL,            NULL,          S(GIVE_SINK_CAP) },
    /*41*/ { S(READY),       E(RX_GOTOMIN),       NULL,            NULL,          S(TRANS_SINK) },
    /*42*/ { S(READY),       E(RX_PING),          NULL,            NULL,          S(SAME) },
    /*43*/ { S(READY),       E(RX_ACCEPT),        NULL,            NULL,          S(SEND_SOFT_RESET) },
           { S(READY),       E(RX_REJECT),        NULL,            NULL,          S(SEND_SOFT_RESET) },
           { S(READY),       E(RX_WAIT),          NULL,            NULL,          S(SEND_SOFT_RESET) },
           { S(READY),       E(RX_PS_RDY),        NULL,            NULL,          S(SEND_SOFT_RESET) },
           { S(READY),       E(RX_UNEXPECTED),    NULL,            NULL,          S(SEND_SOFT_RESET) },
    /*44*/ { S(READY),       E(RX_UNSUPPORTED),   NULL,            a_tx_reject,   S(SAME) },
    /*45*/ { S(READY),       E(TX_SENT),          NULL,            NULL,          S(SAME) },
    /*46*/ { S(READY),       E(TX_FAIL),          NULL,            NULL,          S(SEND_SOFT_RESET) },
    /*47*/ { S(GIVE_SINK_CAP), E(TX_SENT),        NULL,            NULL,          S(READY) },
    /*48*/ { S(GIVE_SINK_CAP), E(TX_FAIL),        NULL,            NULL,          S(SEND_SOFT_RESET) },
    /*49*/ { S(GIVE_SINK_CAP), E(RX_SRC_CAPS),    NULL,            NULL,          S(EVAL_CAP) },
    /*50*/ { S(SEND_SOFT_RESET), E(TX_SENT),      NULL,            a_start_sr,    S(SAME) },
    /*51*/ { S(SEND_SOFT_RESET), E(RX_ACCEPT),    NULL,            NULL,          S(WAIT_CAPS) },
    /*52*/ { S(SEND_SOFT_RESET), E(TIMEOUT),      NULL,            NULL,          S(HARD_RESET) },
           { S(SEND_SOFT_RESET), E(TX_FAIL),      NULL,            NULL,          S(HARD_RESET) },
    /*53*/ { S(SOFT_RESET),  E(TX_SENT),          NULL,            NULL,          S(WAIT_CAPS) },
    /*54*/ { S(SOFT_RESET),  E(TX_FAIL),          NULL,            NULL,          S(HARD_RESET) },
    /*55*/ { S(HARD_RESET),  E(GIVE_UP),          NULL,            NULL,          S(DISABLED) },
    /*56*/ { S(HARD_RESET),  E(HARD_SENT),        NULL,            NULL,          S(TRANS_DEFAULT) },
           { S(HARD_RESET),  E(TIMEOUT),          NULL,            NULL,          S(TRANS_DEFAULT) },
           { S(HARD_RESET),  E(VBUS_OFF),         NULL,            NULL,          S(TRANS_DEFAULT) },
    /*57*/ { S(TRANS_DEFAULT), E(VBUS_OFF),       NULL,            NULL,          S(STARTUP) },
    /*58*/ { S(TRANS_DEFAULT), E(TIMEOUT),        NULL,            NULL,          S(STARTUP) },
    /*59*/ { S(TRANS_DEFAULT), E(VBUS_ON),        NULL,            NULL,          S(SAME) },
           { S(TRANS_DEFAULT), E(HR_RX),          NULL,            NULL,          S(SAME) },
    /*60*/ { S(DISABLED),    E(RX_SRC_CAPS),      NULL,            NULL,          S(EVAL_CAP) },
    /*61*/ { S(ANY),         E(HR_RX),            NULL,            NULL,          S(TRANS_DEFAULT) },
    /*62*/ { S(ANY),         E(VBUS_OFF),         NULL,            a_lost_detach, S(UNATTACHED) },
    /*63*/ { S(ANY),         E(RX_SOFT_RESET),    NULL,            NULL,          S(SOFT_RESET) },
    /*64*/ { S(ANY),         E(WANT),             NULL,            a_log_want,    S(SAME) },
};
#undef S
#undef E
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
        if (r->next != PD_ST_SAME) log_transition(from, (pd_state_t)r->next, ev);
        if (r->action) r->action(sm);
        if (r->next != PD_ST_SAME) {
            pd_state_t to = (pd_state_t)r->next;
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
    wr(sm, FUSB_RESET, FUSB_RESET_SW_RES);
    wr(sm, FUSB_POWER, 0x0F);
    wr(sm, FUSB_CONTROL3, 0x07);
    clear_int(sm);
    sm->want_mv = 5000;
    sm->hr_count = 0;
    sm->vbus_known = false;
    sm->rx_last_id = -1;
    sm->state = PD_ST_UNATTACHED;
    sm->entered_ms = now(sm);
    log_line_t l;
    log_begin(&l, "sm.init");
    log_uint(&l, "rows", (uint32_t)pd_sm_table_len);
    log_uint(&l, "states", PD_ST__COUNT);
    log_str(&l, "state", pd_sm_state_name(sm->state));
    log_end(&l);
    states[sm->state].on_entry(sm);
}

void pd_sm_set_want(pd_sm_t *sm, uint32_t mv)
{
    sm->want_mv = mv;
    pd_sm_post(sm, PD_EV_WANT);
}

void pd_sm_hb_fields(const pd_sm_t *sm, log_line_t *l)
{
    log_str(l, "state", pd_sm_state_name(sm->state));
    log_bool(l, "vbus", sm->vbus);
    log_uint(l, "want", sm->want_mv);
    log_uint(l, "mv", sm->have_contract ? sm->con_mv : 0);
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
    pd_drv_poll(sm);
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
