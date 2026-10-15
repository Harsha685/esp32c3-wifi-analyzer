# Telemetry protocol v1

Device → PC over USB Serial/JTAG (CDC-ACM). One-way stream; the PC never sends anything.
All multi-byte integers are **little-endian**.

```
┌────────┬──────┬──────┬───────┬─────────┬───────┐
│ MAGIC  │ TYPE │ LEN  │ SEQ   │ PAYLOAD │ CRC16 │
│ AA 55  │ u8   │ u16  │ u32   │ LEN B   │ u16   │
└────────┴──────┴──────┴───────┴─────────┴───────┘
```

- **CRC**: CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, no xorout; check value
  of `"123456789"` is `0x29B1`) over `TYPE .. PAYLOAD` (so LEN and SEQ are protected).
- **LEN**: payload bytes, max 128. A larger value is treated as garbage.
- **SEQ**: increments by one per frame *attempted* on the link, wraps at 2³². Frames skipped
  because no host is connected do not consume a number. A forward gap = frames lost in transit;
  a backward jump = device reset.
- **Resync**: on bad CRC or garbage the receiver drops one byte and searches for `AA 55` again.
  Boot ROM / bootloader text appears on the same port and is skipped this way.

## Messages (sent once per second unless noted)

### 0x01 AP_INFO — 57 bytes (every 2nd report, one per known AP)
| off | type | field |
|---|---|---|
| 0 | u8[6] | bssid |
| 6 | u8 | channel (from the DS Parameter Set IE when present) |
| 7 | i8 | rssi_last (dBm) |
| 8 | i8 | rssi_avg (EMA, α=1/8) |
| 9 | i8 | rssi_min |
| 10 | i8 | rssi_max |
| 11 | u8 | security: 0 unknown, 1 open, 2 WEP, 3 WPA, 4 WPA2/WPA3 (RSN IE) |
| 12 | u32 | beacon_count (beacons + probe responses) |
| 16 | u32 | frame_count (all frames attributed to this BSSID) |
| 20 | u32 | last_seen_ms (device uptime) |
| 24 | u8 | ssid_len |
| 25 | char[32] | ssid, zero padded (len 0 = hidden) |

### 0x02 CHANNEL_STATS — 40 bytes (one per channel 1..13)
`u32 ts_ms, u8 channel, u8 ap_count, u16 reserved, u32 dwell_ms, u32 frames, u32 mgmt, u32 ctrl,
u32 data, u32 bytes, i32 rssi_sum, u32 rssi_n`

Counters are **cumulative**. Because the radio hops, a channel only hears traffic while it is
tuned there: activity (frames/s) = Δframes / (Δdwell_ms / 1000). Mean RSSI = Δrssi_sum / Δrssi_n.

### 0x03 PACKET_STATS — 44 bytes
`u32 ts_ms, total, mgmt, ctrl, data, beacons, probe_req, probe_resp, deauth, retry, pps`
(cumulative except `pps`, the frames counted in the last interval; `deauth` includes disassoc).

### 0x04 SYSTEM_STATUS — 28 bytes
`u32 ts_ms, u32 free_heap, u32 min_free_heap, u16 ring_capacity, u16 ring_high_water,
u32 ring_dropped, u32 tx_dropped, u8 current_channel, u8 host_connected, u16 hop_dwell_ms`

- `ring_dropped`: frames the Wi-Fi callback could not enqueue (analyzer too slow).
- `tx_dropped`: telemetry frames the USB link could not accept (host not reading).

## Versioning
Payload sizes are fixed per type and asserted at compile time on the device and at import in
`host/decoder.py`. New fields ⇒ new type id or a new magic.
