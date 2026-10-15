# ESP32-C3 Wi-Fi Network Analyzer

A Wi-Fi environment analyzer for the **Seeed Studio XIAO ESP32-C3**. The chip runs in promiscuous
mode, hops across 2.4 GHz channels, parses 802.11 frames, tracks per-AP and per-channel statistics,
and streams them over USB to a Python backend using a custom CRC-protected binary protocol.

Built with ESP-IDF + FreeRTOS (via PlatformIO), not Arduino.

```
Wi-Fi ─► promiscuous cb ─► SPSC ring ─► analyzer task ─► USB Serial/JTAG ─► decoder.py ─► SQLite/CSV ─► dashboard.py
          (copy only)      lock-free     parse + stats      framed binary        CRC, seq gaps
```

## Layout
```
firmware/   ESP-IDF app (PlatformIO). components: spsc_ring, dot11, wifi_stats, telemetry
host/       decoder.py (live view) · logger.py (SQLite/CSV, reconnect) · dashboard.py (plots) · tests/
protocol/   wire format spec
docs/       architecture, design decisions, limitations
experiments/ RF experiment procedures
```

## Quick start
Open `esp32c3-wifi-analyzer.code-workspace` in VS Code (PlatformIO extension required).

```bash
# 1. Firmware: build + flash (PlatformIO toolbar ✓ / → in VS Code, or:)
cd firmware
pio run -e seeed_xiao_esp32c3 -t upload

# 2. Host
cd ../host
python3 -m venv .venv && .venv/bin/pip install -r requirements.txt
.venv/bin/python decoder.py /dev/ttyACM0                      # live terminal table
.venv/bin/python logger.py  /dev/ttyACM0 --db data/capture.db  # record
.venv/bin/python dashboard.py --db data/capture.db             # plots (--save out.png)
```

**Milestone-1 text mode** (no host tools; stats printed on the serial console):
`pio run -e text -t upload && pio device monitor`

**Fixed channel** (no hopping): add `-DANALYZER_FIXED_CHANNEL=6` to `build_flags`.

If the board does not enumerate, hold **B** while plugging it in, then flash.
Only one program can hold `/dev/ttyACM0` at a time — close the serial monitor before running the logger.

## Tests
```bash
make -C firmware/test_native          # CRC, parser (incl. truncation), stats, ring buffer — ASan/UBSan
host/.venv/bin/python -m unittest discover -s host/tests   # decoder, resync, CRC, seq gaps, C/Python golden frame
```

## Status
| Phase | State |
|---|---|
| 2 Promiscuous capture · 3 Parser · 4 Stats · 5 Ring buffer | done |
| 6 Telemetry protocol · 7 Python receiver | done |
| 8 Visualization | basic dashboard |
| 10 Reliability | seq tracking, CRC, resync, drop counters, reconnect done; overflow stress test pending |
| 9 RF experiments · 11 Power · 12 Benchmarks · 13 Write-up | procedures in `experiments/`; measurements still to do |

See [docs/architecture.md](docs/architecture.md) for design decisions and limitations and
[protocol/protocol.md](protocol/protocol.md) for the wire format.
