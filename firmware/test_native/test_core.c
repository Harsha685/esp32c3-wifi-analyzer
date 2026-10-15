/* Host-side tests for the hardware-independent components. Build: `make -C test_native` */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "dot11.h"
#include "spsc_ring.h"
#include "telemetry.h"
#include "wifi_stats.h"

static int checks;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)

/* Beacon from AA:BB:CC:00:11:22, SSID "TestNet", DS channel 6, RSN present. */
static const uint8_t beacon[] = {
    0x80, 0x00, 0x00, 0x00,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xaa, 0xbb, 0xcc, 0x00, 0x11, 0x22,
    0xaa, 0xbb, 0xcc, 0x00, 0x11, 0x22,
    0x30, 0x01,                                  /* seq 19, frag 0 */
    0, 0, 0, 0, 0, 0, 0, 0,                      /* timestamp */
    0x64, 0x00,                                  /* interval 100 TU */
    0x11, 0x04,                                  /* capability: ESS + privacy */
    0x00, 0x07, 'T', 'e', 's', 't', 'N', 'e', 't',
    0x03, 0x01, 0x06,                            /* DS param: ch 6 */
    0x30, 0x02, 0x01, 0x00,                      /* RSN */
};

static int test_crc(void)
{
    CHECK(tlm_crc16((const uint8_t *)"123456789", 9) == 0x29B1);  /* CRC-16/CCITT-FALSE check value */
    return 0;
}

static int test_encode(void)
{
    uint8_t out[TLM_MAX_FRAME];
    const uint8_t pl[3] = {1, 2, 3};
    size_t n = tlm_encode(TLM_SYSTEM_STATUS, 0x01020304, pl, 3, out);
    CHECK(n == 9 + 3 + 2);
    CHECK(out[0] == 0xAA && out[1] == 0x55 && out[2] == 0x04);
    CHECK(out[3] == 3 && out[4] == 0);
    CHECK(out[5] == 0x04 && out[8] == 0x01);
    uint16_t crc = tlm_crc16(out + 2, 7 + 3);
    CHECK(out[12] == (crc & 0xFF) && out[13] == (crc >> 8));
    CHECK(tlm_encode(1, 0, pl, TLM_MAX_PAYLOAD + 1, out) == 0);
    /* dump a golden frame for the Python decoder test */
    FILE *f = fopen("golden_frame.bin", "wb");
    if (f) { fwrite(out, 1, n, f); fclose(f); }
    return 0;
}

static int test_parse(void)
{
    dot11_hdr_t h;
    CHECK(dot11_parse_header(beacon, sizeof(beacon), &h));
    CHECK(h.type == DOT11_TYPE_MGMT && h.subtype == DOT11_MGMT_BEACON);
    CHECK(h.seq_num == 19 && h.has_bssid && h.bssid[0] == 0xaa && h.bssid[5] == 0x22);
    CHECK(!h.retry && !h.to_ds);

    dot11_beacon_info_t b;
    CHECK(dot11_parse_beacon(beacon, sizeof(beacon), sizeof(beacon), &b));
    CHECK(b.has_ssid && b.ssid_len == 7 && strcmp(b.ssid, "TestNet") == 0);
    CHECK(b.has_ds_channel && b.ds_channel == 6);
    CHECK(b.beacon_interval_tu == 100);
    CHECK(b.security == DOT11_SEC_WPA2);

    /* privacy bit but RSN IE cut off by truncation -> UNKNOWN, not WEP */
    CHECK(dot11_parse_beacon(beacon, 50, 300, &b) && b.security == DOT11_SEC_UNKNOWN);

    /* truncated captures must not read out of bounds or crash */
    for (size_t n = 0; n < sizeof(beacon); n++) {
        dot11_parse_header(beacon, n, &h);
        dot11_parse_beacon(beacon, n, sizeof(beacon), &b);
    }
    CHECK(!dot11_parse_header(beacon, 9, &h));

    /* ACK: 10 bytes, only addr1 */
    const uint8_t ack[10] = {0xd4, 0x00, 0, 0, 1, 2, 3, 4, 5, 6};
    CHECK(dot11_parse_header(ack, 10, &h));
    CHECK(h.type == DOT11_TYPE_CTRL && h.subtype == 13 && !h.has_addr2 && !h.has_bssid);

    /* data frame, ToDS=1: BSSID is addr1 */
    uint8_t data[28] = {0x08, 0x01};
    memcpy(data + 4, "\x11\x11\x11\x11\x11\x11", 6);
    memcpy(data + 10, "\x22\x22\x22\x22\x22\x22", 6);
    CHECK(dot11_parse_header(data, sizeof(data), &h));
    CHECK(h.type == DOT11_TYPE_DATA && h.to_ds && h.bssid[0] == 0x11);

    /* open network */
    uint8_t open[sizeof(beacon) - 4];
    memcpy(open, beacon, sizeof(open));
    open[34] = 0x01;
    CHECK(dot11_parse_beacon(open, sizeof(open), sizeof(open), &b) && b.security == DOT11_SEC_OPEN);
    return 0;
}

static int test_stats(void)
{
    static stats_t s;
    stats_init(&s);
    dot11_hdr_t h;
    dot11_beacon_info_t b;
    dot11_parse_header(beacon, sizeof(beacon), &h);
    dot11_parse_beacon(beacon, sizeof(beacon), sizeof(beacon), &b);

    /* beacon first so the AP exists when its own frame is counted */
    stats_on_beacon(&s, &h, &b, 6, -40, 1000);
    stats_on_frame(&s, &h, 6, -40, 100, 1000);
    stats_on_beacon(&s, &h, &b, 6, -50, 1100);
    stats_on_frame(&s, &h, 6, -50, 100, 1100);

    CHECK(s.totals.total == 2 && s.totals.beacons == 2 && s.totals.mgmt == 2);
    CHECK(s.chan[6].frames == 2 && s.chan[6].rssi_sum == -90 && s.chan[6].rssi_n == 2);
    CHECK(stats_ap_count(&s) == 1 && stats_ap_count_on_channel(&s, 6) == 1);
    CHECK(s.aps[0].beacon_count == 2 && s.aps[0].frame_count == 2);
    CHECK(s.aps[0].rssi_min == -50 && s.aps[0].rssi_max == -40 && s.aps[0].rssi_last == -50);
    CHECK(s.aps[0].channel == 6 && strcmp(s.aps[0].ssid, "TestNet") == 0);

    /* AP table eviction: fill with distinct BSSIDs, oldest gets replaced */
    for (int i = 0; i < STATS_MAX_APS + 5; i++) {
        dot11_hdr_t hh = h;
        hh.bssid[4] = (uint8_t)(i + 1);
        hh.bssid[3] = 0x77;
        stats_on_beacon(&s, &hh, &b, 6, -60, 2000 + i);
    }
    CHECK(stats_ap_count(&s) == STATS_MAX_APS);
    CHECK(s.ap_evictions > 0);
    return 0;
}

static int test_ring(void)
{
    spsc_ring_t r;
    uint32_t store[4 * 2];
    CHECK(spsc_ring_init(&r, store, sizeof(uint32_t) * 2, 3) != 0);  /* not a power of two */
    CHECK(spsc_ring_init(&r, store, sizeof(uint32_t) * 2, 4) == 0);
    CHECK(spsc_ring_peek(&r) == NULL);

    for (uint32_t i = 0; i < 4; i++) {
        uint32_t *p = spsc_ring_acquire(&r);
        CHECK(p != NULL);
        p[0] = i; p[1] = ~i;
        spsc_ring_commit(&r);
    }
    CHECK(spsc_ring_count(&r) == 4);
    CHECK(spsc_ring_acquire(&r) == NULL);   /* full */
    CHECK(atomic_load(&r.dropped) == 1);

    for (uint32_t i = 0; i < 4; i++) {      /* FIFO order, data intact */
        const uint32_t *p = spsc_ring_peek(&r);
        CHECK(p && p[0] == i && p[1] == ~i);
        spsc_ring_pop(&r);
    }
    CHECK(spsc_ring_peek(&r) == NULL && r.high_water == 4);

    /* wrap-around over many cycles */
    for (uint32_t i = 0; i < 1000; i++) {
        uint32_t *p = spsc_ring_acquire(&r);
        CHECK(p);
        p[0] = i;
        spsc_ring_commit(&r);
        const uint32_t *q = spsc_ring_peek(&r);
        CHECK(q && q[0] == i);
        spsc_ring_pop(&r);
    }
    return 0;
}

int main(void)
{
    int fail = test_crc() | test_encode() | test_parse() | test_stats() | test_ring();
    printf(fail ? "FAILED\n" : "all native tests passed (%d checks)\n", checks);
    return fail;
}
