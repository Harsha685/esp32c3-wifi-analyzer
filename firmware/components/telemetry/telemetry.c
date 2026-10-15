#include "telemetry.h"

#include <string.h>

uint16_t tlm_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    while (len--) {
        crc ^= (uint16_t)(*data++) << 8;
        for (int i = 0; i < 8; i++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

size_t tlm_encode(uint8_t type, uint32_t seq, const void *payload, size_t len, uint8_t *out)
{
    if (len > TLM_MAX_PAYLOAD || (len && !payload) || !out) {
        return 0;
    }
    out[0] = TLM_MAGIC0;
    out[1] = TLM_MAGIC1;
    out[2] = type;
    out[3] = (uint8_t)len;
    out[4] = (uint8_t)(len >> 8);
    out[5] = (uint8_t)seq;
    out[6] = (uint8_t)(seq >> 8);
    out[7] = (uint8_t)(seq >> 16);
    out[8] = (uint8_t)(seq >> 24);
    if (len) {
        memcpy(out + TLM_HEADER_SIZE, payload, len);
    }
    uint16_t crc = tlm_crc16(out + 2, 7 + len);
    out[TLM_HEADER_SIZE + len] = (uint8_t)crc;
    out[TLM_HEADER_SIZE + len + 1] = (uint8_t)(crc >> 8);
    return TLM_HEADER_SIZE + len + TLM_CRC_SIZE;
}
