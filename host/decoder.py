#!/usr/bin/env python3
"""Telemetry protocol decoder + live terminal view.

Frame:  | AA 55 | TYPE u8 | LEN u16 | SEQ u32 | PAYLOAD | CRC16 u16 |   (little-endian)
CRC-16/CCITT-FALSE over TYPE..PAYLOAD.  See ../protocol/protocol.md.

Usage:  python decoder.py /dev/ttyACM0            # live table
        python decoder.py /dev/ttyACM0 --raw      # one line per message
"""
from __future__ import annotations

import argparse
import struct
import sys
import time
from dataclasses import dataclass, field
from typing import Iterator

MAGIC = b"\xaa\x55"
HEADER = struct.Struct("<2sBHI")  # magic, type, len, seq  (9 bytes)
MAX_PAYLOAD = 128

AP_INFO, CHANNEL_STATS, PACKET_STATS, SYSTEM_STATUS = 0x01, 0x02, 0x03, 0x04
TYPE_NAMES = {AP_INFO: "AP_INFO", CHANNEL_STATS: "CHANNEL_STATS",
              PACKET_STATS: "PACKET_STATS", SYSTEM_STATUS: "SYSTEM_STATUS"}
SECURITY = {0: "?", 1: "OPEN", 2: "WEP", 3: "WPA", 4: "WPA2+"}

_AP = struct.Struct("<6sBbbbbBIIIB32s")
_CH = struct.Struct("<IBBHIIIIIIiI")
_PKT = struct.Struct("<11I")
_SYS = struct.Struct("<IIIHHIIBBH")
assert (_AP.size, _CH.size, _PKT.size, _SYS.size) == (57, 40, 44, 28)


def crc16(data: bytes, crc: int = 0xFFFF) -> int:
    """CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection)."""
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def encode(msg_type: int, seq: int, payload: bytes) -> bytes:
    """Build a frame (used by tests and for replay tooling)."""
    body = struct.pack("<BHI", msg_type, len(payload), seq) + payload
    return MAGIC + body + struct.pack("<H", crc16(body))


@dataclass
class Message:
    type: int
    seq: int
    payload: bytes
    data: dict = field(default_factory=dict)

    @property
    def name(self) -> str:
        return TYPE_NAMES.get(self.type, f"0x{self.type:02x}")


def parse_payload(msg_type: int, p: bytes) -> dict:
    """Decode a payload into a dict.  Raises struct.error on a wrong-sized payload."""
    if msg_type == AP_INFO:
        (bssid, ch, rl, ra, rmin, rmax, sec, beacons, frames, seen, sl, ssid) = _AP.unpack(p)
        return dict(bssid=":".join(f"{b:02x}" for b in bssid), channel=ch, rssi_last=rl,
                    rssi_avg=ra, rssi_min=rmin, rssi_max=rmax, security=SECURITY.get(sec, "?"),
                    beacon_count=beacons, frame_count=frames, last_seen_ms=seen,
                    ssid=ssid[:min(sl, 32)].decode("utf-8", "replace"))
    if msg_type == CHANNEL_STATS:
        (ts, ch, aps, _r, dwell, frames, mgmt, ctrl, data, nbytes, rsum, rn) = _CH.unpack(p)
        return dict(ts_ms=ts, channel=ch, ap_count=aps, dwell_ms=dwell, frames=frames, mgmt=mgmt,
                    ctrl=ctrl, data=data, bytes=nbytes, rssi_sum=rsum, rssi_n=rn)
    if msg_type == PACKET_STATS:
        keys = ("ts_ms", "total", "mgmt", "ctrl", "data", "beacons", "probe_req", "probe_resp",
                "deauth", "retry", "pps")
        return dict(zip(keys, _PKT.unpack(p)))
    if msg_type == SYSTEM_STATUS:
        keys = ("ts_ms", "free_heap", "min_free_heap", "ring_capacity", "ring_high_water",
                "ring_dropped", "tx_dropped", "current_channel", "host_connected", "hop_dwell_ms")
        return dict(zip(keys, _SYS.unpack(p)))
    return {}


@dataclass
class LinkStats:
    frames: int = 0
    bytes_ok: int = 0
    crc_errors: int = 0
    resync_bytes: int = 0      # bytes discarded while hunting for a frame start
    bad_payloads: int = 0
    missing: int = 0           # frames lost in transit, from SEQ gaps
    device_resets: int = 0     # SEQ went backwards (device rebooted)


class Decoder:
    """Incremental stream decoder: feed() raw bytes, get back validated Messages.

    Self-synchronising: on garbage or a CRC failure it skips one byte and re-hunts for MAGIC,
    so boot-log text, truncated frames and mid-stream attaches all recover on their own.
    """

    def __init__(self) -> None:
        self._buf = bytearray()
        self._last_seq: int | None = None
        self.stats = LinkStats()

    def feed(self, data: bytes) -> list[Message]:
        return list(self._feed(data))

    def _feed(self, data: bytes) -> Iterator[Message]:
        buf = self._buf
        buf += data
        while True:
            i = buf.find(MAGIC)
            if i < 0:
                # keep a trailing 0xAA: it may be the first half of MAGIC
                keep = 1 if buf.endswith(MAGIC[:1]) else 0
                self.stats.resync_bytes += len(buf) - keep
                del buf[:len(buf) - keep]
                return
            if i:
                self.stats.resync_bytes += i
                del buf[:i]
            if len(buf) < HEADER.size:
                return
            _, mtype, length, seq = HEADER.unpack_from(buf)
            if length > MAX_PAYLOAD:
                self.stats.resync_bytes += 1
                del buf[:1]
                continue
            total = HEADER.size + length + 2
            if len(buf) < total:
                return
            (rx_crc,) = struct.unpack_from("<H", buf, total - 2)
            if crc16(bytes(buf[2:total - 2])) != rx_crc:
                self.stats.crc_errors += 1
                self.stats.resync_bytes += 1
                del buf[:1]
                continue
            payload = bytes(buf[HEADER.size:total - 2])
            del buf[:total]
            self.stats.frames += 1
            self.stats.bytes_ok += total
            self._track_seq(seq)
            msg = Message(mtype, seq, payload)
            try:
                msg.data = parse_payload(mtype, payload)
            except struct.error:
                self.stats.bad_payloads += 1
                continue
            yield msg

    def _track_seq(self, seq: int) -> None:
        if self._last_seq is not None:
            gap = (seq - self._last_seq - 1) & 0xFFFFFFFF
            if gap < 0x80000000:
                self.stats.missing += gap
            else:
                self.stats.device_resets += 1
        self._last_seq = seq


# ----------------------------------------------------------------------------- live view

def _live_view(port: str, baud: int, raw: bool) -> None:
    import serial  # imported lazily so the decoder is usable without pyserial

    dec = Decoder()
    aps: dict[str, dict] = {}
    pkt: dict = {}
    sysd: dict = {}
    last_draw = 0.0
    with serial.Serial(port, baud, timeout=0.2) as ser:
        while True:
            for m in dec.feed(ser.read(4096)):
                if raw:
                    print(f"{m.seq:>8} {m.name:<14} {m.data}")
                elif m.type == AP_INFO:
                    aps[m.data["bssid"]] = m.data
                elif m.type == PACKET_STATS:
                    pkt = m.data
                elif m.type == SYSTEM_STATUS:
                    sysd = m.data
            if raw or time.time() - last_draw < 1.0:
                continue
            last_draw = time.time()
            s = dec.stats
            out = ["\x1b[2J\x1b[H"]
            out.append(f"ESP32-C3 Wi-Fi analyzer  |  {pkt.get('pps', 0)} pps  total {pkt.get('total', 0)}  "
                       f"beacons {pkt.get('beacons', 0)}  retry {pkt.get('retry', 0)}  "
                       f"deauth {pkt.get('deauth', 0)}  ch {sysd.get('current_channel', '-')}")
            out.append(f"link: ok {s.frames}  crc_err {s.crc_errors}  missing {s.missing}  "
                       f"resets {s.device_resets}  |  dev: ring {sysd.get('ring_high_water', 0)}/"
                       f"{sysd.get('ring_capacity', 0)} drop {sysd.get('ring_dropped', 0)} "
                       f"txdrop {sysd.get('tx_dropped', 0)} heap {sysd.get('free_heap', 0)}")
            out.append("")
            out.append(f"{'BSSID':<18} {'ch':>2} {'rssi':>5} {'avg':>4} {'sec':<6} {'beacons':>7}  SSID")
            for a in sorted(aps.values(), key=lambda a: -a["rssi_last"])[:25]:
                out.append(f"{a['bssid']:<18} {a['channel']:>2} {a['rssi_last']:>5} {a['rssi_avg']:>4} "
                           f"{a['security']:<6} {a['beacon_count']:>7}  {a['ssid'] or '<hidden>'}")
            sys.stdout.write("\n".join(out) + "\n")
            sys.stdout.flush()


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port")
    ap.add_argument("--baud", type=int, default=115200, help="ignored by USB-CDC, kept for UART adapters")
    ap.add_argument("--raw", action="store_true", help="print every decoded message")
    args = ap.parse_args()
    try:
        _live_view(args.port, args.baud, args.raw)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
