#include "scenario.h"
#include "fusb302_regs.h"
#include <stdio.h>
#include <string.h>

scn_t S;
int scn_verbose;

void scn_begin(const char *name)
{
    memset(&S, 0, sizeof S);
    S.name = name;
    fake_port_init(&S.f, &S.ops);
    logcap_install(&S.f);
    logcap_echo(scn_verbose);
}

uint32_t g_io_max_all, g_bytes_max_all;

void scn_boot(void) { pd_sm_init(&S.sm, &S.ops); }

void scn_tick(uint32_t n)
{
    while (n--) {
        S.f.now++;
        fake_tick(&S.f);
        uint32_t x0 = S.f.xfers, b0 = S.f.bus_bytes;
        pd_sm_step(&S.sm);
        uint32_t x = S.f.xfers - x0, b = S.f.bus_bytes - b0;
        if (x > S.io_max) S.io_max = x;
        if (b > S.bytes_max) S.bytes_max = b;
        if (x > g_io_max_all) g_io_max_all = x;
        if (b > g_bytes_max_all) g_bytes_max_all = b;
    }
}

int scn_until_state(pd_state_t st, uint32_t limit)
{
    for (uint32_t i = 0; i <= limit; i++) {
        if (S.sm.state == st) return (int)i;
        if (i < limit) scn_tick(1);
    }
    return -1;
}

void scn_fail(const char *file, int line, const char *what)
{
    printf("  FAIL %s @%ums %s:%d: %s\n", S.name, S.f.now, file, line, what);
    S.fails++;
}

int scn_end(void)
{
    printf("  %s %s\n", S.fails ? "FAIL" : "ok  ", S.name);
    if (S.fails && !scn_verbose) {
        printf("---- log ----\n");
        for (int i = 0; i < logcap_nlines_total(); i++) printf("    %s\n", logcap_line(i));
        printf("-------------\n");
    }
    return S.fails;
}

void scn_vbus(bool on)
{
    if (on) S.f.reg[FUSB_STATUS0] |= FUSB_ST0_VBUSOK;
    else S.f.reg[FUSB_STATUS0] &= (uint8_t)~FUSB_ST0_VBUSOK;
}
void scn_setcc(uint8_t a, uint8_t b) { S.f.cc_lvl[1] = a; S.f.cc_lvl[2] = b; }
void scn_setbits(uint8_t reg, uint8_t v) { S.f.reg[reg] |= v; }
void scn_clrbits(uint8_t reg, uint8_t v) { S.f.reg[reg] &= (uint8_t)~v; }
void scn_push(const uint8_t *b, size_t n) { fake_rx_push(&S.f, b, n); }

uint16_t scn_src_hdr(uint8_t type, uint8_t id, uint8_t ndo)
{
    return (uint16_t)(0x01A0 | type | (id & 7) << 9 | (ndo & 7) << 12);
}

void scn_push_msg(uint16_t hdr, const uint32_t *obj, uint8_t ndo)
{
    uint8_t b[3 + 4 * 7 + 4];
    size_t k = 0;
    b[k++] = 0xE0;
    b[k++] = (uint8_t)hdr;
    b[k++] = (uint8_t)(hdr >> 8);
    for (uint8_t i = 0; i < ndo; i++)
        for (int j = 0; j < 4; j++) b[k++] = (uint8_t)(obj[i] >> (8 * j));
    for (int j = 0; j < 4; j++) b[k++] = 0;
    scn_push(b, k);
}

bool scn_last_tx_is(const uint8_t *b, size_t n)
{
    const fake_burst_t *t = fake_last_tx(&S.f);
    if (!t || t->n != n || memcmp(t->b, b, n) != 0) {
        printf("    last tx:");
        if (t) for (size_t i = 0; i < t->n; i++) printf(" %02X", t->b[i]);
        printf("\n    want:   ");
        for (size_t i = 0; i < n; i++) printf(" %02X", b[i]);
        printf("\n");
        return false;
    }
    return true;
}

bool scn_tx_within(uint32_t ticks)
{
    const fake_burst_t *t = fake_last_tx(&S.f);
    return t && t->tick >= S.f.last_rxpush && t->tick - S.f.last_rxpush <= ticks;
}

bool scn_log_order(const char *ev_a, const char *fa, const char *ev_b, const char *fb)
{
    /* true if some line B follows the first line A */
    int a = logcap_find(0, ev_a, fa, NULL);
    return a >= 0 && logcap_find(a + 1, ev_b, fb, NULL) > a;
}

void scn_prologue(void)
{
    scn_boot();
    scn_setcc(2, 0);
    scn_vbus(true);
    scn_tick(120);
    SCN_CHECK(S.sm.state == PD_ST_WAIT_CAPS);
    SCN_CHECK(S.f.reg[FUSB_SWITCHES0] == 0x07);
    SCN_CHECK(S.f.reg[FUSB_SWITCHES1] == 0x25);
}

static const uint32_t D1[4] = { 0x0801912C, 0x0002D12C, 0x0004B12C, 0x000641F4 };
void scn_push_d1(uint8_t id) { scn_push_msg(scn_src_hdr(PD_DATA_SOURCE_CAP, id, 4), D1, 4); }

void scn_contract_5v(void)
{
    scn_prologue();
    scn_push_d1(0);
    scn_tick(3);
    SCN_CHECK(S.sm.state == PD_ST_SELECT_CAP);
    scn_push_msg(scn_src_hdr(PD_CTRL_ACCEPT, 1, 0), NULL, 0);
    scn_tick(1);
    SCN_CHECK(S.sm.state == PD_ST_TRANS_SINK);
    scn_push_msg(scn_src_hdr(PD_CTRL_PS_RDY, 2, 0), NULL, 0);
    scn_tick(1);
    SCN_CHECK(S.sm.state == PD_ST_READY);
    SCN_CHECK(S.sm.have_contract && S.sm.con_mv == 5000);
}

/* ---- table runner ------------------------------------------------------- */
static void apply(const scn_step_t *s)
{
    switch (s->op) {
    case SCN_SETREG:  S.f.reg[s->reg] = s->val; break;
    case SCN_SETBITS: scn_setbits(s->reg, s->val); break;
    case SCN_CLRBITS: scn_clrbits(s->reg, s->val); break;
    case SCN_SETCC:   scn_setcc(s->val, s->val2); break;
    case SCN_RXPUSH:  scn_push(s->bytes, s->n); break;
    case SCN_POST:    pd_sm_post(&S.sm, s->event); break;
    case SCN_WANT:    pd_sm_set_want(&S.sm, s->val16); break;
    case SCN_ACK:     fake_set_ack(&S.f, (fake_ack_t)s->val, s->val16); break;
    default: break;
    }
}

static void expect(const scn_step_t *s)
{
    char msg[160];
    switch (s->op) {
    case SCN_EXPECT_STATE:
        if (S.sm.state != s->state) {
            snprintf(msg, sizeof msg, "state %s, want %s", pd_sm_state_name(S.sm.state),
                     pd_sm_state_name(s->state));
            scn_fail(__FILE__, __LINE__, msg);
        }
        break;
    case SCN_EXPECT_LOG:
        if (logcap_count(s->text) == 0) {
            snprintf(msg, sizeof msg, "log lacks %s", s->text);
            scn_fail(__FILE__, __LINE__, msg);
        }
        break;
    case SCN_EXPECT_REG:
        if (S.f.reg[s->reg] != s->val) {
            snprintf(msg, sizeof msg, "reg 0x%02X = 0x%02X, want 0x%02X", s->reg, S.f.reg[s->reg], s->val);
            scn_fail(__FILE__, __LINE__, msg);
        }
        break;
    case SCN_EXPECT_TX:
        if (!scn_last_tx_is(s->bytes, s->n)) scn_fail(__FILE__, __LINE__, "EXPECT_TX");
        break;
    case SCN_EXPECT_NTX:
        if (S.f.ntx != s->val16) {
            snprintf(msg, sizeof msg, "ntx %u, want %u", S.f.ntx, s->val16);
            scn_fail(__FILE__, __LINE__, msg);
        }
        break;
    case SCN_EXPECT_LOG_ORDER:
        if (!scn_log_order(NULL, s->text, NULL, s->text2)) {
            snprintf(msg, sizeof msg, "log order: %s before %s", s->text, s->text2);
            scn_fail(__FILE__, __LINE__, msg);
        }
        break;
    case SCN_EXPECT_TX_WITHIN:
        if (!scn_tx_within(s->val16)) scn_fail(__FILE__, __LINE__, "EXPECT_TX_WITHIN");
        break;
    default: break;
    }
}

int scn_run(const char *name, const scn_step_t *steps, uint32_t tail_ms)
{
    scn_begin(name);
    size_t nsteps = 0;
    uint32_t end = 0;
    while (steps[nsteps].op != SCN_END) {
        if (steps[nsteps].at_ms > end) end = steps[nsteps].at_ms;
        nsteps++;
    }
    end += tail_ms;
    size_t i = 0;
    for (; i < nsteps && steps[i].at_ms == 0; i++) apply(&steps[i]);
    scn_boot();
    for (size_t k = 0; k < i; k++) expect(&steps[k]);
    while (S.f.now < end) {
        uint32_t t = S.f.now + 1;
        size_t j;
        for (j = i; j < nsteps && steps[j].at_ms <= t; j++) apply(&steps[j]);
        scn_tick(1);
        for (; i < j; i++) expect(&steps[i]);
    }
    return scn_end();
}
