#pragma once
/* Minimal IEEE 802.11 frame parser: MAC header + beacon/probe-response IEs. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DOT11_TYPE_MGMT 0
#define DOT11_TYPE_CTRL 1
#define DOT11_TYPE_DATA 2

#define DOT11_MGMT_ASSOC_REQ    0
#define DOT11_MGMT_ASSOC_RESP   1
#define DOT11_MGMT_PROBE_REQ    4
#define DOT11_MGMT_PROBE_RESP   5
#define DOT11_MGMT_BEACON       8
#define DOT11_MGMT_DISASSOC     10
#define DOT11_MGMT_AUTH         11
#define DOT11_MGMT_DEAUTH       12

#define DOT11_MAX_SSID 32

typedef struct {
    uint8_t type;      /* 0 mgmt, 1 ctrl, 2 data, 3 ext */
    uint8_t subtype;
    bool to_ds, from_ds, more_frag, retry, pwr_mgmt, more_data, protected_frame, order;
    uint16_t duration;
    uint8_t addr1[6], addr2[6], addr3[6];
    bool has_addr2, has_addr3;
    uint16_t seq_num;  /* valid if has_addr3 */
    uint8_t frag_num;
    uint8_t bssid[6];
    bool has_bssid;
} dot11_hdr_t;

typedef enum {
    DOT11_SEC_UNKNOWN = 0,
    DOT11_SEC_OPEN = 1,
    DOT11_SEC_WEP = 2,
    DOT11_SEC_WPA = 3,
    DOT11_SEC_WPA2 = 4,   /* RSN IE present (WPA2 / WPA3) */
} dot11_security_t;

typedef struct {
    bool has_ssid;
    uint8_t ssid_len;
    char ssid[DOT11_MAX_SSID + 1];
    bool has_ds_channel;
    uint8_t ds_channel;
    uint16_t beacon_interval_tu;
    uint16_t capability;
    dot11_security_t security;
} dot11_beacon_info_t;

/* Parse the MAC header from a (possibly truncated) frame. False if too short. */
bool dot11_parse_header(const uint8_t *frame, size_t len, dot11_hdr_t *out);

/* Parse fixed fields + IEs of a beacon or probe response. False if not one / too short.
 * len = bytes captured, full_len = on-air length. If the capture is truncated and no RSN/WPA IE
 * was seen, security is DOT11_SEC_UNKNOWN rather than a guess. */
bool dot11_parse_beacon(const uint8_t *frame, size_t len, size_t full_len, dot11_beacon_info_t *out);

const char *dot11_type_name(uint8_t type);
const char *dot11_security_name(dot11_security_t sec);
