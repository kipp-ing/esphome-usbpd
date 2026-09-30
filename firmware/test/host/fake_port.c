#include "fake_port.h"
#include "fusb302_regs.h"
#include "log.h"
#include <stdio.h>
#include <string.h>

static int f_rd(void *ctx, uint8_t reg, uint8_t *v)
{
    fake_port_t *f = ctx;
    if (f->fail_next > 0) { f->fail_next--; return -1; }
    f->reads++;
    if (reg == FUSB_FIFOS) {
        *v = f->rx_pos < f->rx_len ? f->rx[f->rx_pos++] : 0;
        return 0;
    }
    *v = f->reg[reg];
    /* R/C interrupt registers clear on read */
    if (reg == FUSB_INTERRUPT || reg == FUSB_INTERRUPTA || reg == FUSB_INTERRUPTB) f->reg[reg] = 0;
    return 0;
}
static int f_wr(void *ctx, uint8_t reg, uint8_t v)
{
    fake_port_t *f = ctx;
    if (f->fail_next > 0) { f->fail_next--; return -1; }
    f->writes++;
    if (reg == FUSB_FIFOS) { if (f->tx_len < sizeof f->tx) f->tx[f->tx_len++] = v; return 0; }
    f->reg[reg] = v;
    return 0;
}
static int f_fifo_rd(void *ctx, uint8_t *b, size_t n)
{
    for (size_t i = 0; i < n; i++) if (f_rd(ctx, FUSB_FIFOS, &b[i])) return -1;
    return 0;
}
static int f_fifo_wr(void *ctx, const uint8_t *b, size_t n)
{
    for (size_t i = 0; i < n; i++) if (f_wr(ctx, FUSB_FIFOS, b[i])) return -1;
    return 0;
}
static uint32_t f_now(void *ctx) { return ((fake_port_t *)ctx)->now; }

void fake_port_init(fake_port_t *f, struct pd_port_ops *ops)
{
    memset(f, 0, sizeof *f);
    /* datasheet reset values */
    f->reg[FUSB_DEVICE_ID] = 0x91;
    f->reg[FUSB_SWITCHES0] = 0x03;
    f->reg[FUSB_SWITCHES1] = 0x20;
    f->reg[FUSB_MEASURE]   = 0x31;
    f->reg[FUSB_SLICE]     = 0x60;
    f->reg[FUSB_CONTROL0]  = 0x24;
    f->reg[FUSB_CONTROL2]  = 0x02;
    f->reg[FUSB_CONTROL3]  = 0x06;
    f->reg[FUSB_POWER]     = 0x01;
    f->reg[FUSB_OCPREG]    = 0x0F;
    f->reg[FUSB_STATUS1]   = 0x28;
    *ops = (struct pd_port_ops){
        .ctx = f, .reg_read = f_rd, .reg_write = f_wr,
        .fifo_read = f_fifo_rd, .fifo_write = f_fifo_wr, .now_ms = f_now,
    };
}

void fake_rx_push(fake_port_t *f, const uint8_t *b, size_t n)
{
    for (size_t i = 0; i < n && f->rx_len < sizeof f->rx; i++) f->rx[f->rx_len++] = b[i];
    f->reg[FUSB_STATUS1] &= (uint8_t)~FUSB_ST1_RX_EMPTY;
}

/* ---- log capture --------------------------------------------------------- */
static fake_port_t *s_clk;
static char s_cap[1 << 16];
static size_t s_cap_n;
static int s_echo;

static uint32_t cap_clock(void) { return s_clk ? s_clk->now : 0; }
static void cap_sink(const char *line, size_t n)
{
    if (s_echo) fwrite(line, 1, n, stdout);
    if (s_cap_n + n + 1 < sizeof s_cap) { memcpy(s_cap + s_cap_n, line, n); s_cap_n += n; s_cap[s_cap_n] = 0; }
}
void logcap_install(fake_port_t *f) { s_clk = f; log_set_clock(cap_clock); log_set_sink(cap_sink); logcap_clear(); }
void logcap_clear(void) { s_cap_n = 0; s_cap[0] = 0; }
const char *logcap_text(void) { return s_cap; }
void logcap_echo(int on) { s_echo = on; }
int logcap_count(const char *needle)
{
    int c = 0;
    for (const char *p = s_cap; (p = strstr(p, needle)); p += strlen(needle)) c++;
    return c;
}
