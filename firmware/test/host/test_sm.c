/* Proves the SM mechanism: table dispatch, guards, per-state timers,
 * auto-cancel on exit, wildcard rows, transition logging, JSONL format. */
#include <stdio.h>
#include <string.h>
#include "scenario.h"
#include "fusb302_regs.h"
#include "log.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("  FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void test_attach_debounce(void)
{
    static const scn_step_t s[] = {
        { 5,   SCN_SETBITS, .reg = FUSB_STATUS0, .val = FUSB_ST0_VBUSOK },
        { 5,   SCN_EXPECT_STATE, .state = PD_ST_ATTACH_WAIT },
        { 104, SCN_EXPECT_STATE, .state = PD_ST_ATTACH_WAIT },   /* timer 100 ms from t=5 */
        { 105, SCN_EXPECT_STATE, .state = PD_ST_ATTACHED },
        { 105, SCN_EXPECT_LOG, .text = "\"ev\":\"sm\",\"from\":\"AttachWait\",\"to\":\"Attached\",\"on\":\"timeout\"" },
        { 200, SCN_CLRBITS, .reg = FUSB_STATUS0, .val = FUSB_ST0_VBUSOK },
        { 200, SCN_EXPECT_STATE, .state = PD_ST_UNATTACHED },
        { 0, SCN_END },
    };
    fails += scn_run("attach debounce + detach", s, 5);
}

static void test_bounce_cancels_timer(void)
{
    /* VBUS drops inside the debounce window: timer must be cancelled on exit,
     * so no stray timeout fires later. */
    static const scn_step_t s[] = {
        { 10,  SCN_SETBITS, .reg = FUSB_STATUS0, .val = FUSB_ST0_VBUSOK },
        { 50,  SCN_CLRBITS, .reg = FUSB_STATUS0, .val = FUSB_ST0_VBUSOK },
        { 50,  SCN_EXPECT_STATE, .state = PD_ST_UNATTACHED },
        { 300, SCN_EXPECT_STATE, .state = PD_ST_UNATTACHED },
        { 0, SCN_END },
    };
    int f = scn_run("bounce cancels state timer", s, 0);
    if (logcap_count("\"on\":\"timeout\"") != 0) { printf("  FAIL stray timeout\n"); f++; }
    fails += f;
}

static void test_wildcard_internal(void)
{
    static const scn_step_t s[] = {
        { 3, SCN_WANT, .val16 = 9000 },
        { 3, SCN_EXPECT_STATE, .state = PD_ST_UNATTACHED },
        { 3, SCN_EXPECT_LOG, .text = "\"ev\":\"sm\",\"from\":\"Unattached\",\"to\":\"Unattached\",\"on\":\"want\"" },
        { 3, SCN_EXPECT_LOG, .text = "\"ev\":\"sm.want\",\"mv\":9000" },
        { 0, SCN_END },
    };
    fails += scn_run("wildcard row, internal transition", s, 0);
}

static void test_guard_fallthrough(void)
{
    /* In AttachWait, a timeout with VBUS gone (guard false) takes the next row. */
    fake_port_t f; struct pd_port_ops ops; pd_sm_t sm;
    fake_port_init(&f, &ops); logcap_install(&f);
    pd_sm_init(&sm, &ops);
    sm.state = PD_ST_ATTACH_WAIT;
    sm.vbus = false;
    CHECK(pd_sm_dispatch(&sm, PD_EV_TIMEOUT));
    CHECK(sm.state == PD_ST_UNATTACHED);
    sm.state = PD_ST_ATTACH_WAIT;
    sm.vbus = true;
    CHECK(pd_sm_dispatch(&sm, PD_EV_TIMEOUT));
    CHECK(sm.state == PD_ST_ATTACHED);
    CHECK(!pd_sm_dispatch(&sm, PD_EV_TIMEOUT));      /* no row: unhandled */
    printf("  %s guard fall-through\n", fails ? "FAIL" : "ok  ");
}

static void test_jsonl_format(void)
{
    fake_port_t f; struct pd_port_ops ops;
    fake_port_init(&f, &ops); logcap_install(&f);
    f.now = 1234;
    log_line_t l;
    log_begin(&l, "x");
    log_int(&l, "i", -5);
    log_str(&l, "s", "a\"b\n");
    uint8_t b[3] = {0x01, 0xab, 0xff};
    log_hex(&l, "h", b, 3);
    log_hex8(&l, "r", 0x0c);
    log_end(&l);
    CHECK(strcmp(logcap_text(),
        "{\"t\":1234,\"ev\":\"x\",\"i\":-5,\"s\":\"a\\\"b\\n\",\"h\":\"01abff\",\"r\":\"0x0c\"}\n") == 0);

    /* overflow must still yield valid JSON with trunc marker */
    logcap_clear();
    uint8_t big[600] = {0};
    log_begin(&l, "big");
    log_int(&l, "a", 1);
    log_hex(&l, "h", big, sizeof big);
    log_int(&l, "b", 2);
    log_end(&l);
    const char *t = logcap_text();
    CHECK(strstr(t, "\"a\":1,\"trunc\":true}\n") != NULL);
    CHECK(strchr(t, '\n') == t + strlen(t) - 1);
    printf("  %s jsonl format\n", fails ? "FAIL" : "ok  ");
}

static void test_i2c_error_tolerated(void)
{
    fake_port_t f; struct pd_port_ops ops; pd_sm_t sm;
    fake_port_init(&f, &ops); logcap_install(&f);
    pd_sm_init(&sm, &ops);
    f.reg[FUSB_STATUS0] = FUSB_ST0_VBUSOK;
    f.fail_next = 1;
    pd_sm_step(&sm);                 /* read fails: no event, no crash */
    CHECK(sm.state == PD_ST_UNATTACHED);
    f.now++;
    pd_sm_step(&sm);
    CHECK(sm.state == PD_ST_ATTACH_WAIT);
    printf("  %s i2c read error tolerated\n", fails ? "FAIL" : "ok  ");
}

int main(int argc, char **argv)
{
    scn_verbose = argc > 1 && !strcmp(argv[1], "-v");
    test_jsonl_format();
    test_attach_debounce();
    test_bounce_cancels_timer();
    test_wildcard_internal();
    test_guard_fallthrough();
    test_i2c_error_tolerated();
    if (fails) { printf("test_sm: %d FAILURE(S)\n", fails); return 1; }
    printf("test_sm: all passed\n");
    return 0;
}
