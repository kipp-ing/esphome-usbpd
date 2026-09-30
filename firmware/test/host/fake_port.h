/* Fake FUSB302: 256-byte register model + RX/TX FIFO + controllable clock.
 * Implements struct pd_port_ops for the host sim. */
#ifndef FAKE_PORT_H
#define FAKE_PORT_H

#include <stdint.h>
#include <stddef.h>
#include "pd_sm.h"

#define FAKE_FIFO_LEN 80

typedef struct {
    uint32_t now;
    uint8_t reg[256];
    uint8_t rx[256]; size_t rx_len, rx_pos;     /* bytes the SM will read from FIFOS */
    uint8_t tx[256]; size_t tx_len;             /* bytes the SM wrote to FIFOS */
    uint32_t reads, writes;
    int fail_next;                              /* >0: next N accesses return -1 */
} fake_port_t;

void fake_port_init(fake_port_t *f, struct pd_port_ops *ops);
void fake_rx_push(fake_port_t *f, const uint8_t *b, size_t n);

/* Log capture for assertions (installed as the log sink + clock). */
void logcap_install(fake_port_t *f);
void logcap_clear(void);
const char *logcap_text(void);
int logcap_count(const char *needle);    /* occurrences of substring */
void logcap_echo(int on);                /* also print lines to stdout */

#endif
