#include "pd_msg.h"
#include <stdio.h>

uint16_t pd_hdr_tx(uint8_t type, uint8_t id, uint8_t ndo)
{
    return (uint16_t)(0x0040u | (type & 0x1Fu) | ((id & 7u) << 9) | ((ndo & 7u) << 12));
}

pd_rx_class_t pd_classify(uint16_t hdr, uint32_t obj0)
{
    pd_rx_class_t r = { PD_RXK_NONE, PD_CLS_UNSUPPORTED, false };
    uint8_t t = PD_HDR_TYPE(hdr);
    if (PD_HDR_EXT(hdr)) return r;
    if (PD_HDR_NDO(hdr) == 0) {
        r.cls = PD_CLS_OK;
        switch (t) {
        case PD_CTRL_GOODCRC:      r.kind = PD_RXK_GOODCRC; break;
        case PD_CTRL_GOTOMIN:      r.kind = PD_RXK_GOTOMIN; break;
        case PD_CTRL_ACCEPT:       r.kind = PD_RXK_ACCEPT; break;
        case PD_CTRL_REJECT:       r.kind = PD_RXK_REJECT; break;
        case PD_CTRL_PING:         r.kind = PD_RXK_PING; break;
        case PD_CTRL_PS_RDY:       r.kind = PD_RXK_PS_RDY; break;
        case PD_CTRL_GET_SINK_CAP: r.kind = PD_RXK_GET_SINK_CAP; break;
        case PD_CTRL_WAIT:         r.kind = PD_RXK_WAIT; break;
        case PD_CTRL_SOFT_RESET:   r.kind = PD_RXK_SOFT_RESET; break;
        default:                   r.cls = PD_CLS_UNSUPPORTED; break; /* 0, 7, 9-11, 14-31 */
        }
        return r;
    }
    switch (t) {
    case PD_DATA_SOURCE_CAP:
        if (pd_pdo_is_fixed(obj0)) { r.cls = PD_CLS_OK; r.kind = PD_RXK_SRC_CAPS; }
        else { r.cls = PD_CLS_UNEXPECTED; r.caps_bad = true; }
        break;
    case PD_DATA_REQUEST:
    case PD_DATA_SINK_CAP: r.cls = PD_CLS_UNEXPECTED; break;
    case PD_DATA_BIST:
    case PD_DATA_VDM:      r.cls = PD_CLS_IGNORED; break;
    default:               r.cls = PD_CLS_UNSUPPORTED; break;
    }
    return r;
}

const char *pd_cls_name(pd_cls_t c)
{
    switch (c) {
    case PD_CLS_OK: return "ok";
    case PD_CLS_UNEXPECTED: return "unexpected";
    case PD_CLS_UNSUPPORTED: return "unsupported";
    default: return "ignored";
    }
}

static const char *const ctrl_names[14] = {
    NULL, "GoodCRC", "GotoMin", "Accept", "Reject", "Ping", "PS_RDY", "Get_Source_Cap",
    "Get_Sink_Cap", "DR_Swap", "PR_Swap", "VCONN_Swap", "Wait", "Soft_Reset",
};

static const char *data_name(uint8_t t)
{
    switch (t) {
    case PD_DATA_SOURCE_CAP: return "Source_Capabilities";
    case PD_DATA_REQUEST:    return "Request";
    case PD_DATA_BIST:       return "BIST";
    case PD_DATA_SINK_CAP:   return "Sink_Capabilities";
    case PD_DATA_VDM:        return "Vendor_Defined";
    default:                 return NULL;
    }
}

const char *pd_msg_name(uint16_t hdr, char *buf, size_t n)
{
    uint8_t t = PD_HDR_TYPE(hdr);
    if (PD_HDR_EXT(hdr)) { snprintf(buf, n, "Unknown_E%u", t); return buf; }
    if (PD_HDR_NDO(hdr) == 0) {
        if (t < 14 && ctrl_names[t]) return ctrl_names[t];
        snprintf(buf, n, "Unknown_C%u", t);
        return buf;
    }
    const char *s = data_name(t);
    if (s) return s;
    snprintf(buf, n, "Unknown_D%u", t);
    return buf;
}

const char *pd_tx_name(uint8_t type, uint8_t ndo)
{
    if (ndo == 0) return (type < 14 && ctrl_names[type]) ? ctrl_names[type] : "Unknown";
    const char *s = data_name(type);
    return s ? s : "Unknown";
}

pd_pdo_t pd_pdo_decode(uint32_t pdo)
{
    pd_pdo_t d = {0};
    switch (pdo >> 30) {
    case 0: /* fixed */
        d.kind = 'F';
        d.mv = ((pdo >> 10) & 0x3FF) * 50;
        d.ma = (pdo & 0x3FF) * 10;
        d.unconstrained = (pdo >> 27) & 1;
        break;
    case 1: { /* battery: max V 29:20 (50 mV), max power 9:0 (250 mW) */
        d.kind = 'B';
        d.mv = ((pdo >> 20) & 0x3FF) * 50;
        uint32_t mw = (pdo & 0x3FF) * 250;
        d.ma = d.mv ? (mw * 1000u) / d.mv : 0;
        break;
    }
    case 2: /* variable: max V 29:20 (50 mV), max I 9:0 (10 mA) */
        d.kind = 'V';
        d.mv = ((pdo >> 20) & 0x3FF) * 50;
        d.ma = (pdo & 0x3FF) * 10;
        break;
    default: /* augmented (PPS): max V 24:17 (100 mV), max I 6:0 (50 mA) */
        d.kind = 'A';
        d.mv = ((pdo >> 17) & 0xFF) * 100;
        d.ma = (pdo & 0x7F) * 50;
        break;
    }
    return d;
}

pd_sel_t pd_select(const uint32_t *caps, uint8_t n, uint32_t want_mv, bool reject_fallback)
{
    pd_sel_t s = { 1, 0, 0, PD_WHY_MIN };
    int exact = 0, below = 0;
    uint32_t below_mv = 0;
    for (uint8_t i = 0; i < n; i++) {
        if (!pd_pdo_is_fixed(caps[i])) continue;
        uint32_t mv = pd_pdo_decode(caps[i]).mv;
        if (mv == want_mv && !exact) exact = i + 1;
        if (mv <= want_mv && (!below || mv > below_mv)) { below = i + 1; below_mv = mv; }
    }
    if (reject_fallback) { s.pos = 1; s.why = PD_WHY_FALLBACK; }
    else if (exact)      { s.pos = (uint8_t)exact; s.why = PD_WHY_EXACT; }
    else if (below)      { s.pos = (uint8_t)below; s.why = PD_WHY_BELOW; }
    else                 { s.pos = 1; s.why = PD_WHY_MIN; }
    if (n == 0) { s.mv = 0; s.ma = 0; return s; }
    pd_pdo_t d = pd_pdo_decode(caps[s.pos - 1]);
    s.mv = d.mv;
    s.ma = d.ma > PD_SEL_MA_MAX ? PD_SEL_MA_MAX : d.ma;
    return s;
}

const char *pd_why_name(pd_why_t w)
{
    switch (w) {
    case PD_WHY_EXACT: return "exact";
    case PD_WHY_BELOW: return "below";
    case PD_WHY_MIN: return "min";
    default: return "fallback";
    }
}

uint32_t pd_rdo_fixed(uint8_t pos, uint32_t ma)
{
    uint32_t c = (ma / 10) & 0x3FF;
    return ((uint32_t)(pos & 7) << 28) | (c << 10) | c;
}

uint8_t pd_sink_caps(uint32_t want_mv, uint32_t *pdos)
{
    pdos[0] = 0x0001912Cu | (want_mv > 5000 ? (1u << 28) : 0);
    if (want_mv <= 5000) return 1;
    pdos[1] = (((want_mv / 50) & 0x3FF) << 10) | 300;
    return 2;
}

size_t pd_tx_burst(uint16_t hdr, const uint32_t *objs, uint8_t ndo, uint8_t *buf)
{
    size_t k = 0;
    buf[k++] = PD_TOK_SYNC1;
    buf[k++] = PD_TOK_SYNC1;
    buf[k++] = PD_TOK_SYNC1;
    buf[k++] = PD_TOK_SYNC2;
    buf[k++] = (uint8_t)(PD_TOK_PACKSYM | (2 + 4 * ndo));
    buf[k++] = (uint8_t)(hdr & 0xFF);
    buf[k++] = (uint8_t)(hdr >> 8);
    for (uint8_t i = 0; i < ndo; i++)
        for (int b = 0; b < 4; b++) buf[k++] = (uint8_t)(objs[i] >> (8 * b));
    buf[k++] = PD_TOK_JAM_CRC;
    buf[k++] = PD_TOK_EOP;
    buf[k++] = PD_TOK_TXOFF;
    buf[k++] = PD_TOK_TXON;
    return k;
}
