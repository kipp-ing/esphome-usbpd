#include "pd_drv.h"
#include "fusb302_regs.h"
#include "log.h"

static uint32_t now(pd_sm_t *sm) { return sm->ops->now_ms(sm->ops->ctx); }

int drv_write(pd_sm_t *sm, uint8_t reg, uint8_t val) { return sm->ops->reg_write(sm->ops->ctx, reg, val); }
int drv_read(pd_sm_t *sm, uint8_t reg, uint8_t *val) { return sm->ops->reg_read(sm->ops->ctx, reg, val); }

void proto_reset(pd_sm_t *sm)
{
    sm->tx_msgid = 0;
    sm->rx_last_id = -1;
    sm->tx_pending = false;
}

static void log_txres(pd_sm_t *sm, const char *res)
{
    log_line_t l;
    log_begin(&l, "pd.txres");
    log_str(&l, "res", res);
    log_uint(&l, "id", sm->tx_id);
    log_end(&l);
}

static void tx_success(pd_sm_t *sm, const char *res)
{
    sm->tx_pending = false;
    sm->tx_msgid = (uint8_t)((sm->tx_msgid + 1) & 7);
    pd_sm_post(sm, PD_EV_TX_SENT);
    log_txres(sm, res);
}

static void tx_fail(pd_sm_t *sm, const char *why)
{
    sm->tx_pending = false;
    pd_sm_post(sm, PD_EV_TX_FAIL);
    log_line_t l;
    log_begin(&l, "pd.txres");
    log_str(&l, "res", "fail");
    log_uint(&l, "id", sm->tx_id);
    if (why) log_bool(&l, why, true);
    log_end(&l);
}

static int write_burst(pd_sm_t *sm)
{
    if (drv_write(sm, FUSB_CONTROL0, 0x64)) return -1;          /* TX flush */
    return sm->ops->fifo_write(sm->ops->ctx, sm->tx_buf, sm->tx_len);
}

int drv_tx(pd_sm_t *sm, uint8_t type, uint8_t ndo, const uint32_t *objs)
{
    uint16_t hdr = pd_hdr_tx(type, sm->tx_msgid, ndo);
    sm->tx_len = pd_tx_burst(hdr, objs, ndo, sm->tx_buf);
    sm->tx_id = sm->tx_msgid;
    if (write_burst(sm)) {
        tx_fail(sm, "i2c");
        return -1;
    }
    sm->tx_pending = true;
    sm->tx_coll = false;
    sm->tx_deadline = now(sm) + PD_DRV_TX_GUARD_MS;
    log_line_t l;
    log_begin(&l, "pd.tx");
    log_str(&l, "type", pd_tx_name(type, ndo));
    log_uint(&l, "id", sm->tx_id);
    log_hexn(&l, "hdr", hdr, 4);
    if (ndo) log_hex32_arr(&l, "obj", objs, ndo);
    log_bytes(&l, "bytes", sm->tx_buf, sm->tx_len);
    log_end(&l);
    return 0;
}

void drv_hard_reset(pd_sm_t *sm)
{
    drv_write(sm, FUSB_CONTROL3, 0x07 | FUSB_CTL3_SEND_HARD_RESET);
    sm->tx_pending = false;
}

/* ---- RX (§2.1 step 6) ---------------------------------------------------- */
static pd_event_t rx_event(pd_rx_class_t c)
{
    if (c.cls == PD_CLS_UNEXPECTED) return PD_EV_RX_UNEXPECTED;
    if (c.cls == PD_CLS_UNSUPPORTED) return PD_EV_RX_UNSUPPORTED;
    if (c.cls == PD_CLS_IGNORED) return PD_EV_NONE;
    switch (c.kind) {
    case PD_RXK_SRC_CAPS:     return PD_EV_RX_SRC_CAPS;
    case PD_RXK_ACCEPT:       return PD_EV_RX_ACCEPT;
    case PD_RXK_REJECT:       return PD_EV_RX_REJECT;
    case PD_RXK_WAIT:         return PD_EV_RX_WAIT;
    case PD_RXK_PS_RDY:       return PD_EV_RX_PS_RDY;
    case PD_RXK_SOFT_RESET:   return PD_EV_RX_SOFT_RESET;
    case PD_RXK_GET_SINK_CAP: return PD_EV_RX_GET_SINK_CAP;
    case PD_RXK_PING:         return PD_EV_RX_PING;
    case PD_RXK_GOTOMIN:      return PD_EV_RX_GOTOMIN;
    default:                  return PD_EV_RX_UNSUPPORTED;
    }
}

static void log_rx(uint16_t hdr, const uint32_t *obj, const char *cls, bool other)
{
    char nb[16];
    uint8_t ndo = PD_HDR_NDO(hdr);
    log_line_t l;
    log_begin(&l, "pd.rx");
    log_str(&l, "type", pd_msg_name(hdr, nb, sizeof nb));
    log_uint(&l, "id", PD_HDR_ID(hdr));
    log_uint(&l, "ndo", ndo);
    log_uint(&l, "rev", PD_HDR_REV(hdr));
    log_hexn(&l, "hdr", hdr, 4);
    log_str(&l, "cls", cls);
    if (ndo) log_hex32_arr(&l, "obj", obj, ndo);
    if (other) log_str(&l, "sop", "other");
    log_end(&l);
}

static void store_caps(pd_sm_t *sm, uint8_t ndo, const uint32_t *obj)
{
    uint32_t mv[PD_MAX_OBJ], ma[PD_MAX_OBJ];
    char kind[PD_MAX_OBJ + 1];
    sm->caps_n = ndo;
    for (uint8_t k = 0; k < ndo; k++) {
        sm->caps[k] = obj[k];
        pd_pdo_t d = pd_pdo_decode(obj[k]);
        kind[k] = d.kind; mv[k] = d.mv; ma[k] = d.ma;
    }
    kind[ndo] = 0;
    log_line_t l;
    log_begin(&l, "pd.caps");
    log_uint(&l, "n", ndo);
    log_str(&l, "kind", kind);
    log_uint_arr(&l, "mv", mv, ndo);
    log_uint_arr(&l, "ma", ma, ndo);
    log_end(&l);
}

static int rx_one(pd_sm_t *sm)   /* 1 = PE event posted, 0 = continue, -1 = stop */
{
    const struct pd_port_ops *o = sm->ops;
    /* §2.1 step 6.2: 2 transactions: token + header (3 B), then data + CRC (4*NDO + 4 B) */
    uint8_t th[3], d[4 * PD_MAX_OBJ + 4];
    if (o->fifo_read(o->ctx, th, sizeof th)) return -1;
    uint8_t tok = th[0];
    bool other = false;
    uint8_t tc = tok & PD_RXTOK_MASK;
    if (tc == PD_RXTOK_SOP) {
    } else if (tc == 0xC0 || tc == 0xA0 || tc == 0x80 || tc == 0x60) {
        other = true;
    } else {
        drv_write(sm, FUSB_CONTROL1, FUSB_CTL1_RX_FLUSH);
        log_line_t l;
        log_begin(&l, "pd.rxraw");
        log_hexn(&l, "tok", tok, 2);
        log_end(&l);
        return -1;
    }
    uint16_t hdr = (uint16_t)(th[1] | (th[2] << 8));
    uint8_t ndo = PD_HDR_NDO(hdr), id = PD_HDR_ID(hdr), type = PD_HDR_TYPE(hdr);
    if (o->fifo_read(o->ctx, d, 4u * ndo + 4u)) return -1;   /* CRC discarded */
    uint32_t obj[PD_MAX_OBJ] = {0};
    for (uint8_t k = 0; k < ndo; k++)
        obj[k] = (uint32_t)d[4 * k] | ((uint32_t)d[4 * k + 1] << 8) |
                 ((uint32_t)d[4 * k + 2] << 16) | ((uint32_t)d[4 * k + 3] << 24);

    if (other) { log_rx(hdr, obj, "ignored", true); return 0; }

    bool ctrl = !PD_HDR_EXT(hdr) && ndo == 0;
    if (ctrl && type == PD_CTRL_GOODCRC) {
        bool match = id == sm->tx_id;
        log_line_t l;
        log_begin(&l, "pd.gcrc");
        log_uint(&l, "id", id);
        log_bool(&l, "match", match);
        log_end(&l);
        if (sm->tx_pending && match) tx_success(sm, "gcrc");
        return 0;
    }
    if ((int)id == sm->rx_last_id && !(ctrl && type == PD_CTRL_SOFT_RESET)) {
        char nb[16];
        log_line_t l;
        log_begin(&l, "pd.dup");
        log_str(&l, "type", pd_msg_name(hdr, nb, sizeof nb));
        log_uint(&l, "id", id);
        log_end(&l);
        return 0;
    }
    pd_rx_class_t c = pd_classify(hdr, obj[0]);
    sm->rx_last_id = (int8_t)id;
    sm->rx_hdr = hdr;
    sm->rx_ndo = ndo;
    sm->rx_type = type;
    for (uint8_t k = 0; k < PD_MAX_OBJ; k++) sm->rx_obj[k] = obj[k];
    log_rx(hdr, obj, pd_cls_name(c.cls), false);
    if (c.kind == PD_RXK_SRC_CAPS) {
        store_caps(sm, ndo, obj);
    } else if (c.caps_bad) {
        log_line_t l;
        log_begin(&l, "pd.caps.bad");
        log_hexn(&l, "hdr", hdr, 4);
        log_hexn(&l, "obj0", obj[0], 8);
        log_end(&l);
    }
    pd_event_t ev = rx_event(c);
    if (ev == PD_EV_NONE) return 0;
    pd_sm_post(sm, ev);
    return 1;
}

/* ---- poll (§2.1) ---------------------------------------------------------- */
void pd_drv_poll(pd_sm_t *sm)
{
    /* step 1: 0x3E Interrupta, 0x3F Interruptb, 0x40 Status0, 0x41 Status1, 0x42 Interrupt.
     * Read-to-clear registers are read exactly once; on failure the step is abandoned. */
    uint8_t r[5];
#if PD_DRV_BURST_POLL
    if (sm->ops->reg_read_burst(sm->ops->ctx, FUSB_INTERRUPTA, r, sizeof r)) return;
#else
    for (uint8_t k = 0; k < sizeof r; k++)
        if (drv_read(sm, (uint8_t)(FUSB_INTERRUPTA + k), &r[k])) return;
#endif
    uint8_t ia = r[0], st0 = r[2], st1 = r[3], ir = r[4];
    sm->st0 = st0;
    sm->st1 = st1;

    bool hr_rx = (ia & FUSB_IA_HARDRST) != 0;
    if (hr_rx) {                                              /* step 2 */
        pd_sm_post(sm, PD_EV_HR_RX);
        drv_write(sm, FUSB_CONTROL1, FUSB_CTL1_RX_FLUSH);
        sm->tx_pending = false;
        log_line_t l;
        log_begin(&l, "pd.hr");
        log_str(&l, "dir", "rx");
        log_end(&l);
    }
    if (ia & FUSB_IA_HARDSENT) pd_sm_post(sm, PD_EV_HARD_SENT);   /* step 3 */

    if (!hr_rx && sm->tx_pending) {                           /* step 4 */
        if (ia & FUSB_IA_RETRYFAIL) {
            tx_fail(sm, NULL);
        } else if (ia & FUSB_IA_TXSENT) {
            tx_success(sm, "sent");
        } else if (ir & FUSB_I_COLLISION) {
            if (!sm->tx_coll) {
                sm->tx_coll = true;
                if (write_burst(sm)) {
                    tx_fail(sm, "i2c");
                } else {
                    sm->tx_deadline = now(sm) + PD_DRV_TX_GUARD_MS;
                    log_txres(sm, "coll");
                }
            } else {
                tx_fail(sm, "coll");
            }
        } else if ((int32_t)(now(sm) - sm->tx_deadline) >= 0) {
            tx_success(sm, "tmo");
        }
    }

    bool v = (st0 & FUSB_ST0_VBUSOK) != 0;                    /* step 5 */
    if (!sm->vbus_known || v != sm->vbus) {
        sm->vbus = v;
        sm->vbus_known = true;
        pd_sm_post(sm, v ? PD_EV_VBUS_ON : PD_EV_VBUS_OFF);
        log_line_t l;
        log_begin(&l, "vbus");
        log_bool(&l, "on", v);
        log_end(&l);
    }

    if (hr_rx) return;
    for (int k = 0; k < PD_DRV_RX_MAX_FRAMES; k++) {          /* step 6 */
        if (k > 0 && drv_read(sm, FUSB_STATUS1, &st1)) break;
        if (st1 & FUSB_ST1_RX_EMPTY) break;
        if (rx_one(sm) != 0) break;
    }
}
