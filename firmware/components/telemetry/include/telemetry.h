#pragma once
/*
 * Binary telemetry protocol (see protocol/protocol.md).  All multi-byte fields little-endian.
 *
 *   | MAGIC 2 | TYPE 1 | LEN 2 | SEQ 4 | PAYLOAD LEN | CRC16 2 |
 *   CRC-16/CCITT-FALSE over TYPE..PAYLOAD.
 */
#include <stddef.h>
#include <stdint.h>

#define TLM_MAGIC0 0xAA
#define TLM_MAGIC1 0x55
#define TLM_HEADER_SIZE 9
#define TLM_CRC_SIZE 2
#define TLM_MAX_PAYLOAD 128
#define TLM_MAX_FRAME (TLM_HEADER_SIZE + TLM_MAX_PAYLOAD + TLM_CRC_SIZE)

enum {
    TLM_AP_INFO = 0x01,
    TLM_CHANNEL_STATS = 0x02,
    TLM_PACKET_STATS = 0x03,
    TLM_SYSTEM_STATUS = 0x04,
};

#define TLM_PACKED __attribute__((packed))

typedef struct TLM_PACKED {
    uint8_t bssid[6];
    uint8_t channel;
    int8_t rssi_last, rssi_avg, rssi_min, rssi_max;
    uint8_t security;          /* dot11_security_t */
    uint32_t beacon_count;
    uint32_t frame_count;
    uint32_t last_seen_ms;
    uint8_t ssid_len;
    char ssid[32];             /* zero padded */
} tlm_ap_info_t;

typedef struct TLM_PACKED {
    uint32_t ts_ms;
    uint8_t channel;
    uint8_t ap_count;
    uint16_t reserved;
    uint32_t dwell_ms;         /* cumulative time spent listening on this channel */
    uint32_t frames, mgmt, ctrl, data, bytes;  /* cumulative */
    int32_t rssi_sum;          /* cumulative; mean = rssi_sum / rssi_n */
    uint32_t rssi_n;
} tlm_channel_stats_t;

typedef struct TLM_PACKED {
    uint32_t ts_ms;
    uint32_t total, mgmt, ctrl, data;          /* cumulative */
    uint32_t beacons, probe_req, probe_resp, deauth, retry;
    uint32_t pps;              /* frames in the last report interval */
} tlm_packet_stats_t;

typedef struct TLM_PACKED {
    uint32_t ts_ms;
    uint32_t free_heap, min_free_heap;
    uint16_t ring_capacity, ring_high_water;
    uint32_t ring_dropped;     /* frames lost because the ring was full */
    uint32_t tx_dropped;       /* telemetry frames lost on the USB link */
    uint8_t current_channel;
    uint8_t host_connected;
    uint16_t hop_dwell_ms;
} tlm_system_status_t;

_Static_assert(sizeof(tlm_ap_info_t) == 57, "AP_INFO size");
_Static_assert(sizeof(tlm_channel_stats_t) == 40, "CHANNEL_STATS size");
_Static_assert(sizeof(tlm_packet_stats_t) == 44, "PACKET_STATS size");
_Static_assert(sizeof(tlm_system_status_t) == 28, "SYSTEM_STATUS size");

uint16_t tlm_crc16(const uint8_t *data, size_t len);

/* Encode a frame into out (>= TLM_HEADER_SIZE + len + TLM_CRC_SIZE). Returns bytes, 0 on error. */
size_t tlm_encode(uint8_t type, uint32_t seq, const void *payload, size_t len, uint8_t *out);
