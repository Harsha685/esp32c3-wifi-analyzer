# Learning resources — Rust port

Read `learning-resources.md` first: the theory there (802.11, lock-free queues and memory ordering,
CRC/framing) is language-independent and applies unchanged. This page adds what is Rust-specific.
Links checked 2026-10-16. The `esp-hal` / `esp-radio` crates move fast and parts are marked `unstable`:
prefer their docs and examples over tutorials more than a few months old.

## 1. Rust itself (skip what you know)
- [The Rust Programming Language](https://doc.rust-lang.org/book/) — ownership, borrowing, traits, error handling. Chapters 4, 10, 13, 15–16 matter most here.
- [Rust Atomics and Locks (free online)](http://mara.nl/atomics/) — the Rust equivalent of the Preshing articles: `Ordering`, building a lock-free queue. Read before porting `spsc_ring.c`.

## 2. Embedded Rust concepts
- [The Embedded Rust Book](https://docs.rust-embedded.org/book/) — `no_std`, peripherals, interrupts, concurrency. Examples are Cortex-M; the ideas transfer to RISC-V.
- [`embedded-hal` 1.0 docs](https://docs.rs/embedded-hal/latest/embedded_hal/) — the traits (GPIO, SPI, I²C, delay) that drivers are written against. It does **not** cover Wi-Fi: capture goes through `esp-radio`, not `embedded-hal`.
- [Discovery MB2 book](https://docs.rust-embedded.org/discovery-mb2/) — optional hands-on course; uses a micro:bit board, not yours.

## 3. Espressif + Rust (this chip)
- [The Rust on ESP Book](https://docs.espressif.com/projects/rust/book/) — toolchain setup, `esp-hal` vs `std` on ESP-IDF, project generation.
- [Embedded Rust (`no_std`) on Espressif training](https://docs.espressif.com/projects/rust/no_std-training/) — hands-on course for ESP32-C3 (uses a different dev board, same chip).
- [`esp-hal` API docs (ESP32-C3)](https://docs.espressif.com/projects/rust/esp-hal/latest/esp32c3/esp_hal/index.html) — GPIO, timers, USB Serial/JTAG.
- [`esp-radio` API docs (ESP32-C3)](https://docs.espressif.com/projects/rust/esp-radio/latest/esp32c3/esp_radio/index.html) — Wi-Fi; look for the `wifi::sniffer` module (promiscuous callback, unstable feature).
- [esp-rs on GitHub](https://github.com/esp-rs) and [awesome-esp-rust](https://github.com/esp-rs/awesome-esp-rust) — examples and a curated list. The `esp-hal` repo's `examples/` are the best working references.

## 4. Async and data structures
- [The Embassy book](https://embassy.dev/book/) — async executor for embedded; the natural replacement for the FreeRTOS analyzer/hop tasks.
- [`heapless::spsc`](https://docs.rs/heapless/latest/heapless/spsc/index.html) — fixed-capacity lock-free SPSC queue; compare against your own ring buffer.

## Suggested order
1. Rust Book (as needed) → Rust Atomics and Locks (ch. 1–5)
2. Embedded Rust Book → Rust on ESP Book → `no_std` training (blink, UART, a task)
3. Port the pure logic (parser, stats, CRC) as a `no_std` crate with `cargo test` on the PC
4. Add Wi-Fi sniffer via `esp-radio`, the hop task (Embassy), then USB output
5. Compare with the C version: RAM, flash, frames/s, drops
