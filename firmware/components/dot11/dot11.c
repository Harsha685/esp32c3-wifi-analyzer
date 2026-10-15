#include "dot11.h"

#include <string.h>

#define IE_SSID      0
#define IE_DS_PARAM  3
#define IE_RSN       48
#define IE_VENDOR    221

bool dot11_parse_header(const uint8_t *f, size_t len, dot11_hdr_t *h)
{
    if (!f || !h || len < 10) {
        return false;
    }
    memset(h, 0, sizeof(*h));

    uint8_t fc0 = f[0], fc1 = f[1];
    h->type = (fc0 >> 2) & 0x3;
    h->subtype = (fc0 >> 4) & 0xF;
    h->to_ds = fc1 & 0x01;
    h->from_ds = fc1 & 0x02;
    h->more_frag = fc1 & 0x04;
    h->retry = fc1 & 0x08;
    h->pwr_mgmt = fc1 & 0x10;
    h->more_data = fc1 & 0x20;
    h->protected_frame = fc1 & 0x40;
    h->order = fc1 & 0x80;
    h->duration = (uint16_t)(f[2] | (f[3] << 8));
    memcpy(h->addr1, f + 4, 6);

    if (h->type == DOT11_TYPE_CTRL) {
        /* CTS (12) and ACK (13) carry only the receiver address. */
        if (h->subtype == 12 || h->subtype == 13) {
            return true;
        }
        if (len < 16) {
            return false;
        }
        memcpy(h->addr2, f + 10, 6);
        h->has_addr2 = true;
        return true;
    }

    if (len < 24) {
        return false;
    }
    memcpy(h->addr2, f + 10, 6);
    memcpy(h->addr3, f + 16, 6);
    h->has_addr2 = h->has_addr3 = true;
    uint16_t sc = (uint16_t)(f[22] | (f[23] << 8));
    h->frag_num = sc & 0xF;
    h->seq_num = sc >> 4;

    if (h->type == DOT11_TYPE_MGMT) {
        memcpy(h->bssid, h->addr3, 6);
        h->has_bssid = true;
    } else if (h->type == DOT11_TYPE_DATA) {
        if (!h->to_ds && !h->from_ds) {
            memcpy(h->bssid, h->addr3, 6);
        } else if (h->to_ds && !h->from_ds) {
            memcpy(h->bssid, h->addr1, 6);
        } else if (!h->to_ds && h->from_ds) {
            memcpy(h->bssid, h->addr2, 6);
        } else {
            memcpy(h->bssid, h->addr3, 6); /* WDS: addr4 follows, no single BSSID */
        }
        h->has_bssid = true;
    }
    return true;
}

bool dot11_parse_beacon(const uint8_t *f, size_t len, size_t full_len, dot11_beacon_info_t *b)
{
    dot11_hdr_t h;
    if (!b || !dot11_parse_header(f, len, &h) || h.type != DOT11_TYPE_MGMT ||
        (h.subtype != DOT11_MGMT_BEACON && h.subtype != DOT11_MGMT_PROBE_RESP)) {
        return false;
    }
    /* 24 header + 8 timestamp + 2 interval + 2 capability */
    if (len < 36) {
        return false;
    }
    memset(b, 0, sizeof(*b));
    b->beacon_interval_tu = (uint16_t)(f[32] | (f[33] << 8));
    b->capability = (uint16_t)(f[34] | (f[35] << 8));

    bool has_rsn = false, has_wpa = false;
    size_t off = 36;
    while (off + 2 <= len) {
        uint8_t id = f[off], ielen = f[off + 1];
        if (off + 2 + ielen > len) {
            break; /* truncated capture */
        }
        const uint8_t *ie = f + off + 2;
        switch (id) {
        case IE_SSID:
            b->has_ssid = true;
            b->ssid_len = ielen > DOT11_MAX_SSID ? DOT11_MAX_SSID : ielen;
            memcpy(b->ssid, ie, b->ssid_len);
            b->ssid[b->ssid_len] = '\0';
            break;
        case IE_DS_PARAM:
            if (ielen >= 1) {
                b->has_ds_channel = true;
                b->ds_channel = ie[0];
            }
            break;
        case IE_RSN:
            has_rsn = true;
            break;
        case IE_VENDOR:
            /* Microsoft OUI 00:50:F2, type 1 = WPA */
            if (ielen >= 4 && ie[0] == 0x00 && ie[1] == 0x50 && ie[2] == 0xF2 && ie[3] == 0x01) {
                has_wpa = true;
            }
            break;
        default:
            break;
        }
        off += 2 + ielen;
    }

    if (has_rsn) {
        b->security = DOT11_SEC_WPA2;
    } else if (has_wpa) {
        b->security = DOT11_SEC_WPA;
    } else if (len < full_len && (b->capability & 0x0010)) {
        b->security = DOT11_SEC_UNKNOWN;  /* RSN IE may lie beyond the captured bytes */
    } else if (b->capability & 0x0010) {
        b->security = DOT11_SEC_WEP;
    } else {
        b->security = DOT11_SEC_OPEN;
    }
    return true;
}

const char *dot11_type_name(uint8_t type)
{
    switch (type) {
    case DOT11_TYPE_MGMT: return "MGMT";
    case DOT11_TYPE_CTRL: return "CTRL";
    case DOT11_TYPE_DATA: return "DATA";
    default: return "EXT";
    }
}

const char *dot11_security_name(dot11_security_t sec)
{
    switch (sec) {
    case DOT11_SEC_OPEN: return "OPEN";
    case DOT11_SEC_WEP: return "WEP";
    case DOT11_SEC_WPA: return "WPA";
    case DOT11_SEC_WPA2: return "WPA2+";
    default: return "?";
    }
}
