/* Host scenario tests T01-T37 (docs/spec.md §8.3) plus mechanism checks.
 * T38 (codec vectors) lives in test_msg.c. */
#include <stdio.h>
#include <string.h>
#include "scenario.h"
#include "fusb302_regs.h"
#include "pd_drv.h"
#include "log.h"

#define HAS(ev, a, b) (logcap_find(0, ev, a, b) >= 0)
#define NL(ev, a, b)  logcap_nlines(ev, a, b)
#define TX_IS(arr)    scn_last_tx_is(arr, sizeof arr)

static int total_fails, ntests;
static void done(void) { total_fails += scn_end(); ntests++; }

static int last_find(const char *ev, const char *a, const char *b)
{
    int r = -1;
    for (int i = logcap_find(0, ev, a, b); i >= 0; i = logcap_find(i + 1, ev, a, b)) r = i;
    return r;
}
static uint32_t tick_of(int line) { return line >= 0 ? logcap_tick(line) : 0xFFFFFFFFu; }
static void ctrl(uint8_t type, uint8_t id) { scn_push_msg(scn_src_hdr(type, id, 0), NULL, 0); }
static void attach_cc1(void)
{
    scn_setcc(2, 0);
    scn_vbus(true);
    scn_tick(120);
    SCN_CHECK(S.sm.state == PD_ST_WAIT_CAPS);
}
static void to_hard_reset_from_waitcaps(void)
{
    SCN_CHECK(scn_until_state(PD_ST_HARD_RESET, 700) >= 0);
}

/* §6.5 byte vectors */
static const uint8_t V1[] = {0x12,0x12,0x12,0x13,0x86,0x42,0x10,0x2C,0xB1,0x04,0x20,0xFF,0x14,0xFE,0xA1};
static const uint8_t V2[] = {0x12,0x12,0x12,0x13,0x86,0x42,0x10,0x2C,0xB1,0x04,0x10,0xFF,0x14,0xFE,0xA1};
static const uint8_t V3[] = {0x12,0x12,0x12,0x13,0x86,0x42,0x12,0x2C,0xB1,0x04,0x20,0xFF,0x14,0xFE,0xA1};
static const uint8_t V4[] = {0x12,0x12,0x12,0x13,0x82,0x4D,0x00,0xFF,0x14,0xFE,0xA1};
static const uint8_t V5[] = {0x12,0x12,0x12,0x13,0x82,0x43,0x00,0xFF,0x14,0xFE,0xA1};
/* 5 V Request with id 1 (T13 fallback, T21) */
static const uint8_t REQ5_ID1[] = {0x12,0x12,0x12,0x13,0x86,0x42,0x12,0x2C,0xB1,0x04,0x10,0xFF,0x14,0xFE,0xA1};
/* V7 with id 1: hdr 0x1244 */
static const uint8_t SNK5_ID1[] = {0x12,0x12,0x12,0x13,0x86,0x44,0x12,0x2C,0x91,0x01,0x00,0xFF,0x14,0xFE,0xA1};
/* Reject id 1: hdr 0x0244 */
static const uint8_t REJ_ID1[] = {0x12,0x12,0x12,0x13,0x82,0x44,0x02,0xFF,0x14,0xFE,0xA1};

/* ---- T01-T06 Type-C ------------------------------------------------------ */
static void t01(void)
{
    static const scn_step_t s[] = {
        { 1, SCN_EXPECT_REG, .reg = FUSB_POWER, .val = 0x0F },
        { 1, SCN_EXPECT_REG, .reg = FUSB_CONTROL3, .val = 0x07 },
        { 1, SCN_EXPECT_REG, .reg = FUSB_SWITCHES1, .val = 0x20 },
        { 1, SCN_EXPECT_REG, .reg = FUSB_SWITCHES0, .val = 0x03 },
        { 1, SCN_EXPECT_STATE, .state = PD_ST_UNATTACHED },
        { 1, SCN_EXPECT_LOG, .text = "\"ev\":\"sm.init\",\"rows\":85,\"states\":16" },
        { 50, SCN_EXPECT_STATE, .state = PD_ST_UNATTACHED },
        { 0, SCN_END },
    };
    total_fails += scn_run("T01 boot_init", s, 0);
    SCN_CHECK(S.f.sw_resets == 1);
    ntests++;
}

static void t02(void)
{
    static const scn_step_t s[] = {
        { 0, SCN_SETCC, .val = 2, .val2 = 0 },
        { 0, SCN_SETBITS, .reg = FUSB_STATUS0, .val = FUSB_ST0_VBUSOK },
        { 120, SCN_EXPECT_STATE, .state = PD_ST_WAIT_CAPS },
        { 120, SCN_EXPECT_REG, .reg = FUSB_SWITCHES0, .val = 0x07 },
        { 120, SCN_EXPECT_REG, .reg = FUSB_SWITCHES1, .val = 0x25 },
        { 120, SCN_EXPECT_LOG, .text = "\"ev\":\"cc\",\"cc\":1" },
        { 120, SCN_EXPECT_LOG, .text = "\"ev\":\"pd.cfg\",\"sw0\":\"0x07\",\"sw1\":\"0x25\"" },
        { 120, SCN_EXPECT_LOG_ORDER, .text = "\"to\":\"AttachWait\"", .text2 = "\"to\":\"Attached\"" },
        { 120, SCN_EXPECT_LOG_ORDER, .text = "\"to\":\"Attached\"", .text2 = "\"to\":\"Startup\"" },
        { 120, SCN_EXPECT_LOG_ORDER, .text = "\"to\":\"Startup\"", .text2 = "\"to\":\"Discovery\"" },
        { 120, SCN_EXPECT_LOG_ORDER, .text = "\"to\":\"Discovery\"", .text2 = "\"to\":\"WaitCaps\"" },
        { 0, SCN_END },
    };
    total_fails += scn_run("T02 attach_cc1", s, 0);
    ntests++;
}

static void t03(void)
{
    static const scn_step_t s[] = {
        { 0, SCN_SETCC, .val = 0, .val2 = 3 },
        { 0, SCN_SETBITS, .reg = FUSB_STATUS0, .val = FUSB_ST0_VBUSOK },
        { 120, SCN_EXPECT_STATE, .state = PD_ST_WAIT_CAPS },
        { 120, SCN_EXPECT_REG, .reg = FUSB_SWITCHES0, .val = 0x0B },
        { 120, SCN_EXPECT_REG, .reg = FUSB_SWITCHES1, .val = 0x26 },
        { 120, SCN_EXPECT_LOG, .text = "\"ev\":\"cc\",\"cc\":2" },
        { 0, SCN_END },
    };
    total_fails += scn_run("T03 attach_cc2", s, 0);
    ntests++;
}

static void t04(void)
{
    scn_begin("T04 cc_both_retry");
    scn_boot();
    scn_setcc(2, 2);
    scn_vbus(true);
    scn_tick(250);
    SCN_CHECK(S.sm.state == PD_ST_ATTACH_WAIT);
    SCN_CHECK(NL("cc.retry", NULL, NULL) >= 2);
    SCN_CHECK(!HAS("sm", "\"to\":\"Attached\"", NULL));
    scn_setcc(2, 0);
    SCN_CHECK(scn_until_state(PD_ST_WAIT_CAPS, 200) >= 0);
    SCN_CHECK(tick_of(logcap_find(0, "sm", "\"to\":\"Attached\"", NULL)) > 250);
    SCN_CHECK(HAS("cc", "\"cc\":1", NULL));
    done();
}

static void t05(void)
{
    scn_begin("T05 cc_activity_skip");
    scn_boot();
    scn_setcc(2, 0);
    scn_setbits(FUSB_STATUS0, FUSB_ST0_ACTIVITY);
    scn_vbus(true);
    scn_tick(150);
    SCN_CHECK(S.sm.state == PD_ST_ATTACH_WAIT);
    SCN_CHECK(S.sm.n[1] == 0 && S.sm.n[2] == 0);
    SCN_CHECK(HAS("cc.retry", "\"n1\":0,\"n2\":0", NULL));
    scn_clrbits(FUSB_STATUS0, FUSB_ST0_ACTIVITY);
    SCN_CHECK(scn_until_state(PD_ST_WAIT_CAPS, 200) >= 0);
    SCN_CHECK(tick_of(logcap_find(0, "sm", "\"to\":\"Attached\"", NULL)) >= 150);
    done();
}

static void t06(void)
{
    static const scn_step_t s[] = {
        { 0, SCN_SETCC, .val = 2, .val2 = 0 },
        { 0, SCN_SETBITS, .reg = FUSB_STATUS0, .val = FUSB_ST0_VBUSOK },
        { 10, SCN_EXPECT_STATE, .state = PD_ST_ATTACH_WAIT },
        { 50, SCN_CLRBITS, .reg = FUSB_STATUS0, .val = FUSB_ST0_VBUSOK },
        { 50, SCN_EXPECT_STATE, .state = PD_ST_UNATTACHED },
        { 300, SCN_EXPECT_STATE, .state = PD_ST_UNATTACHED },
        { 0, SCN_END },
    };
    total_fails += scn_run("T06 detach_in_attachwait", s, 0);
    SCN_CHECK(!HAS("cc", NULL, NULL));
    SCN_CHECK(!HAS("sm.unhandled", NULL, NULL));
    ntests++;
}

/* ---- T07-T15 contract paths ---------------------------------------------- */
static void t07(void)
{
    scn_begin("T07 happy_5v");
    scn_boot();
    attach_cc1();
    scn_push_d1(0);
    scn_tick(1);
    SCN_CHECK(S.sm.state == PD_ST_SELECT_CAP);
    SCN_CHECK(TX_IS(V2));
    SCN_CHECK(scn_tx_within(2));
    SCN_CHECK(tick_of(logcap_find(0, "pd.tx", "Request", NULL)) == tick_of(logcap_find(0, "pd.rx", "Source_Capabilities", NULL)));
    SCN_CHECK(HAS("pd.caps", "\"n\":4,\"kind\":\"FFFF\",\"mv\":[5000,9000,15000,20000]", NULL));
    SCN_CHECK(HAS("pd.rx", "\"rev\":2", "\"hdr\":\"0x41A1\""));
    scn_tick(2);
    SCN_CHECK(HAS("pd.txres", "\"res\":\"sent\"", NULL) || HAS("pd.txres", "\"res\":\"gcrc\"", NULL));
    SCN_CHECK(HAS("pd.gcrc", "\"id\":0,\"match\":true", NULL));
    ctrl(PD_CTRL_ACCEPT, 1);
    scn_tick(1);
    SCN_CHECK(S.sm.state == PD_ST_TRANS_SINK);
    ctrl(PD_CTRL_PS_RDY, 2);
    scn_tick(1);
    SCN_CHECK(S.sm.state == PD_ST_READY);
    SCN_CHECK(HAS("contract", "\"mv\":5000", "\"pos\":1"));
    log_line_t l;
    log_begin(&l, "hb");
    pd_sm_hb_fields(&S.sm, &l);
    log_end(&l);
    SCN_CHECK(HAS("hb", "\"state\":\"Ready\"", "\"mv\":5000"));
    SCN_CHECK(!HAS("sm.unhandled", NULL, NULL));
    done();
}

static void t08(void)
{
    scn_begin("T08 want_9v_preattach");
    scn_boot();
    pd_sm_set_want(&S.sm, 9000);
    attach_cc1();
    scn_push_d1(0);
    scn_tick(1);
    SCN_CHECK(TX_IS(V1));
    scn_tick(2);
    ctrl(PD_CTRL_ACCEPT, 1);
    scn_tick(1);
    ctrl(PD_CTRL_PS_RDY, 2);
    scn_tick(1);
    SCN_CHECK(S.sm.state == PD_ST_READY);
    SCN_CHECK(HAS("contract", "\"mv\":9000", NULL));
    done();
}

static void t09(void)
{
    scn_begin("T09 want_9v_in_ready");
    scn_contract_5v();
    pd_sm_set_want(&S.sm, 9000);
    scn_tick(1);
    SCN_CHECK(S.sm.state == PD_ST_SELECT_CAP);
    SCN_CHECK(HAS("pd.want", "\"mv\":9000", NULL));
    SCN_CHECK(HAS("sm", "\"from\":\"Ready\",\"to\":\"EvalCap\",\"on\":\"WANT\"", NULL));
    SCN_CHECK(TX_IS(V3));
    scn_tick(2);
    ctrl(PD_CTRL_ACCEPT, 3);
    scn_tick(1);
    ctrl(PD_CTRL_PS_RDY, 4);
    scn_tick(1);
    SCN_CHECK(S.sm.state == PD_ST_READY);
    SCN_CHECK(HAS("contract", "\"mv\":9000", NULL));
    done();
}

static void t10(void)
{
    scn_begin("T10 select_below_min");
    scn_boot();
    pd_sm_set_want(&S.sm, 12000);
    attach_cc1();
    scn_push_d1(0);
    scn_tick(3);
    SCN_CHECK(HAS("pd.sel", "\"want\":12000,\"pos\":2", "\"why\":\"below\""));
    ctrl(PD_CTRL_ACCEPT, 1);
    scn_tick(1);
    ctrl(PD_CTRL_PS_RDY, 2);
    scn_tick(1);
    SCN_CHECK(S.sm.state == PD_ST_READY);
    pd_sm_set_want(&S.sm, 3000);
    scn_tick(1);
    SCN_CHECK(HAS("pd.sel", "\"want\":3000,\"pos\":1", "\"why\":\"min\""));
    done();
}

static void t11(void)
{
    static const uint32_t caps[5] = { 0x0801912C, 0x0002D12C, 0x0004B12C, 0x000641F4, 0xC1A42164 };
    scn_begin("T11 apdo_ignored");
    scn_boot();
    pd_sm_set_want(&S.sm, 21000);
    attach_cc1();
    scn_push_msg(scn_src_hdr(PD_DATA_SOURCE_CAP, 0, 5), caps, 5);
    scn_tick(1);
    SCN_CHECK(HAS("pd.caps", "\"kind\":\"FFFFA\"", NULL));
    SCN_CHECK(HAS("pd.sel", "\"pos\":4", "\"ma\":3000"));
    SCN_CHECK(HAS("pd.tx", "\"obj\":[\"0x4004B12C\"]", NULL));
    done();
}

static void t12(void)
{
    static const uint32_t caps[2] = { 0x0801912C, 0x0002D12C };
    scn_begin("T12 caps_r2");
    scn_boot();
    attach_cc1();
    scn_push_msg(0x2161, caps, 2);
    scn_tick(1);
    SCN_CHECK(HAS("pd.rx", "\"rev\":1", "\"hdr\":\"0x2161\""));
    SCN_CHECK(HAS("pd.caps", "\"n\":2", NULL));
    SCN_CHECK(HAS("pd.tx", "\"type\":\"Request\"", NULL));
    SCN_CHECK(S.sm.state == PD_ST_SELECT_CAP);
    done();
}

static void t13(void)
{
    scn_begin("T13 reject_no_contract");
    scn_boot();
    pd_sm_set_want(&S.sm, 9000);
    attach_cc1();
    scn_push_d1(0);
    scn_tick(3);
    ctrl(PD_CTRL_REJECT, 1);
    scn_tick(1);
    SCN_CHECK(S.sm.state == PD_ST_WAIT_CAPS);
    SCN_CHECK(HAS("pd.rej", "\"mv\":9000", NULL));
    scn_push_d1(2);
    scn_tick(1);
    SCN_CHECK(HAS("pd.sel", "\"pos\":1", "\"why\":\"fallback\""));
    SCN_CHECK(TX_IS(REQ5_ID1));
    done();
}

static void t14(void)
{
    scn_begin("T14 reject_with_contract");
    scn_contract_5v();
    pd_sm_set_want(&S.sm, 9000);
    scn_tick(3);
    ctrl(PD_CTRL_REJECT, 3);
    scn_tick(1);
    SCN_CHECK(S.sm.state == PD_ST_READY);
    SCN_CHECK(NL("contract", NULL, NULL) == 1);
    SCN_CHECK(S.sm.have_contract && S.sm.con_mv == 5000);
    SCN_CHECK(!HAS("pd.rej", NULL, NULL));
    done();
}

static void t15(void)
{
    scn_begin("T15 wait_with_contract");
    scn_contract_5v();
    pd_sm_set_want(&S.sm, 9000);
    scn_tick(3);
    uint32_t n0 = S.f.ntx;
    ctrl(PD_CTRL_WAIT, 3);
    scn_tick(1);
    SCN_CHECK(S.sm.state == PD_ST_READY);
    uint32_t t_ready = S.f.now;
    scn_tick(99);
    SCN_CHECK(S.f.ntx == n0);
    scn_tick(1);
    SCN_CHECK(S.f.ntx == n0 + 1);
    SCN_CHECK(fake_last_tx(&S.f)->tick == t_ready + 100);
    SCN_CHECK(HAS("sm", "\"from\":\"Ready\",\"to\":\"EvalCap\",\"on\":\"SINKREQ\"", NULL));
    SCN_CHECK(S.sm.state == PD_ST_SELECT_CAP);
    done();
}

/* ---- T16-T24 timeouts, resets -------------------------------------------- */
static void t16(void)
{
    scn_begin("T16 sender_response_tmo");
    scn_boot();
    attach_cc1();
    scn_push_d1(0);
    SCN_CHECK(scn_until_state(PD_ST_HARD_RESET, 60) >= 0);
    SCN_CHECK(S.f.wlast[FUSB_CONTROL3] == 0x47);
    SCN_CHECK(HAS("pd.hr", "\"dir\":\"tx\",\"n\":1", NULL));
    SCN_CHECK(tick_of(logcap_find(0, "pd.hr", NULL, NULL)) - tick_of(logcap_find(0, "pd.txres", NULL, NULL)) == 30);
    done();
}

static void t17(void)
{
    scn_begin("T17 ps_transition_tmo");
    scn_boot();
    attach_cc1();
    scn_push_d1(0);
    scn_tick(3);
    ctrl(PD_CTRL_ACCEPT, 1);
    scn_tick(1);
    SCN_CHECK(S.sm.state == PD_ST_TRANS_SINK);
    uint32_t t0 = S.f.now;
    scn_tick(549);
    SCN_CHECK(S.sm.state == PD_ST_TRANS_SINK);
    scn_tick(1);
    SCN_CHECK(S.sm.state == PD_ST_HARD_RESET);
    SCN_CHECK(S.f.now - t0 == 550);
    done();
}

static void t18(void)
{
    scn_begin("T18 waitcaps_tmo_hr_cycle");
    scn_boot();
    attach_cc1();
    uint32_t t_wc = tick_of(logcap_find(0, "sm", "\"to\":\"WaitCaps\"", NULL));
    to_hard_reset_from_waitcaps();
    SCN_CHECK(S.f.now - t_wc == 600);
    SCN_CHECK(S.f.wlast[FUSB_CONTROL3] == 0x47);
    uint32_t pdr = S.f.pd_resets;
    scn_setbits(FUSB_INTERRUPTA, FUSB_IA_HARDSENT);
    scn_tick(1);
    SCN_CHECK(S.sm.state == PD_ST_TRANS_DEFAULT);
    SCN_CHECK(HAS("sm", "\"to\":\"TransDefault\",\"on\":\"HARD_SENT\"", NULL));
    SCN_CHECK(S.f.reg[FUSB_CONTROL3] == 0x07);
    SCN_CHECK(S.f.pd_resets == pdr + 1);
    scn_vbus(false);
    scn_tick(1);
    SCN_CHECK(S.sm.state == PD_ST_DISCOVERY);
    SCN_CHECK(scn_log_order("sm", "\"from\":\"TransDefault\",\"to\":\"Startup\",\"on\":\"VBUS_OFF\"",
                            "sm", "\"from\":\"Startup\",\"to\":\"Discovery\""));
    scn_tick(50);
    scn_vbus(true);
    scn_tick(1);
    SCN_CHECK(S.sm.state == PD_ST_WAIT_CAPS);
    done();
}

static void t19(void)
{
    scn_begin("T19 hr_exhaust");
    scn_boot();
    attach_cc1();
    SCN_CHECK(scn_until_state(PD_ST_DISABLED, 8000) >= 0);
    SCN_CHECK(NL("pd.hr", "\"dir\":\"tx\"", NULL) == 3);
    SCN_CHECK(NL("pd.hr", "\"n\":1", NULL) == 1);
    SCN_CHECK(NL("pd.hr", "\"n\":2", NULL) == 1);
    SCN_CHECK(NL("pd.hr", "\"n\":3", NULL) == 1);
    SCN_CHECK(HAS("pd.giveup", "\"n\":3", NULL));
    scn_tick(100);
    SCN_CHECK(S.sm.state == PD_ST_DISABLED);
    scn_vbus(false);
    scn_tick(1);
    SCN_CHECK(S.sm.state == PD_ST_UNATTACHED);
    SCN_CHECK(S.sm.hr_count == 0);
    scn_vbus(true);
    SCN_CHECK(scn_until_state(PD_ST_WAIT_CAPS, 200) >= 0);
    SCN_CHECK(scn_until_state(PD_ST_HARD_RESET, 700) >= 0);
    SCN_CHECK(logcap_find(last_find("pd.giveup", NULL, NULL), "pd.hr", "\"n\":1", NULL) >= 0);
    done();
}

static void t20(void)
{
    scn_begin("T20 hr_received");
    scn_contract_5v();
    uint8_t hrc = S.sm.hr_count;
    scn_setbits(FUSB_INTERRUPTA, FUSB_IA_HARDRST);
    scn_tick(1);
    SCN_CHECK(S.sm.state == PD_ST_TRANS_DEFAULT);
    SCN_CHECK(HAS("pd.hr", "\"dir\":\"rx\"", NULL));
    SCN_CHECK(!HAS("pd.hr", "\"dir\":\"tx\"", NULL));
    SCN_CHECK(HAS("contract.lost", "\"why\":\"hardreset\"", NULL));
    SCN_CHECK(S.sm.hr_count == hrc);
    SCN_CHECK(S.f.wlast[FUSB_CONTROL1] == 0x04);
    done();
}

static void t21(void)
{
    scn_begin("T21 soft_reset_rx");
    scn_contract_5v();
    scn_push_msg(0x01AD, NULL, 0);
    scn_tick(1);
    SCN_CHECK(S.sm.state == PD_ST_SOFT_RESET);
    SCN_CHECK(TX_IS(V5));
    SCN_CHECK(scn_tx_within(1));
    SCN_CHECK(HAS("pd.sr", "\"dir\":\"rx\"", NULL));
    scn_tick(1);
    SCN_CHECK(S.sm.state == PD_ST_WAIT_CAPS);
    scn_push_d1(0);
    scn_tick(1);
    SCN_CHECK(!HAS("pd.dup", NULL, NULL));
    SCN_CHECK(S.sm.state == PD_ST_SELECT_CAP);
    SCN_CHECK(TX_IS(REQ5_ID1));
    done();
}

static void t22(void)
{
    scn_begin("T22 dup_dropped");
    scn_boot();
    attach_cc1();
    scn_push_d1(0);
    scn_push_d1(0);
    scn_tick(5);
    SCN_CHECK(NL("pd.tx", "\"type\":\"Request\"", NULL) == 1);
    SCN_CHECK(NL("pd.dup", "\"id\":0", NULL) == 1);
    SCN_CHECK(S.sm.state == PD_ST_SELECT_CAP);
    done();
}

static void tx_fail_prefix(void)
{
    scn_boot();
    attach_cc1();
    fake_set_ack(&S.f, ACK_RETRYFAIL, 1);
    scn_push_d1(0);
    scn_tick(1);
    fake_set_ack(&S.f, ACK_BOTH, 1);
    scn_tick(1);
    SCN_CHECK(HAS("pd.txres", "\"res\":\"fail\"", NULL));
    SCN_CHECK(S.sm.state == PD_ST_SEND_SOFT_RESET);
    SCN_CHECK(TX_IS(V4));
    SCN_CHECK(HAS("pd.sr", "\"dir\":\"tx\"", NULL));
    scn_tick(1);
}

static void t23(void)
{
    scn_begin("T23 tx_fail_soft_reset");
    tx_fail_prefix();
    ctrl(PD_CTRL_ACCEPT, 0);
    scn_tick(1);
    SCN_CHECK(S.sm.state == PD_ST_WAIT_CAPS);
    done();
}

static void t24(void)
{
    scn_begin("T24 soft_reset_fail_hr");
    tx_fail_prefix();
    uint32_t t_sent = tick_of(last_find("pd.txres", "\"res\":\"sent\"", NULL));
    SCN_CHECK(scn_until_state(PD_ST_HARD_RESET, 60) >= 0);
    SCN_CHECK(S.f.now - t_sent == 30);
    done();
}

/* ---- T25-T29 Ready ------------------------------------------------------- */
static void t25(void)
{
    scn_begin("T25 get_sink_cap");
    scn_contract_5v();
    ctrl(PD_CTRL_GET_SINK_CAP, 3);
    scn_tick(1);
    SCN_CHECK(S.sm.state == PD_ST_GIVE_SINK_CAP);
    SCN_CHECK(TX_IS(SNK5_ID1));
    scn_tick(1);
    SCN_CHECK(S.sm.state == PD_ST_READY);
    done();
}

static void t26(void)
{
    scn_begin("T26 unsupported_reject");
    scn_contract_5v();
    int nsm = NL("sm", NULL, NULL);
    ctrl(PD_CTRL_DR_SWAP, 3);
    scn_tick(1);
    SCN_CHECK(HAS("pd.rx", "\"type\":\"DR_Swap\"", "\"cls\":\"unsupported\""));
    SCN_CHECK(TX_IS(REJ_ID1));
    scn_tick(3);
    SCN_CHECK(S.sm.state == PD_ST_READY);
    SCN_CHECK(NL("sm", NULL, NULL) == nsm);
    SCN_CHECK(!HAS("sm.unhandled", NULL, NULL));
    done();
}

static void t27(void)
{
    scn_begin("T27 unexpected_soft_reset");
    scn_contract_5v();
    ctrl(PD_CTRL_ACCEPT, 3);
    scn_tick(1);
    SCN_CHECK(S.sm.state == PD_ST_SEND_SOFT_RESET);
    SCN_CHECK(TX_IS(V4));
    done();
}

static void t28(void)
{
    scn_begin("T28 caps_in_transsink");
    scn_boot();
    attach_cc1();
    scn_push_d1(0);
    scn_tick(3);
    ctrl(PD_CTRL_ACCEPT, 1);
    scn_tick(1);
    SCN_CHECK(S.sm.state == PD_ST_TRANS_SINK);
    scn_push_d1(2);
    scn_tick(1);
    SCN_CHECK(S.sm.state == PD_ST_HARD_RESET);
    done();
}

static void t29(void)
{
    scn_begin("T29 detach_in_ready");
    scn_contract_5v();
    scn_vbus(false);
    scn_tick(1);
    SCN_CHECK(S.sm.state == PD_ST_UNATTACHED);
    SCN_CHECK(NL("contract.lost", "\"why\":\"detach\"", NULL) == 1);
    SCN_CHECK(S.f.reg[FUSB_SWITCHES1] == 0x20);
    SCN_CHECK(!S.sm.have_contract);
    done();
}

/* ---- T30-T37 driver / protocol -------------------------------------------- */
static void t30_one(const char *name, fake_ack_t mode, const char *res, uint32_t dt)
{
    scn_begin(name);
    scn_boot();
    attach_cc1();
    fake_set_ack(&S.f, mode, 1);
    scn_push_d1(0);
    scn_tick(12);
    SCN_CHECK(NL("pd.txres", NULL, NULL) == 1);
    SCN_CHECK(NL("pd.txres", res, NULL) == 1);
    SCN_CHECK(tick_of(logcap_find(0, "pd.txres", NULL, NULL)) - tick_of(logcap_find(0, "pd.tx", NULL, NULL)) == dt);
    SCN_CHECK(S.sm.tx_msgid == 1);
    SCN_CHECK(S.sm.state == PD_ST_SELECT_CAP);
    done();
}
static void t30(void)
{
    t30_one("T30a txres_sent", ACK_TXSENT, "\"res\":\"sent\"", 1);
    t30_one("T30b txres_gcrc", ACK_GCRC, "\"res\":\"gcrc\"", 1);
    t30_one("T30c txres_tmo", ACK_NONE, "\"res\":\"tmo\"", 10);
}

static void t31(void)
{
    scn_begin("T31 gcrc_mismatch");
    scn_boot();
    attach_cc1();
    fake_set_ack(&S.f, ACK_NONE, 1);
    scn_push_d1(0);
    scn_tick(1);
    scn_push_msg(0x01A1 | 5 << 9, NULL, 0);
    scn_tick(1);
    SCN_CHECK(HAS("pd.gcrc", "\"id\":5,\"match\":false", NULL));
    SCN_CHECK(!HAS("pd.txres", NULL, NULL));
    SCN_CHECK(S.sm.tx_pending);
    SCN_CHECK(S.sm.tx_msgid == 0);
    done();
}

static void t32(void)
{
    scn_begin("T32 fifo_two_msgs");
    scn_boot();
    attach_cc1();
    fake_set_ack(&S.f, ACK_NONE, 1);
    scn_push_d1(0);
    scn_tick(1);
    ctrl(PD_CTRL_GOODCRC, 0);
    ctrl(PD_CTRL_ACCEPT, 1);
    ctrl(PD_CTRL_PS_RDY, 2);
    scn_tick(1);
    SCN_CHECK(HAS("pd.txres", "\"res\":\"gcrc\"", NULL));
    SCN_CHECK(S.sm.state == PD_ST_TRANS_SINK);
    scn_tick(1);
    SCN_CHECK(S.sm.state == PD_ST_READY);
    int a = logcap_find(0, "pd.rx", "\"type\":\"Accept\"", NULL);
    int b = logcap_find(0, "pd.rx", "\"type\":\"PS_RDY\"", NULL);
    SCN_CHECK(a >= 0 && b > a && tick_of(b) == tick_of(a) + 1);
    done();
}

static void t33(void)
{
    static const uint8_t junk[] = { 0x55, 0x01, 0x02, 0x03 };
    scn_begin("T33 bad_token");
    scn_boot();
    attach_cc1();
    int nsm = NL("sm", NULL, NULL);
    uint32_t c1 = S.f.wcount[FUSB_CONTROL1];
    scn_push(junk, sizeof junk);
    scn_tick(1);
    SCN_CHECK(S.f.wcount[FUSB_CONTROL1] == c1 + 1 && S.f.wlast[FUSB_CONTROL1] == 0x04);
    SCN_CHECK(HAS("pd.rxraw", "\"tok\":\"0x55\"", NULL));
    SCN_CHECK(S.f.rx_len == 0);
    scn_tick(2);
    SCN_CHECK(S.sm.state == PD_ST_WAIT_CAPS);
    SCN_CHECK(NL("sm", NULL, NULL) == nsm);
    done();
}

static void t34(void)
{
    scn_begin("T34 collision_resend");
    scn_boot();
    attach_cc1();
    fake_set_ack(&S.f, ACK_COLL, 1);
    scn_push_d1(0);
    scn_tick(4);
    SCN_CHECK(S.f.ntx == 2);
    SCN_CHECK(S.f.tx[0].n == S.f.tx[1].n && memcmp(S.f.tx[0].b, S.f.tx[1].b, S.f.tx[0].n) == 0);
    SCN_CHECK(scn_log_order("pd.txres", "\"res\":\"coll\"", "pd.txres", "\"res\":\"sent\""));
    SCN_CHECK(S.sm.tx_msgid == 1);
    SCN_CHECK(S.sm.state == PD_ST_SELECT_CAP);
    done();
}

static void t34bcd(void)
{
    scn_begin("T34b collision_restarts_deadline");
    scn_boot();
    attach_cc1();
    fake_set_ack(&S.f, ACK_COLL, 8);                     /* coll at +8, GoodCRC at +16 > 10 */
    scn_push_d1(0);
    scn_tick(20);
    SCN_CHECK(S.f.ntx == 2);
    SCN_CHECK(scn_log_order("pd.txres", "\"res\":\"coll\"", "pd.txres", "\"res\":\"sent\""));
    SCN_CHECK(!HAS("pd.txres", "\"res\":\"tmo\"", NULL));
    SCN_CHECK(S.sm.state == PD_ST_SELECT_CAP);
    done();

    scn_begin("T34c double_collision_fail");
    scn_boot();
    attach_cc1();
    fake_set_ack(&S.f, ACK_COLL, 1);
    S.f.coll_n = 2;
    scn_push_d1(0);
    scn_tick(4);
    SCN_CHECK(S.f.ntx == 3);                             /* Request, resend, then Soft_Reset */
    SCN_CHECK(scn_log_order("pd.txres", "\"coll\":true", "sm", "\"to\":\"SendSoftReset\",\"on\":\"TX_FAIL\""));
    SCN_CHECK(scn_log_order("pd.txres", "\"res\":\"coll\"", "pd.txres", "\"res\":\"fail\""));
    done();

    scn_begin("T34d fifo_write_i2c_fail");
    scn_boot();
    attach_cc1();
    S.f.fail_fifo_wr = 1;
    scn_push_d1(0);
    scn_tick(2);
    SCN_CHECK(S.f.ntx == 1);                             /* only the Soft_Reset reached the bus */
    SCN_CHECK(scn_log_order("pd.txres", "\"i2c\":true", "sm", "\"to\":\"SendSoftReset\",\"on\":\"TX_FAIL\""));
    SCN_CHECK(HAS("pd.tx", "\"type\":\"Soft_Reset\"", NULL) && !HAS("pd.tx", "\"type\":\"Request\"", NULL));
    done();
}

/* §2.1: an idle step is one 5-byte burst 0x3E..0x42 (or 5 single reads with
 * PD_DRV_BURST_POLL=0); nothing ever bursts into FIFOS. */
static void t39(void)
{
    scn_begin(PD_DRV_BURST_POLL ? "T39 idle_step_burst" : "T39 idle_step_single");
    scn_contract_5v();
    scn_tick(5);
    uint32_t x0 = S.f.xfers, b0 = S.f.bus_bytes, n0 = S.f.bursts;
    uint32_t s0[5];
    for (int k = 0; k < 5; k++) s0[k] = S.f.single_reads[FUSB_INTERRUPTA + k];
    scn_tick(1);
    SCN_CHECK(S.sm.state == PD_ST_READY);
#if PD_DRV_BURST_POLL
    SCN_CHECK(S.f.xfers - x0 == 1 && S.f.bus_bytes - b0 == 8 && S.f.bursts - n0 == 1);
    for (int k = 0; k < 5; k++) SCN_CHECK(S.f.single_reads[FUSB_INTERRUPTA + k] == s0[k]);
#else
    SCN_CHECK(S.f.xfers - x0 == 5 && S.f.bus_bytes - b0 == 20 && S.f.bursts == n0 && n0 == 0);
    for (int k = 0; k < 5; k++) SCN_CHECK(S.f.single_reads[FUSB_INTERRUPTA + k] == s0[k] + 1);
#endif
    SCN_CHECK(S.f.burst_bad == 0);
    done();
}

static void t35(void)
{
    scn_begin("T35a ping_in_ready");
    scn_contract_5v();
    int nsm = NL("sm", NULL, NULL);
    ctrl(PD_CTRL_PING, 3);
    scn_tick(2);
    SCN_CHECK(S.sm.state == PD_ST_READY);
    SCN_CHECK(NL("sm", NULL, NULL) == nsm);
    SCN_CHECK(!HAS("sm.unhandled", NULL, NULL));
    done();

    scn_begin("T35b ping_in_transsink");
    scn_boot();
    attach_cc1();
    scn_push_d1(0);
    scn_tick(3);
    ctrl(PD_CTRL_ACCEPT, 1);
    scn_tick(1);
    nsm = NL("sm", NULL, NULL);
    ctrl(PD_CTRL_PING, 2);
    scn_tick(2);
    SCN_CHECK(S.sm.state == PD_ST_TRANS_SINK);
    SCN_CHECK(NL("sm", NULL, NULL) == nsm);
    SCN_CHECK(!HAS("sm.unhandled", NULL, NULL));
    done();
}

static void t36(void)
{
    scn_begin("T36 transdefault_no_vbus_drop");
    scn_boot();
    attach_cc1();
    to_hard_reset_from_waitcaps();
    scn_setbits(FUSB_INTERRUPTA, FUSB_IA_HARDSENT);
    scn_tick(1);
    SCN_CHECK(S.sm.state == PD_ST_TRANS_DEFAULT);
    uint32_t t0 = S.f.now;
    scn_tick(999);
    SCN_CHECK(S.sm.state == PD_ST_TRANS_DEFAULT);
    scn_tick(1);
    SCN_CHECK(S.sm.state == PD_ST_WAIT_CAPS);
    SCN_CHECK(S.f.now - t0 == 1000);
    SCN_CHECK(scn_log_order("sm", "\"from\":\"TransDefault\",\"to\":\"Startup\",\"on\":\"TIMEOUT\"",
                            "sm", "\"from\":\"Discovery\",\"to\":\"WaitCaps\",\"on\":\"DONE\""));
    done();
}

static void t37(void)
{
    scn_begin("T37 discovery_timeout");
    scn_boot();
    attach_cc1();
    to_hard_reset_from_waitcaps();
    scn_setbits(FUSB_INTERRUPTA, FUSB_IA_HARDSENT);
    scn_tick(1);
    scn_vbus(false);
    scn_tick(1);
    SCN_CHECK(S.sm.state == PD_ST_DISCOVERY);
    uint32_t t0 = S.f.now;
    scn_tick(2499);
    SCN_CHECK(S.sm.state == PD_ST_DISCOVERY);
    scn_tick(1);
    SCN_CHECK(S.sm.state == PD_ST_UNATTACHED);
    SCN_CHECK(S.f.now - t0 == 2500);
    done();
}

/* ---- mechanism ----------------------------------------------------------- */
static void m_jsonl(void)
{
    scn_begin("M1 jsonl_format");
    scn_contract_5v();
    SCN_CHECK(logcap_nlines_total() > 10);
    for (int i = 0; i < logcap_nlines_total(); i++) {
        const char *l = logcap_line(i);
        size_t n = strlen(l);
        if (strncmp(l, "{\"t\":", 5) != 0 || !strstr(l, ",\"ev\":\"") || n < 2 || l[n - 1] != '}') {
            printf("    bad line: %s\n", l);
            SCN_CHECK(0);
        }
    }
    done();
}

static void m_table(void)
{
    scn_begin("M2 table_shape");
    SCN_CHECK(pd_sm_table_len == 85);
    bool seen[PD_ST__COUNT] = {0};
    int any_idx = -1;
    for (size_t i = 0; i < pd_sm_table_len; i++) {
        const pd_transition_t *r = &pd_sm_table[i];
        if (r->state == PD_ST_ANY) { if (any_idx < 0) any_idx = (int)i; }
        else {
            SCN_CHECK(any_idx < 0);           /* ANY rows come last */
            SCN_CHECK(r->state < PD_ST__COUNT);
            seen[r->state] = true;
        }
        if (r->next != PD_ST_SAME) seen[r->next] = true;
        SCN_CHECK(r->event > PD_EV_NONE && r->event < PD_EV__COUNT);
    }
    for (int s = 0; s < PD_ST__COUNT; s++) SCN_CHECK(seen[s]);
    for (int e = 0; e < PD_EV__COUNT; e++) SCN_CHECK(strcmp(pd_sm_event_name((pd_event_t)e), "?") != 0);
    done();
}

static void m_want_unattached(void)
{
    scn_begin("M3 want_logged_anywhere");
    scn_boot();
    pd_sm_set_want(&S.sm, 9000);
    scn_tick(1);
    SCN_CHECK(HAS("pd.want", "\"mv\":9000", NULL));
    SCN_CHECK(S.sm.state == PD_ST_UNATTACHED);
    done();
}

int main(int argc, char **argv)
{
    (void)argv;
    scn_verbose = argc > 1;
    printf("test_sm (PD_DRV_BURST_POLL=%d)\n", PD_DRV_BURST_POLL);
    t01(); t02(); t03(); t04(); t05(); t06();
    t07(); t08(); t09(); t10(); t11(); t12(); t13(); t14(); t15();
    t16(); t17(); t18(); t19(); t20(); t21(); t22(); t23(); t24();
    t25(); t26(); t27(); t28(); t29();
    t30(); t31(); t32(); t33(); t34(); t34bcd(); t35(); t36(); t37(); t39();
    m_jsonl(); m_table(); m_want_unattached();
    /* §2.1 budget at 400 kHz: 9 bit-times per byte = 22.5 us, plus ~50 us driver
     * overhead per transaction (estimate; the firmware measures the real value). */
    printf("I2C per step worst case: %u transactions, %u bytes -> est. %u us\n",
           g_io_max_all, g_bytes_max_all, g_bytes_max_all * 45 / 2 + g_io_max_all * 50);
    /* every log_begin in the fw core is paired with log_end and nesting stays
     * within the static pool (fw/src/log.h "Stack budget") */
    if (log_pool_busy()) { printf("  FAIL log pool slots leaked: %u\n", log_pool_busy()); total_fails++; }
    if (log_pool_miss()) { printf("  FAIL log pool misses: %u\n", (unsigned)log_pool_miss()); total_fails++; }
    printf("%d scenario runs, %d failure(s)\n", ntests, total_fails);
    return total_fails ? 1 : 0;
}
