/* Scenario runner: a timeline of register states / RX FIFO pushes / events /
 * expectations, played against the SM with a 1 ms simulated tick.
 *
 *   static const scn_step_t s[] = {
 *       { 0,   SCN_SETREG,  .reg = FUSB_STATUS0, .val = FUSB_ST0_VBUSOK },
 *       { 150, SCN_EXPECT_STATE, .state = PD_ST_ATTACHED },
 *       { 160, SCN_RXPUSH, .bytes = {0xe0, 0xa1, 0x11}, .n = 3 },
 *       { 0,   SCN_END },
 *   };
 *   scn_run("name", s);   // returns 0 on pass
 */
#ifndef SCENARIO_H
#define SCENARIO_H

#include <stdint.h>
#include <stddef.h>
#include "pd_sm.h"
#include "fake_port.h"

typedef enum {
    SCN_END = 0,
    SCN_SETREG,        /* reg[reg] = val */
    SCN_SETBITS,       /* reg[reg] |= val */
    SCN_CLRBITS,       /* reg[reg] &= ~val */
    SCN_RXPUSH,        /* append bytes[0..n) to RX FIFO */
    SCN_POST,          /* pd_sm_post(event) */
    SCN_WANT,          /* pd_sm_set_want(val16) */
    SCN_EXPECT_STATE,  /* sm.state == state */
    SCN_EXPECT_LOG,    /* captured log contains text */
    SCN_EXPECT_REG,    /* reg[reg] == val (what the SM wrote) */
} scn_op_t;

typedef struct {
    uint32_t at_ms;    /* absolute sim time the step applies at */
    scn_op_t op;
    uint8_t reg, val;
    uint16_t val16;
    pd_state_t state;
    pd_event_t event;
    const char *text;
    uint8_t bytes[64];
    size_t n;
} scn_step_t;

/* Runs until the last step's time (+ tail_ms). Steps are applied BEFORE the
 * pd_sm_step() of their tick; expectations are checked after it. */
int scn_run(const char *name, const scn_step_t *steps, uint32_t tail_ms);

extern int scn_verbose;

#endif
