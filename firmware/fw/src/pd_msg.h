/* L2 protocol codec (docs/spec.md §6). Pure functions: no I/O, no SM, no log.
 * Shared by firmware and host sim. */
#ifndef USBPD_PD_MSG_H
#define USBPD_PD_MSG_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ---- header (§6.1) ------------------------------------------------------- */
#define PD_HDR_TYPE(h)   ((uint8_t)((h) & 0x1F))
#define PD_HDR_REV(h)    ((uint8_t)(((h) >> 6) & 3))
#define PD_HDR_PROLE(h)  ((uint8_t)(((h) >> 8) & 1))
#define PD_HDR_ID(h)     ((uint8_t)(((h) >> 9) & 7))
#define PD_HDR_NDO(h)    ((uint8_t)(((h) >> 12) & 7))
#define PD_HDR_EXT(h)    ((uint8_t)(((h) >> 15) & 1))

/* control message types (NDO 0) */
enum {
    PD_CTRL_GOODCRC = 1, PD_CTRL_GOTOMIN = 2, PD_CTRL_ACCEPT = 3, PD_CTRL_REJECT = 4,
    PD_CTRL_PING = 5, PD_CTRL_PS_RDY = 6, PD_CTRL_GET_SOURCE_CAP = 7, PD_CTRL_GET_SINK_CAP = 8,
    PD_CTRL_DR_SWAP = 9, PD_CTRL_PR_SWAP = 10, PD_CTRL_VCONN_SWAP = 11, PD_CTRL_WAIT = 12,
    PD_CTRL_SOFT_RESET = 13,
};
/* data message types (NDO >= 1) */
enum {
    PD_DATA_SOURCE_CAP = 1, PD_DATA_REQUEST = 2, PD_DATA_BIST = 3, PD_DATA_SINK_CAP = 4,
    PD_DATA_VDM = 15,
};

/* TX header: 0x0040 | type | id<<9 | ndo<<12  (SpecRev 01, UFP, Sink) */
uint16_t pd_hdr_tx(uint8_t type, uint8_t id, uint8_t ndo);

/* ---- RX classification (§6.2) -------------------------------------------- */
typedef enum {
    PD_RXK_NONE = 0,       /* ignored / unexpected / unsupported: see cls */
    PD_RXK_GOODCRC,
    PD_RXK_SRC_CAPS,
    PD_RXK_ACCEPT,
    PD_RXK_REJECT,
    PD_RXK_WAIT,
    PD_RXK_PS_RDY,
    PD_RXK_SOFT_RESET,
    PD_RXK_GET_SINK_CAP,
    PD_RXK_PING,
    PD_RXK_GOTOMIN,
} pd_rxk_t;

typedef enum {
    PD_CLS_OK = 0,
    PD_CLS_UNEXPECTED,
    PD_CLS_UNSUPPORTED,
    PD_CLS_IGNORED,
} pd_cls_t;

typedef struct {
    pd_rxk_t kind;     /* meaningful when cls == PD_CLS_OK */
    pd_cls_t cls;
    bool caps_bad;     /* Source_Capabilities with PDO1 not Fixed (-> UNEXPECTED) */
} pd_rx_class_t;

/* obj0 is only looked at for Source_Capabilities (ndo >= 1). */
pd_rx_class_t pd_classify(uint16_t hdr, uint32_t obj0);
const char *pd_cls_name(pd_cls_t c);   /* "ok" "unexpected" "unsupported" "ignored" */

/* Message type name, e.g. "Source_Capabilities", "Accept", "Unknown_C14",
 * "Unknown_D7", "Unknown_E1" (extended). buf >= 16 bytes; returns buf or a literal. */
const char *pd_msg_name(uint16_t hdr, char *buf, size_t n);
/* Name of a message we transmit (control if ndo == 0). */
const char *pd_tx_name(uint8_t type, uint8_t ndo);

/* ---- PDOs (§6.2 "Source PDO decode") ------------------------------------- */
typedef struct {
    char kind;          /* 'F' fixed, 'B' battery, 'V' variable, 'A' augmented */
    uint32_t mv;        /* fixed: voltage; others: max voltage */
    uint32_t ma;        /* fixed/variable/APDO: (max) current; battery: max power / max voltage */
    bool unconstrained; /* fixed only, bit 27 */
} pd_pdo_t;

pd_pdo_t pd_pdo_decode(uint32_t pdo);
static inline bool pd_pdo_is_fixed(uint32_t pdo) { return (pdo >> 30) == 0; }

/* ---- selection (§4.2 select()) ------------------------------------------- */
typedef enum { PD_WHY_EXACT = 0, PD_WHY_BELOW, PD_WHY_MIN, PD_WHY_FALLBACK } pd_why_t;
typedef struct {
    uint8_t pos;        /* 1-based */
    uint32_t mv, ma;    /* ma already capped at 3000 */
    pd_why_t why;
} pd_sel_t;
#define PD_SEL_MA_MAX 3000
pd_sel_t pd_select(const uint32_t *caps, uint8_t n, uint32_t want_mv, bool reject_fallback);
const char *pd_why_name(pd_why_t w);

/* ---- objects we build (§6.3) --------------------------------------------- */
uint32_t pd_rdo_fixed(uint8_t pos, uint32_t ma);
/* Sink_Capabilities PDOs for want_mv; returns count (1 or 2). pdos[2]. */
uint8_t pd_sink_caps(uint32_t want_mv, uint32_t *pdos);

/* ---- TX FIFO burst (§6.4) ------------------------------------------------ */
#define PD_TX_BURST_MAX (5 + 2 + 7 * 4 + 4)
/* Writes the whole burst into buf (>= PD_TX_BURST_MAX); returns its length. */
size_t pd_tx_burst(uint16_t hdr, const uint32_t *objs, uint8_t ndo, uint8_t *buf);

/* FIFO token bytes */
#define PD_TOK_SYNC1    0x12
#define PD_TOK_SYNC2    0x13
#define PD_TOK_PACKSYM  0x80
#define PD_TOK_JAM_CRC  0xFF
#define PD_TOK_EOP      0x14
#define PD_TOK_TXOFF    0xFE
#define PD_TOK_TXON     0xA1
#define PD_RXTOK_MASK   0xE0
#define PD_RXTOK_SOP    0xE0

#endif
