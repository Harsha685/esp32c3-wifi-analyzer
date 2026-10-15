#include "wifi_stats.h"

#include <string.h>

void stats_init(stats_t *s)
{
    memset(s, 0, sizeof(*s));
}

static stats_ap_t *find_ap(stats_t *s, const uint8_t *bssid)
{
    for (int i = 0; i < STATS_MAX_APS; i++) {
        if (s->aps[i].used && memcmp(s->aps[i].bssid, bssid, 6) == 0) {
            return &s->aps[i];
        }
    }
    return NULL;
}

/* Free slot, or evict the least-recently-seen AP. */
static stats_ap_t *alloc_ap(stats_t *s)
{
    stats_ap_t *oldest = &s->aps[0];
    for (int i = 0; i < STATS_MAX_APS; i++) {
        if (!s->aps[i].used) {
            return &s->aps[i];
        }
        if ((int32_t)(s->aps[i].last_seen_ms - oldest->last_seen_ms) < 0) {
            oldest = &s->aps[i];
        }
    }
    s->ap_evictions++;
    return oldest;
}

void stats_on_frame(stats_t *s, const dot11_hdr_t *h, uint8_t channel, int8_t rssi,
                    uint16_t frame_len, uint32_t now_ms)
{
    (void)now_ms;
    stats_totals_t *t = &s->totals;
    t->total++;
    t->bytes += frame_len;
    if (h->retry) {
        t->retry++;
    }

    stats_chan_t *c = (channel >= 1 && channel <= STATS_MAX_CHANNEL) ? &s->chan[channel] : NULL;
    if (c) {
        c->frames++;
        c->bytes += frame_len;
        c->rssi_sum += rssi;
        c->rssi_n++;
    }

    switch (h->type) {
    case DOT11_TYPE_MGMT:
        t->mgmt++;
        if (c) c->mgmt++;
        switch (h->subtype) {
        case DOT11_MGMT_BEACON:     t->beacons++; break;
        case DOT11_MGMT_PROBE_REQ:  t->probe_req++; break;
        case DOT11_MGMT_PROBE_RESP: t->probe_resp++; break;
        case DOT11_MGMT_DEAUTH:
        case DOT11_MGMT_DISASSOC:   t->deauth++; break;
        default: break;
        }
        break;
    case DOT11_TYPE_CTRL:
        t->ctrl++;
        if (c) c->ctrl++;
        break;
    case DOT11_TYPE_DATA:
        t->data++;
        if (c) c->data++;
        break;
    default:
        break;
    }

    if (h->has_bssid) {
        stats_ap_t *ap = find_ap(s, h->bssid);
        if (ap) {
            ap->frame_count++;
        }
    }
}

void stats_on_beacon(stats_t *s, const dot11_hdr_t *h, const dot11_beacon_info_t *b,
                     uint8_t rx_channel, int8_t rssi, uint32_t now_ms)
{
    stats_ap_t *ap = find_ap(s, h->bssid);
    if (!ap) {
        ap = alloc_ap(s);
        memset(ap, 0, sizeof(*ap));
        ap->used = true;
        memcpy(ap->bssid, h->bssid, 6);
        ap->rssi_min = ap->rssi_max = rssi;
        ap->rssi_avg_q4 = (int16_t)(rssi * 16);
    }
    /* The DS Parameter Set IE is the AP's real channel; rx channel can be adjacent leakage. */
    ap->channel = b->has_ds_channel ? b->ds_channel : rx_channel;
    ap->security = b->security;
    if (b->has_ssid) {
        ap->ssid_len = b->ssid_len;
        memcpy(ap->ssid, b->ssid, sizeof(ap->ssid));
    }
    ap->rssi_last = rssi;
    if (rssi < ap->rssi_min) ap->rssi_min = rssi;
    if (rssi > ap->rssi_max) ap->rssi_max = rssi;
    ap->rssi_avg_q4 = (int16_t)(ap->rssi_avg_q4 + ((rssi * 16 - ap->rssi_avg_q4) / 8));
    ap->beacon_count++;
    ap->last_seen_ms = now_ms;
}

uint32_t stats_ap_count(const stats_t *s)
{
    uint32_t n = 0;
    for (int i = 0; i < STATS_MAX_APS; i++) {
        n += s->aps[i].used;
    }
    return n;
}

uint32_t stats_ap_count_on_channel(const stats_t *s, uint8_t channel)
{
    uint32_t n = 0;
    for (int i = 0; i < STATS_MAX_APS; i++) {
        n += s->aps[i].used && s->aps[i].channel == channel;
    }
    return n;
}
