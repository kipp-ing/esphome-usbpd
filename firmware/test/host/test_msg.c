/* T38 codec_vectors (docs/spec.md §6.5): pure L2 unit test.
 * V1-V8 byte-exact, D1 decode, selection vectors, classification, names. */
#include <stdio.h>
#include <string.h>
#include "pd_msg.h"
#include "log.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { printf("  FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void expect_burst(const char *name, uint16_t hdr, const uint32_t *objs, uint8_t ndo,
                         const uint8_t *want, size_t wn)
{
    uint8_t b[PD_TX_BURST_MAX];
    size_t n = pd_tx_burst(hdr, objs, ndo, b);
    checks++;
    if (n != wn || memcmp(b, want, wn)) {
        printf("  FAIL %s burst:", name);
        for (size_t i = 0; i < n; i++) printf(" %02X", b[i]);
        printf("\n");
        fails++;
    }
}

/* D1 frame */
static const uint8_t D1[] = { 0xE0, 0xA1, 0x41, 0x2C, 0x91, 0x01, 0x08, 0x2C, 0xD1, 0x02, 0x00,
                              0x2C, 0xB1, 0x04, 0x00, 0xF4, 0x41, 0x06, 0x00, 0, 0, 0, 0 };

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static void test_encode(void)
{
    uint32_t caps[4] = { 0x0801912C, 0x0002D12C, 0x0004B12C, 0x000641F4 };
    /* V1: 9 V request, id 0 */
    pd_sel_t s = pd_select(caps, 4, 9000, false);
    uint32_t rdo = pd_rdo_fixed(s.pos, s.ma);
    CHECK(rdo == 0x2004B12C);
    CHECK(pd_hdr_tx(PD_DATA_REQUEST, 0, 1) == 0x1042);
    static const uint8_t v1[] = { 0x12,0x12,0x12,0x13,0x86,0x42,0x10,0x2C,0xB1,0x04,0x20,0xFF,0x14,0xFE,0xA1 };
    expect_burst("V1", pd_hdr_tx(PD_DATA_REQUEST, 0, 1), &rdo, 1, v1, sizeof v1);
    /* V2: 5 V request pos 1 id 0 */
    rdo = pd_rdo_fixed(1, 3000);
    CHECK(rdo == 0x1004B12C);
    static const uint8_t v2[] = { 0x12,0x12,0x12,0x13,0x86,0x42,0x10,0x2C,0xB1,0x04,0x10,0xFF,0x14,0xFE,0xA1 };
    expect_burst("V2", pd_hdr_tx(PD_DATA_REQUEST, 0, 1), &rdo, 1, v2, sizeof v2);
    /* V3: 9 V request id 1 */
    rdo = 0x2004B12C;
    CHECK(pd_hdr_tx(PD_DATA_REQUEST, 1, 1) == 0x1242);
    static const uint8_t v3[] = { 0x12,0x12,0x12,0x13,0x86,0x42,0x12,0x2C,0xB1,0x04,0x20,0xFF,0x14,0xFE,0xA1 };
    expect_burst("V3", pd_hdr_tx(PD_DATA_REQUEST, 1, 1), &rdo, 1, v3, sizeof v3);
    /* V4: Soft_Reset id 0 */
    CHECK(pd_hdr_tx(PD_CTRL_SOFT_RESET, 0, 0) == 0x004D);
    static const uint8_t v4[] = { 0x12,0x12,0x12,0x13,0x82,0x4D,0x00,0xFF,0x14,0xFE,0xA1 };
    expect_burst("V4", 0x004D, NULL, 0, v4, sizeof v4);
    /* V5: Accept id 0 */
    CHECK(pd_hdr_tx(PD_CTRL_ACCEPT, 0, 0) == 0x0043);
    static const uint8_t v5[] = { 0x12,0x12,0x12,0x13,0x82,0x43,0x00,0xFF,0x14,0xFE,0xA1 };
    expect_burst("V5", 0x0043, NULL, 0, v5, sizeof v5);
    /* V6: Reject id 2 */
    CHECK(pd_hdr_tx(PD_CTRL_REJECT, 2, 0) == 0x0444);
    static const uint8_t v6[] = { 0x12,0x12,0x12,0x13,0x82,0x44,0x04,0xFF,0x14,0xFE,0xA1 };
    expect_burst("V6", 0x0444, NULL, 0, v6, sizeof v6);
    /* V7: Sink_Caps want 5000 id 0 */
    uint32_t pdos[2];
    uint8_t n = pd_sink_caps(5000, pdos);
    CHECK(n == 1 && pdos[0] == 0x0001912C);
    CHECK(pd_hdr_tx(PD_DATA_SINK_CAP, 0, n) == 0x1044);
    static const uint8_t v7[] = { 0x12,0x12,0x12,0x13,0x86,0x44,0x10,0x2C,0x91,0x01,0x00,0xFF,0x14,0xFE,0xA1 };
    expect_burst("V7", pd_hdr_tx(PD_DATA_SINK_CAP, 0, n), pdos, n, v7, sizeof v7);
    /* V8: Sink_Caps want 9000 id 0 */
    n = pd_sink_caps(9000, pdos);
    CHECK(n == 2 && pdos[0] == 0x1001912C && pdos[1] == 0x0002D12C);
    CHECK(pd_hdr_tx(PD_DATA_SINK_CAP, 0, n) == 0x2044);
    static const uint8_t v8[] = { 0x12,0x12,0x12,0x13,0x8A,0x44,0x20,0x2C,0x91,0x01,0x10,0x2C,0xD1,0x02,0x00,
                                  0xFF,0x14,0xFE,0xA1 };
    expect_burst("V8", pd_hdr_tx(PD_DATA_SINK_CAP, 0, n), pdos, n, v8, sizeof v8);
    /* rounding down to 50 mV */
    n = pd_sink_caps(9049, pdos);
    CHECK(n == 2 && pdos[1] == 0x0002D12C);
}

static void test_decode(void)
{
    CHECK(D1[0] == PD_RXTOK_SOP);
    uint16_t hdr = (uint16_t)(D1[1] | D1[2] << 8);
    CHECK(hdr == 0x41A1);
    CHECK(PD_HDR_NDO(hdr) == 4 && PD_HDR_REV(hdr) == 2 && PD_HDR_ID(hdr) == 0 && PD_HDR_TYPE(hdr) == 1);
    uint32_t caps[4];
    for (int i = 0; i < 4; i++) caps[i] = le32(&D1[3 + 4 * i]);
    CHECK(caps[0] == 0x0801912C && caps[1] == 0x0002D12C && caps[2] == 0x0004B12C && caps[3] == 0x000641F4);
    pd_rx_class_t c = pd_classify(hdr, caps[0]);
    CHECK(c.cls == PD_CLS_OK && c.kind == PD_RXK_SRC_CAPS && !c.caps_bad);
    static const uint32_t mv[4] = { 5000, 9000, 15000, 20000 }, ma[4] = { 3000, 3000, 3000, 5000 };
    for (int i = 0; i < 4; i++) {
        pd_pdo_t d = pd_pdo_decode(caps[i]);
        CHECK(d.kind == 'F' && d.mv == mv[i] && d.ma == ma[i]);
        CHECK(d.unconstrained == (i == 0));
    }
    /* other vectors */
    CHECK(pd_pdo_decode(0x0003C12C).mv == 12000);
    pd_pdo_t a = pd_pdo_decode(0xC1A42164);
    CHECK(a.kind == 'A' && a.mv == 21000 && a.ma == 5000);
    CHECK(pd_pdo_decode(0x40000000).kind == 'B' && pd_pdo_decode(0x80000000).kind == 'V');
    pd_pdo_t b = pd_pdo_decode(0x590000F0);               /* battery 20 V max, 60 W */
    CHECK(b.kind == 'B' && b.mv == 20000 && b.ma == 3000);
    pd_pdo_t v = pd_pdo_decode(0x92C0012C);               /* variable 15 V max, 3 A */
    CHECK(v.kind == 'V' && v.mv == 15000 && v.ma == 3000);
    CHECK(PD_HDR_REV(0x2161) == 1 && PD_HDR_NDO(0x2161) == 2);
}

static void test_select(void)
{
    const uint32_t caps[5] = { 0x0801912C, 0x0002D12C, 0x0004B12C, 0x000641F4, 0xC1A42164 };
    struct { uint32_t want; bool fb; uint8_t pos; uint32_t ma; pd_why_t why; } v[] = {
        { 5000, false, 1, 3000, PD_WHY_EXACT },
        { 9000, false, 2, 3000, PD_WHY_EXACT },
        { 12000, false, 2, 3000, PD_WHY_BELOW },
        { 20000, false, 4, 3000, PD_WHY_EXACT },
        { 3000, false, 1, 3000, PD_WHY_MIN },
        { 9000, true, 1, 3000, PD_WHY_FALLBACK },
    };
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        pd_sel_t s = pd_select(caps, 4, v[i].want, v[i].fb);
        checks++;
        if (s.pos != v[i].pos || s.ma != v[i].ma || s.why != v[i].why) {
            printf("  FAIL select want %u fb %d: pos %u ma %u why %s\n", v[i].want, v[i].fb, s.pos, s.ma,
                   pd_why_name(s.why));
            fails++;
        }
    }
    CHECK(pd_rdo_fixed(4, pd_select(caps, 4, 20000, false).ma) == 0x4004B12C);
    pd_sel_t s = pd_select(caps, 5, 21000, false);       /* APDO ignored */
    CHECK(s.pos == 4 && s.why == PD_WHY_BELOW && s.mv == 20000 && s.ma == 3000);
    CHECK(!strcmp(pd_why_name(PD_WHY_BELOW), "below"));
}

static void test_classify(void)
{
    struct { uint16_t hdr; uint32_t obj0; pd_cls_t cls; pd_rxk_t kind; const char *name; } v[] = {
        { 0x01A1, 0, PD_CLS_OK, PD_RXK_GOODCRC, "GoodCRC" },
        { 0x03A3, 0, PD_CLS_OK, PD_RXK_ACCEPT, "Accept" },
        { 0x05A6, 0, PD_CLS_OK, PD_RXK_PS_RDY, "PS_RDY" },
        { 0x01AD, 0, PD_CLS_OK, PD_RXK_SOFT_RESET, "Soft_Reset" },
        { 0x07A8, 0, PD_CLS_OK, PD_RXK_GET_SINK_CAP, "Get_Sink_Cap" },
        { 0x07A5, 0, PD_CLS_OK, PD_RXK_PING, "Ping" },
        { 0x07A9, 0, PD_CLS_UNSUPPORTED, PD_RXK_NONE, "DR_Swap" },
        { 0x01A7, 0, PD_CLS_UNSUPPORTED, PD_RXK_NONE, "Get_Source_Cap" },
        { 0x01AE, 0, PD_CLS_UNSUPPORTED, PD_RXK_NONE, "Unknown_C14" },
        { 0x01A0, 0, PD_CLS_UNSUPPORTED, PD_RXK_NONE, "Unknown_C0" },
        { 0x01A2, 0, PD_CLS_OK, PD_RXK_GOTOMIN, "GotoMin" },
        { 0x01A4, 0, PD_CLS_OK, PD_RXK_REJECT, "Reject" },
        { 0x01AC, 0, PD_CLS_OK, PD_RXK_WAIT, "Wait" },
        { 0x41A1, 0x0801912C, PD_CLS_OK, PD_RXK_SRC_CAPS, "Source_Capabilities" },
        { 0x41A1, 0xC1A42164, PD_CLS_UNEXPECTED, PD_RXK_NONE, "Source_Capabilities" },
        { 0x11A2, 0, PD_CLS_UNEXPECTED, PD_RXK_NONE, "Request" },
        { 0x11A4, 0, PD_CLS_UNEXPECTED, PD_RXK_NONE, "Sink_Capabilities" },
        { 0x11A3, 0, PD_CLS_IGNORED, PD_RXK_NONE, "BIST" },
        { 0x11AF, 0, PD_CLS_IGNORED, PD_RXK_NONE, "Vendor_Defined" },
        { 0x11A7, 0, PD_CLS_UNSUPPORTED, PD_RXK_NONE, "Unknown_D7" },
        { 0x91A1, 0, PD_CLS_UNSUPPORTED, PD_RXK_NONE, "Unknown_E1" },
    };
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        pd_rx_class_t c = pd_classify(v[i].hdr, v[i].obj0);
        char buf[16];
        const char *nm = pd_msg_name(v[i].hdr, buf, sizeof buf);
        checks++;
        if (c.cls != v[i].cls || (c.cls == PD_CLS_OK && c.kind != v[i].kind) || strcmp(nm, v[i].name)) {
            printf("  FAIL classify 0x%04X: cls %s kind %d name %s\n", v[i].hdr, pd_cls_name(c.cls), c.kind, nm);
            fails++;
        }
    }
    CHECK(pd_classify(0x41A1, 0xC1A42164).caps_bad);
    CHECK(!strcmp(pd_tx_name(PD_DATA_REQUEST, 1), "Request"));
    CHECK(!strcmp(pd_tx_name(PD_CTRL_SOFT_RESET, 0), "Soft_Reset"));
    CHECK(!strcmp(pd_tx_name(PD_DATA_SINK_CAP, 2), "Sink_Capabilities"));
}

/* log pool: nested lines up to LOG_POOL_N, overflow dropped + counted, slots released */
static int s_sink_n;
static void count_sink(const char *line, size_t len) { (void)line; (void)len; s_sink_n++; }
static void test_log_pool(void)
{
    log_set_sink(count_sink);
    uint32_t miss0 = log_pool_miss();
    log_line_t l[LOG_POOL_N + 1];
    for (int k = 0; k < LOG_POOL_N + 1; k++) { log_begin(&l[k], "nest"); log_uint(&l[k], "k", (uint32_t)k); }
    CHECK(log_pool_miss() == miss0 + 1);
    CHECK(l[LOG_POOL_N].buf == NULL);
    for (int k = LOG_POOL_N; k >= 0; k--) log_end(&l[k]);
    CHECK(s_sink_n == LOG_POOL_N);
    for (int r = 0; r < 3 * LOG_POOL_N; r++) { log_line_t x; log_begin(&x, "again"); log_end(&x); }
    CHECK(s_sink_n == 4 * LOG_POOL_N);
    CHECK(log_pool_miss() == miss0 + 1);
    CHECK(log_pool_busy() == 0);
    CHECK(sizeof(log_line_t) <= 32);   /* the handle stays off-stack-cheap */
    log_set_sink(NULL);
}

int main(void)
{
    test_log_pool();
    test_encode();
    test_decode();
    test_select();
    test_classify();
    printf("  %s T38 codec_vectors (%d checks)\n", fails ? "FAIL" : "ok  ", checks);
    return fails ? 1 : 0;
}
