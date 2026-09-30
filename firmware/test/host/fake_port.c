#include "fake_port.h"
#include "fusb302_regs.h"
#include "log.h"
#include <stdio.h>
#include <string.h>

static void reset_values(fake_port_t *f)
{
    uint8_t st0 = f->reg[FUSB_STATUS0];     /* physical inputs (VBUSOK, ACTIVITY) survive SW_RES */
    memset(f->reg, 0, sizeof f->reg);
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
    f->reg[FUSB_STATUS0]   = st0;
    f->rx_len = f->rx_pos = 0;
}

uint8_t fake_status0(const fake_port_t *f)                /* F1 */
{
    uint8_t m = f->reg[FUSB_SWITCHES0] & (FUSB_SW0_MEAS_CC1 | FUSB_SW0_MEAS_CC2);
    uint8_t bc = m == FUSB_SW0_MEAS_CC1 ? f->cc_lvl[1] : m == FUSB_SW0_MEAS_CC2 ? f->cc_lvl[2] : 0;
    return (uint8_t)((f->reg[FUSB_STATUS0] & ~FUSB_ST0_BC_LVL_MASK) | (bc & FUSB_ST0_BC_LVL_MASK));
}

static int f_rd(void *ctx, uint8_t reg, uint8_t *v)
{
    fake_port_t *f = ctx;
    if (f->fail_next > 0) { f->fail_next--; return -1; }
    f->reads++;
    if (reg != FUSB_FIFOS) { f->xfers++; f->bus_bytes += 4; }   /* addrW reg addrR data */
    switch (reg) {
    case FUSB_FIFOS:
        *v = f->rx_pos < f->rx_len ? f->rx[f->rx_pos++] : 0;
        if (f->rx_pos >= f->rx_len) f->rx_len = f->rx_pos = 0;
        return 0;
    case FUSB_STATUS0:
        *v = fake_status0(f);
        return 0;
    case FUSB_STATUS1:                                    /* F2 */
        *v = (uint8_t)((f->reg[reg] & ~FUSB_ST1_RX_EMPTY) | (f->rx_pos >= f->rx_len ? FUSB_ST1_RX_EMPTY : 0));
        return 0;
    case FUSB_INTERRUPT: case FUSB_INTERRUPTA: case FUSB_INTERRUPTB:
        *v = f->reg[reg];
        f->reg[reg] = 0;                                  /* read-to-clear */
        return 0;
    default:
        *v = f->reg[reg];
        return 0;
    }
}

static void tx_capture(fake_port_t *f, const uint8_t *b, size_t n)   /* F5 + F6 */
{
    fake_burst_t *t = &f->tx[f->ntx % FAKE_TX_HIST];
    t->tick = f->now;
    t->n = n > FAKE_BURST_MAX ? FAKE_BURST_MAX : n;
    memcpy(t->b, b, t->n);
    f->ntx++;
    fake_ack_t mode = f->ack_mode;
    if (mode == ACK_COLL && f->coll_done) mode = ACK_BOTH;
    if (mode == ACK_COLL) f->coll_done = true;
    if (mode == ACK_NONE) return;
    uint8_t id = n >= 7 ? (uint8_t)((b[6] >> 1) & 7) : 0;
    for (int i = 0; i < 8; i++)
        if (!f->pend[i].active) {
            f->pend[i] = (fake_pending_ack_t){ true, f->now + f->ack_delay, mode, id };
            return;
        }
}

static int f_wr(void *ctx, uint8_t reg, uint8_t v)
{
    fake_port_t *f = ctx;
    if (f->fail_next > 0) { f->fail_next--; return -1; }
    f->writes++;
    f->xfers++; f->bus_bytes += 3;                        /* addrW reg data */
    f->wlast[reg] = v;
    f->wcount[reg]++;
    switch (reg) {
    case FUSB_FIFOS:
        tx_capture(f, &v, 1);
        return 0;
    case FUSB_CONTROL0:                                   /* F3: TX_FLUSH self-clears */
        f->reg[reg] = v & (uint8_t)~FUSB_CTL0_TX_FLUSH;
        return 0;
    case FUSB_CONTROL1:                                   /* F3: RX_FLUSH empties RX, self-clears */
        if (v & FUSB_CTL1_RX_FLUSH) f->rx_len = f->rx_pos = 0;
        f->reg[reg] = v & (uint8_t)~FUSB_CTL1_RX_FLUSH;
        return 0;
    case FUSB_RESET:                                      /* F4 */
        if (v & FUSB_RESET_SW_RES) { f->sw_resets++; reset_values(f); }
        if (v & FUSB_RESET_PD_RESET) f->pd_resets++;
        f->reg[reg] = 0;
        return 0;
    case FUSB_STATUS0: case FUSB_STATUS1: case FUSB_DEVICE_ID:
        return 0;                                         /* read-only */
    default:
        f->reg[reg] = v;
        return 0;
    }
}

static int f_fifo_rd(void *ctx, uint8_t *b, size_t n)
{
    fake_port_t *f = ctx;
    f->xfers++; f->bus_bytes += 3 + (uint32_t)n;
    for (size_t i = 0; i < n; i++) if (f_rd(ctx, FUSB_FIFOS, &b[i])) return -1;
    return 0;
}
static int f_fifo_wr(void *ctx, const uint8_t *b, size_t n)
{
    fake_port_t *f = ctx;
    if (f->fail_next > 0) { f->fail_next--; return -1; }
    f->writes++;
    f->xfers++; f->bus_bytes += 2 + (uint32_t)n;
    f->wcount[FUSB_FIFOS]++;
    tx_capture(f, b, n);                                  /* one call = one burst */
    return 0;
}
static uint32_t f_now(void *ctx) { return ((fake_port_t *)ctx)->now; }

void fake_port_init(fake_port_t *f, struct pd_port_ops *ops)
{
    memset(f, 0, sizeof *f);
    reset_values(f);
    f->ack_mode = ACK_BOTH;
    f->ack_delay = 1;
    *ops = (struct pd_port_ops){
        .ctx = f, .reg_read = f_rd, .reg_write = f_wr,
        .fifo_read = f_fifo_rd, .fifo_write = f_fifo_wr, .now_ms = f_now,
    };
}

static void rx_append(fake_port_t *f, const uint8_t *b, size_t n)
{
    for (size_t i = 0; i < n && f->rx_len < sizeof f->rx; i++) f->rx[f->rx_len++] = b[i];
}

void fake_rx_push(fake_port_t *f, const uint8_t *b, size_t n)
{
    rx_append(f, b, n);
    f->last_rxpush = f->now;
}

void fake_set_ack(fake_port_t *f, fake_ack_t mode, uint32_t delay)
{
    f->ack_mode = mode;
    f->ack_delay = delay;
    f->coll_done = false;
}

void fake_tick(fake_port_t *f)
{
    for (int i = 0; i < 8; i++) {
        fake_pending_ack_t *p = &f->pend[i];
        if (!p->active || (int32_t)(f->now - p->at) < 0) continue;
        p->active = false;
        switch (p->mode) {
        case ACK_TXSENT:    f->reg[FUSB_INTERRUPTA] |= FUSB_IA_TXSENT; break;
        case ACK_RETRYFAIL: f->reg[FUSB_INTERRUPTA] |= FUSB_IA_RETRYFAIL; break;
        case ACK_COLL:      f->reg[FUSB_INTERRUPT] |= FUSB_I_COLLISION; break;
        case ACK_GCRC: case ACK_BOTH: {
            if (p->mode == ACK_BOTH) f->reg[FUSB_INTERRUPTA] |= FUSB_IA_TXSENT;
            uint16_t h = (uint16_t)(0x01A1 | (p->id << 9));   /* GoodCRC, R3, source */
            uint8_t fr[7] = { 0xE0, (uint8_t)h, (uint8_t)(h >> 8), 0, 0, 0, 0 };
            rx_append(f, fr, sizeof fr);
            break;
        }
        default: break;
        }
    }
}

const fake_burst_t *fake_last_tx(const fake_port_t *f)
{
    return f->ntx ? &f->tx[(f->ntx - 1) % FAKE_TX_HIST] : NULL;
}

/* ---- log capture (F7) ------------------------------------------------------ */
#define CAP_BYTES (1 << 21)
#define CAP_LINES 32768
static fake_port_t *s_clk;
static char s_cap[CAP_BYTES];
static size_t s_cap_n;
static uint32_t s_off[CAP_LINES], s_tick[CAP_LINES];
static int s_nl;
static int s_echo;

static uint32_t cap_clock(void) { return s_clk ? s_clk->now : 0; }
static void cap_sink(const char *line, size_t n)
{
    if (s_echo) fwrite(line, 1, n, stdout);
    if (n && line[n - 1] == '\n') n--;
    if (s_nl >= CAP_LINES || s_cap_n + n + 1 >= CAP_BYTES) return;
    s_off[s_nl] = (uint32_t)s_cap_n;
    s_tick[s_nl] = s_clk ? s_clk->now : 0;
    memcpy(s_cap + s_cap_n, line, n);
    s_cap_n += n;
    s_cap[s_cap_n++] = 0;
    s_nl++;
}
void logcap_install(fake_port_t *f) { s_clk = f; log_set_clock(cap_clock); log_set_sink(cap_sink); logcap_clear(); }
void logcap_clear(void) { s_cap_n = 0; s_nl = 0; }
void logcap_echo(int on) { s_echo = on; }
int logcap_nlines_total(void) { return s_nl; }
const char *logcap_line(int i) { return (i >= 0 && i < s_nl) ? s_cap + s_off[i] : ""; }
uint32_t logcap_tick(int i) { return (i >= 0 && i < s_nl) ? s_tick[i] : 0; }
void logcap_dump(void) { for (int i = 0; i < s_nl; i++) printf("    %s\n", logcap_line(i)); }

int logcap_count(const char *needle)
{
    int c = 0;
    for (int i = 0; i < s_nl; i++) if (strstr(logcap_line(i), needle)) c++;
    return c;
}

int logcap_find(int from, const char *ev, const char *f1, const char *f2)
{
    char evs[80];
    if (ev) snprintf(evs, sizeof evs, "\"ev\":\"%s\"", ev);
    for (int i = from < 0 ? 0 : from; i < s_nl; i++) {
        const char *l = logcap_line(i);
        if (ev && !strstr(l, evs)) continue;
        if (f1 && !strstr(l, f1)) continue;
        if (f2 && !strstr(l, f2)) continue;
        return i;
    }
    return -1;
}

int logcap_nlines(const char *ev, const char *f1, const char *f2)
{
    int c = 0;
    for (int i = logcap_find(0, ev, f1, f2); i >= 0; i = logcap_find(i + 1, ev, f1, f2)) c++;
    return c;
}
