# Architecture

```
 Wi-Fi driver task                 analyzer task (prio 5)               hop task (prio 6)
 ─────────────────                 ──────────────────────               ─────────────────
 promiscuous cb ──► SPSC ring ──►  parse 802.11 header                  set_channel(1..13)
 (copy ≤200 B,      256 slots      update stats (APs, channels,         every 200 ms
  never blocks)     lock-free      totals)  — single owner, no locks
                                   every 1 s: encode ─► USB Serial/JTAG ─► PC
```

## Decisions

| Decision | Why |
|---|---|
| Callback only copies into a ring | The Wi-Fi task must not stall; parsing in the callback would cause drops under load. |
| Lock-free SPSC ring, zero-copy slots | Exactly one producer and one consumer; atomics with acquire/release suffice. Full ⇒ drop and count, never block. |
| Stats owned by one task | No mutexes, no priority inversion; reports read them from the same task. |
| Cumulative counters on the wire | Lost telemetry frames cost resolution, not correctness; the PC computes deltas. |
| Channel from DS Parameter Set IE | The radio can receive a neighbouring channel's beacon; the IE states the AP's real channel. |
| Capture 200 B of management frames, 36 B of others | Beacon IEs (SSID, DS, RSN) need ~150 B; data/control only need the MAC header. |
| Security `unknown` on truncated capture | Better than mislabelling WPA2 as WEP when the RSN IE is cut off. |
| USB Serial/JTAG, logs disabled at runtime | No UART adapter needed; the stream stays clean. |
| Transport back-off | If nobody reads the port, USB writes time out; the device backs off 1 s instead of stalling the analyzer. |

## Limitations
- 2.4 GHz only (the ESP32-C3 has no 5 GHz radio); channels 1–13.
- Hopping means each channel is observed ~1/13 of the time. Short bursts are missed; use `-DANALYZER_FIXED_CHANNEL=n` to dwell on one channel.
- RSSI is measured at the C3's antenna and varies by several dB; it is not a calibrated power measurement.
- Frame payload is not captured beyond the MAC header / beacon IEs, and encrypted content is not decoded.
- WPA2 and WPA3 are both reported as `WPA2+` (RSN IE; AKM suites are not parsed yet).
- AP table holds 64 entries; the least-recently-seen AP is evicted when full.
- Not yet done: power measurements, long-run benchmarks, RF experiments (see `experiments/`).
