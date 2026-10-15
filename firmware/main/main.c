/*
 * ESP32-C3 Wi-Fi analyzer
 *
 *   Wi-Fi driver task --(promiscuous cb)--> SPSC ring --> analyzer task --> USB serial
 *   hop task: cycles the radio through channels 1..13
 *
 * The promiscuous callback does the bare minimum (copy ~80 bytes into a ring slot) so it
 * never stalls the Wi-Fi driver.  The analyzer task owns all statistics, so they need no locks.
 */
#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#if !ANALYZER_TEXT_MODE
#include "driver/usb_serial_jtag.h"
#endif
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "analyzer_config.h"
#include "dot11.h"
#include "spsc_ring.h"
#include "telemetry.h"
#include "wifi_stats.h"

static const char *TAG = "analyzer";

/* ---------------------------------------------------------------- capture record */

typedef struct {
    uint64_t ts_us;
    uint16_t frame_len;     /* on-air length without FCS */
    uint8_t cap_len;        /* bytes valid in data[] */
    uint8_t pkt_type;       /* wifi_promiscuous_pkt_type_t */
    int8_t rssi;
    uint8_t channel;
    uint8_t pad[2];
    uint8_t data[ANALYZER_CAPTURE_BYTES];
} capture_rec_t;

static capture_rec_t s_ring_storage[ANALYZER_RING_SLOTS];
static spsc_ring_t s_ring;
static stats_t s_stats;

static _Atomic uint8_t s_channel = 1;
static volatile uint32_t s_dwell_ms[ANALYZER_MAX_CHANNEL + 1];  /* single writer: hop task */

/* ---------------------------------------------------------------- Wi-Fi capture */

static void sniffer_cb(void *buf, wifi_promiscuous_pkt_type_t type)
{
    if (type == WIFI_PKT_MISC) {
        return;
    }
    const wifi_promiscuous_pkt_t *pkt = buf;
    capture_rec_t *rec = spsc_ring_acquire(&s_ring);
    if (!rec) {
        return;  /* ring full: counted by the ring */
    }
    uint32_t sig_len = pkt->rx_ctrl.sig_len;
    uint32_t len = sig_len > 4 ? sig_len - 4 : 0;  /* strip FCS */
    uint32_t keep = type == WIFI_PKT_MGMT ? ANALYZER_CAPTURE_BYTES : 36;
    if (keep > len) keep = len;

    rec->ts_us = (uint64_t)esp_timer_get_time();
    rec->frame_len = (uint16_t)(len > 0xFFFF ? 0xFFFF : len);
    rec->cap_len = (uint8_t)keep;
    rec->pkt_type = (uint8_t)type;
    rec->rssi = pkt->rx_ctrl.rssi;
    rec->channel = pkt->rx_ctrl.channel;
    memcpy(rec->data, pkt->payload, keep);
    spsc_ring_commit(&s_ring);
}

static void wifi_capture_start(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    wifi_promiscuous_filter_t filt = {.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT |
                                                     WIFI_PROMIS_FILTER_MASK_CTRL |
                                                     WIFI_PROMIS_FILTER_MASK_DATA};
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_filter(&filt));
    wifi_promiscuous_filter_t ctrl_filt = {.filter_mask = WIFI_PROMIS_CTRL_FILTER_MASK_ALL};
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_ctrl_filter(&ctrl_filt));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(sniffer_cb));

    uint8_t start_ch = ANALYZER_FIXED_CHANNEL ? ANALYZER_FIXED_CHANNEL : 1;
    ESP_ERROR_CHECK(esp_wifi_set_channel(start_ch, WIFI_SECOND_CHAN_NONE));
    atomic_store(&s_channel, start_ch);
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));
}

static void hop_task(void *arg)
{
    (void)arg;
    uint8_t ch = 1;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE));
        atomic_store(&s_channel, ch);
        vTaskDelayUntil(&last, pdMS_TO_TICKS(ANALYZER_DWELL_MS));
        s_dwell_ms[ch] += ANALYZER_DWELL_MS;
        ch = ch >= ANALYZER_MAX_CHANNEL ? 1 : ch + 1;
    }
}

/* ---------------------------------------------------------------- USB telemetry transport */

static uint32_t s_tx_dropped;
#if !ANALYZER_TEXT_MODE
static uint32_t s_tx_seq;
static int64_t s_tx_blocked_until_us;   /* back off while nobody is reading the port */
#endif

static bool host_connected(void)
{
#if ANALYZER_TEXT_MODE
    return false;
#else
    return usb_serial_jtag_is_connected();
#endif
}

static void tlm_send(uint8_t type, const void *payload, size_t len)
{
#if !ANALYZER_TEXT_MODE
    int64_t now = esp_timer_get_time();
    if (!host_connected() || now < s_tx_blocked_until_us) {
        return;  /* not attempted: sequence number not consumed */
    }
    uint8_t frame[TLM_MAX_FRAME];
    size_t n = tlm_encode(type, s_tx_seq++, payload, len, frame);
    if (n == 0) {
        s_tx_dropped++;
        return;
    }
    int written = usb_serial_jtag_write_bytes(frame, n, pdMS_TO_TICKS(20));
    if (written != (int)n) {
        s_tx_dropped++;                                 /* host sees a truncated frame + seq gap */
        s_tx_blocked_until_us = now + 1000 * 1000;      /* retry in 1 s */
    }
#else
    (void)type; (void)payload; (void)len;
#endif
}

/* ---------------------------------------------------------------- reporting */

static void send_reports(uint32_t now_ms, uint32_t pps, bool send_aps)
{
    const stats_totals_t *t = &s_stats.totals;

    tlm_packet_stats_t ps = {
        .ts_ms = now_ms, .total = t->total, .mgmt = t->mgmt, .ctrl = t->ctrl, .data = t->data,
        .beacons = t->beacons, .probe_req = t->probe_req, .probe_resp = t->probe_resp,
        .deauth = t->deauth, .retry = t->retry, .pps = pps,
    };
    tlm_send(TLM_PACKET_STATS, &ps, sizeof(ps));

    tlm_system_status_t sys = {
        .ts_ms = now_ms,
        .free_heap = esp_get_free_heap_size(),
        .min_free_heap = esp_get_minimum_free_heap_size(),
        .ring_capacity = ANALYZER_RING_SLOTS,
        .ring_high_water = (uint16_t)s_ring.high_water,
        .ring_dropped = atomic_load(&s_ring.dropped),
        .tx_dropped = s_tx_dropped,
        .current_channel = atomic_load(&s_channel),
        .host_connected = host_connected(),
        .hop_dwell_ms = ANALYZER_FIXED_CHANNEL ? 0 : ANALYZER_DWELL_MS,
    };
    tlm_send(TLM_SYSTEM_STATUS, &sys, sizeof(sys));

    for (uint8_t ch = 1; ch <= ANALYZER_MAX_CHANNEL; ch++) {
        const stats_chan_t *c = &s_stats.chan[ch];
        tlm_channel_stats_t cs = {
            .ts_ms = now_ms, .channel = ch,
            .ap_count = (uint8_t)stats_ap_count_on_channel(&s_stats, ch),
            .dwell_ms = s_dwell_ms[ch],
            .frames = c->frames, .mgmt = c->mgmt, .ctrl = c->ctrl, .data = c->data,
            .bytes = c->bytes, .rssi_sum = c->rssi_sum, .rssi_n = c->rssi_n,
        };
        tlm_send(TLM_CHANNEL_STATS, &cs, sizeof(cs));
    }

    if (send_aps) {
        for (int i = 0; i < STATS_MAX_APS; i++) {
            const stats_ap_t *ap = &s_stats.aps[i];
            if (!ap->used) continue;
            tlm_ap_info_t info = {
                .channel = ap->channel, .rssi_last = ap->rssi_last,
                .rssi_avg = (int8_t)(ap->rssi_avg_q4 / 16), .rssi_min = ap->rssi_min,
                .rssi_max = ap->rssi_max, .security = ap->security,
                .beacon_count = ap->beacon_count, .frame_count = ap->frame_count,
                .last_seen_ms = ap->last_seen_ms, .ssid_len = ap->ssid_len,
            };
            memcpy(info.bssid, ap->bssid, 6);
            memcpy(info.ssid, ap->ssid, ap->ssid_len);
            tlm_send(TLM_AP_INFO, &info, sizeof(info));
        }
    }
}

#if ANALYZER_TEXT_MODE
static void print_text_report(uint32_t now_ms, uint32_t pps)
{
    const stats_totals_t *t = &s_stats.totals;
    printf("\n== t=%" PRIu32 "s ch=%u  %" PRIu32 " pps | total %" PRIu32 " (mgmt %" PRIu32 " ctrl %" PRIu32
           " data %" PRIu32 ") beacons %" PRIu32 " | ring hw %" PRIu32 "/%d drop %" PRIu32 " | heap %" PRIu32 "\n",
           now_ms / 1000, atomic_load(&s_channel), pps, t->total, t->mgmt, t->ctrl, t->data, t->beacons,
           s_ring.high_water, ANALYZER_RING_SLOTS, atomic_load(&s_ring.dropped), esp_get_free_heap_size());
    printf("%-18s %-3s %5s %5s %-6s %7s  %s\n", "BSSID", "ch", "rssi", "avg", "sec", "beacons", "SSID");
    /* strongest 12 by last RSSI */
    bool shown[STATS_MAX_APS] = {0};
    for (int n = 0; n < 12; n++) {
        int best = -1;
        for (int i = 0; i < STATS_MAX_APS; i++) {
            if (s_stats.aps[i].used && !shown[i] &&
                (best < 0 || s_stats.aps[i].rssi_last > s_stats.aps[best].rssi_last)) {
                best = i;
            }
        }
        if (best < 0) break;
        shown[best] = true;
        const stats_ap_t *a = &s_stats.aps[best];
        printf("%02x:%02x:%02x:%02x:%02x:%02x  %-3u %5d %5d %-6s %7" PRIu32 "  %s\n", a->bssid[0], a->bssid[1],
               a->bssid[2], a->bssid[3], a->bssid[4], a->bssid[5], a->channel, a->rssi_last,
               a->rssi_avg_q4 / 16, dot11_security_name(a->security), a->beacon_count,
               a->ssid_len ? a->ssid : "<hidden>");
    }
}
#endif

/* ---------------------------------------------------------------- analyzer task */

static void process_record(const capture_rec_t *rec, uint32_t now_ms)
{
    dot11_hdr_t h;
    if (!dot11_parse_header(rec->data, rec->cap_len, &h)) {
        return;
    }
    /* beacon first, so the AP exists when its own frame is attributed to it */
    dot11_beacon_info_t b;
    if (h.type == DOT11_TYPE_MGMT &&
        (h.subtype == DOT11_MGMT_BEACON || h.subtype == DOT11_MGMT_PROBE_RESP) &&
        dot11_parse_beacon(rec->data, rec->cap_len, rec->frame_len, &b)) {
        stats_on_beacon(&s_stats, &h, &b, rec->channel, rec->rssi, now_ms);
    }
    stats_on_frame(&s_stats, &h, rec->channel, rec->rssi, rec->frame_len, now_ms);
}

static void analyzer_task(void *arg)
{
    (void)arg;
    int64_t next_report_us = esp_timer_get_time() + ANALYZER_REPORT_MS * 1000LL;
    uint32_t last_total = 0, report_count = 0;

    for (;;) {
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        const capture_rec_t *rec;
        int budget = 256;  /* bound the time spent before checking the report timer */
        while (budget-- && (rec = spsc_ring_peek(&s_ring)) != NULL) {
            process_record(rec, now_ms);
            spsc_ring_pop(&s_ring);
        }

        if (esp_timer_get_time() >= next_report_us) {
            next_report_us += ANALYZER_REPORT_MS * 1000LL;
            uint32_t total = s_stats.totals.total;
            uint32_t pps = total - last_total;
            last_total = total;
#if ANALYZER_TEXT_MODE
            print_text_report(now_ms, pps);
#endif
            send_reports(now_ms, pps, (report_count++ % ANALYZER_AP_REPORT_EVERY) == 0);
        }
        if (!spsc_ring_count(&s_ring)) {
            vTaskDelay(1);
        }
    }
}

/* ---------------------------------------------------------------- entry point */

void app_main(void)
{
    stats_init(&s_stats);
    ESP_ERROR_CHECK(spsc_ring_init(&s_ring, s_ring_storage, sizeof(capture_rec_t), ANALYZER_RING_SLOTS));

#if !ANALYZER_TEXT_MODE
    usb_serial_jtag_driver_config_t usb_cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    usb_cfg.tx_buffer_size = 2048;
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb_cfg));
#endif

    wifi_capture_start();
    ESP_LOGI(TAG, "capture started (%s)", ANALYZER_FIXED_CHANNEL ? "fixed channel" : "channel hopping");

#if !ANALYZER_TEXT_MODE
    esp_log_level_set("*", ESP_LOG_NONE);  /* keep the port clean for binary frames */
#endif

    xTaskCreate(analyzer_task, "analyzer", 4096, NULL, 5, NULL);
    if (!ANALYZER_FIXED_CHANNEL) {
        xTaskCreate(hop_task, "hop", 3072, NULL, 6, NULL);
    }
}
