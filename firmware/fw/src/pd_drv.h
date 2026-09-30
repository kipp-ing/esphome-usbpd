/* L1 driver / protocol layer (docs/spec.md §2.1, §2.2): all FUSB302 I2C for the
 * PD path, the per-step poll, TX burst + TX result, RX FIFO read, MessageID
 * counters and the duplicate filter. Pure C over struct pd_port_ops; its state
 * lives in struct pd_sm. Posts events into the SM queue; no policy. */
#ifndef USBPD_PD_DRV_H
#define USBPD_PD_DRV_H

#include "pd_sm.h"

#define PD_DRV_TX_GUARD_MS 10   /* §5 TX guard */
#define PD_DRV_RX_MAX_FRAMES 8  /* per poll: bounds the loop on a stuck RX_EMPTY */
/* §2.1 step 1: 1 = one 5-byte burst read of 0x3E-0x42 (default); 0 = 5 single reads
 * in the same order (A/B fallback, §10 Q16). */
#ifndef PD_DRV_BURST_POLL
#define PD_DRV_BURST_POLL 1
#endif

void pd_drv_poll(pd_sm_t *sm);                                   /* §2.1 steps 1-6 */
int  drv_tx(pd_sm_t *sm, uint8_t type, uint8_t ndo, const uint32_t *objs); /* §2.2 */
void drv_hard_reset(pd_sm_t *sm);
int  drv_write(pd_sm_t *sm, uint8_t reg, uint8_t val);
int  drv_read(pd_sm_t *sm, uint8_t reg, uint8_t *val);
void proto_reset(pd_sm_t *sm);

#endif
