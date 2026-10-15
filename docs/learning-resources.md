# Learning resources

Read these in order **before** reading the code. Assumed background: embedded C, FreeRTOS basics, Python.
Rough time: 15–20 h in total. Links were checked on 2026-10-15; ESP-IDF docs move between versions, so
if one breaks, search the page title.

## 1. Wi-Fi / 802.11 basics (~4 h) — start here
What the radio is actually capturing.
- [Wireshark wiki: WLAN (IEEE 802.11) capture setup](https://wiki.wireshark.org/CaptureSetup/WLAN) — monitor mode, why management and control frames are normally hidden.
- [Wikipedia: 802.11 frame types](https://en.wikipedia.org/wiki/802.11_frame_types) — Frame Control field, type/subtype, ToDS/FromDS, Retry. Maps directly to `dot11.c`.
- [Wikipedia: Beacon frame](https://en.wikipedia.org/wiki/Beacon_frame) — fixed fields and information elements (SSID, DS Parameter Set, RSN).
- Exercise: capture beacons in Wireshark (if you have a monitor-mode adapter) and compare BSSID / channel / RSSI with what the analyzer reports.

## 2. This board + ESP-IDF (~4 h)
- [Seeed wiki: XIAO ESP32-C3 getting started](https://wiki.seeedstudio.com/XIAO_ESP32C3_Getting_Started/) — pinout, USB, boot button.
- [ESP-IDF: Get Started (ESP32-C3)](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/get-started/index.html) — project layout, components, build system.
- [ESP-IDF: Wi-Fi Driver → Wi-Fi Modes → Sniffer mode](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-guides/wifi-driver/wifi-modes.html#wi-fi-sniffer-mode) — what the promiscuous callback receives, filters, and its limits.
- The exact function signatures (`esp_wifi_set_promiscuous`, `..._rx_cb`, `..._filter`) are in the ESP-IDF "Wi-Fi API" reference for your version.

## 3. FreeRTOS refresher (~2 h, you already have the basics)
- [Mastering the FreeRTOS Real Time Kernel (official page, free PDF)](https://www.freertos.org/Documentation/02-Kernel/07-Books-and-manual/01-RTOS_book) — read the chapters on tasks, priorities and queues; focus on why you must not block inside a driver callback.

## 4. Lock-free ring buffer & memory ordering (~4 h) — the hardest part
Needed for `components/spsc_ring/`.
- [Preshing: An Introduction to Lock-Free Programming](https://preshing.com/20120612/an-introduction-to-lock-free-programming/)
- [Preshing: Acquire and Release Semantics](https://preshing.com/20120913/acquire-and-release-semantics/) — explains exactly why `commit()` uses release and `peek()` uses acquire.
- [Rigtorp: Optimizing a Ring Buffer for Throughput](https://rigtorp.se/ringbuffer/) — SPSC queue design; a good "next optimization" for this project.
- [Herb Sutter: atomic<> Weapons (C++ memory model talk, 2 parts)](https://isocpp.org/blog/2013/02/atomic-weapons-the-c-memory-model-and-modern-hardware-herb-sutter) — C++, but C11 `<stdatomic.h>` uses the same model.
- Exercise: shrink the ring to 4 slots and watch `ring_dropped`; then change `memory_order_release` to `relaxed` and reason about what could go wrong.

## 5. Protocol framing & CRC (~3 h)
Needed for `components/telemetry/` and `host/decoder.py`.
- [Wikipedia: Cyclic redundancy check](https://en.wikipedia.org/wiki/Cyclic_redundancy_check) — what the CRC-16/CCITT-FALSE in `protocol.md` computes.
- [Wikipedia: Consistent Overhead Byte Stuffing](https://en.wikipedia.org/wiki/Consistent_Overhead_Byte_Stuffing) — an alternative framing scheme; compare with the magic-byte + resync approach used here.
- Exercise: corrupt one byte in `host/tests/test_decoder.py` and trace how the decoder resynchronises.

## 6. Python host side (skim, ~1 h)
- [`struct` — packed binary data](https://docs.python.org/3/library/struct.html) — the format strings in `decoder.py`.
- [pySerial documentation](https://pyserial.readthedocs.io/en/latest/)
- `sqlite3` and `matplotlib` standard docs as needed.

## Suggested reading order for the code
1. `protocol/protocol.md` → `docs/architecture.md`
2. `firmware/components/dot11/` (parser) and `wifi_stats/`
3. `firmware/components/spsc_ring/`
4. `firmware/main/main.c` (how the pieces connect)
5. `firmware/components/telemetry/` and `host/decoder.py`
6. `host/logger.py`, `host/dashboard.py`
