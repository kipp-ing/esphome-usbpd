#include "scenario.h"
#include <stdio.h>
#include <string.h>

int scn_verbose;

int scn_run(const char *name, const scn_step_t *steps, uint32_t tail_ms)
{
    fake_port_t f;
    struct pd_port_ops ops;
    pd_sm_t sm;
    fake_port_init(&f, &ops);
    logcap_install(&f);
    logcap_echo(scn_verbose);
    pd_sm_init(&sm, &ops);

    size_t nsteps = 0;
    uint32_t end = 0;
    while (steps[nsteps].op != SCN_END) { if (steps[nsteps].at_ms > end) end = steps[nsteps].at_ms; nsteps++; }
    end += tail_ms;

    int fails = 0;
    size_t i = 0;
    for (f.now = 0; f.now <= end; f.now++) {
        size_t j;
        /* apply stimuli due now */
        for (j = i; j < nsteps && steps[j].at_ms <= f.now; j++) {
            const scn_step_t *s = &steps[j];
            switch (s->op) {
            case SCN_SETREG:  f.reg[s->reg] = s->val; break;
            case SCN_SETBITS: f.reg[s->reg] |= s->val; break;
            case SCN_CLRBITS: f.reg[s->reg] &= (uint8_t)~s->val; break;
            case SCN_RXPUSH:  fake_rx_push(&f, s->bytes, s->n); break;
            case SCN_POST:    pd_sm_post(&sm, s->event); break;
            case SCN_WANT:    pd_sm_set_want(&sm, s->val16); break;
            default: break;
            }
        }
        pd_sm_step(&sm);
        /* check expectations due now */
        for (; i < j; i++) {
            const scn_step_t *s = &steps[i];
            switch (s->op) {
            case SCN_EXPECT_STATE:
                if (sm.state != s->state) {
                    printf("  FAIL %s @%ums: state %s, want %s\n", name, f.now,
                           pd_sm_state_name(sm.state), pd_sm_state_name(s->state));
                    fails++;
                }
                break;
            case SCN_EXPECT_LOG:
                if (!strstr(logcap_text(), s->text)) {
                    printf("  FAIL %s @%ums: log lacks %s\n", name, f.now, s->text);
                    fails++;
                }
                break;
            case SCN_EXPECT_REG:
                if (f.reg[s->reg] != s->val) {
                    printf("  FAIL %s @%ums: reg 0x%02x = 0x%02x, want 0x%02x\n", name, f.now,
                           s->reg, f.reg[s->reg], s->val);
                    fails++;
                }
                break;
            default: break;
            }
        }
    }
    printf("  %s %s\n", fails ? "FAIL" : "ok  ", name);
    if (fails && !scn_verbose) printf("---- log ----\n%s-------------\n", logcap_text());
    return fails;
}
