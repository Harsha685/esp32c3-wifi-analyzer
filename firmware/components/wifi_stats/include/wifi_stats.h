#pragma once
/*
 * Statistics engine: per-AP table, per-channel counters, aggregate packet counters.
 * Not thread-safe: it is owned and mutated by the analyzer task only.
 * All counters are cumulative; consumers compute deltas.
 */
#include "dot11.h"

#define STATS_MAX_APS      64
#define STATS_MAX_CHANNEL  13   /* 2.4 GHz channels 1..13 */

typedef struct {
    bool used;
    uint8_t bssid[6];
    uint8_t ssid_len;
    char ssid[DOT11_MAX_SSID + 1];
    uint8_t channel;
    dot11_security_t security;
    int8_t rssi_last, rssi_min, rssi_max;
    int16_t rssi_avg_q4;      /* EMA, value * 16 */
    uint32_t beacon_count;    /* beacons + probe responses */
    uint32_t frame_count;     /* every frame attributed to this BSSID */
    uint32_t last_seen_ms;
} stats_ap_t;

typedef struct {
    uint32_t frames, mgmt, ctrl, data, bytes;
    int32_t rssi_sum;
    uint32_t rssi_n;
} stats_chan_t;

typedef struct {
    uint32_t total, mgmt, ctrl, data, bytes;
    uint32_t beacons, probe_req, probe_resp, deauth, retry;
} stats_totals_t;

typedef struct {
    stats_ap_t aps[STATS_MAX_APS];
    stats_chan_t chan[STATS_MAX_CHANNEL + 1];  /* index 0 unused */
    stats_totals_t totals;
    uint32_t ap_evictions;
} stats_t;

void stats_init(stats_t *s);

/* Account one captured frame. channel is the radio channel it was received on. */
void stats_on_frame(stats_t *s, const dot11_hdr_t *h, uint8_t channel, int8_t rssi,
                    uint16_t frame_len, uint32_t now_ms);

/* Create/update an AP entry from a beacon or probe response. */
void stats_on_beacon(stats_t *s, const dot11_hdr_t *h, const dot11_beacon_info_t *b,
                     uint8_t rx_channel, int8_t rssi, uint32_t now_ms);

uint32_t stats_ap_count(const stats_t *s);
uint32_t stats_ap_count_on_channel(const stats_t *s, uint8_t channel);
