#!/usr/bin/env python3
"""Capture telemetry to SQLite (and optionally CSV).  Reconnects if the board is unplugged.

Usage:  python logger.py /dev/ttyACM0 --db data/capture.db [--csv-dir data/csv]
"""
from __future__ import annotations

import argparse
import csv
import sqlite3
import time
from pathlib import Path

from decoder import AP_INFO, CHANNEL_STATS, PACKET_STATS, SYSTEM_STATUS, Decoder

SCHEMA = """
CREATE TABLE IF NOT EXISTS ap_info (
  host_ts REAL, bssid TEXT, ssid TEXT, channel INT, rssi_last INT, rssi_avg INT, rssi_min INT,
  rssi_max INT, security TEXT, beacon_count INT, frame_count INT, last_seen_ms INT);
CREATE TABLE IF NOT EXISTS channel_stats (
  host_ts REAL, ts_ms INT, channel INT, ap_count INT, dwell_ms INT, frames INT, mgmt INT, ctrl INT,
  data INT, bytes INT, rssi_sum INT, rssi_n INT);
CREATE TABLE IF NOT EXISTS packet_stats (
  host_ts REAL, ts_ms INT, total INT, mgmt INT, ctrl INT, data INT, beacons INT, probe_req INT,
  probe_resp INT, deauth INT, retry INT, pps INT);
CREATE TABLE IF NOT EXISTS system_status (
  host_ts REAL, ts_ms INT, free_heap INT, min_free_heap INT, ring_capacity INT, ring_high_water INT,
  ring_dropped INT, tx_dropped INT, current_channel INT, host_connected INT, hop_dwell_ms INT);
CREATE TABLE IF NOT EXISTS events (host_ts REAL, kind TEXT, detail TEXT);
"""
TABLES = {AP_INFO: "ap_info", CHANNEL_STATS: "channel_stats",
          PACKET_STATS: "packet_stats", SYSTEM_STATUS: "system_status"}


def open_db(path: str) -> sqlite3.Connection:
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    db = sqlite3.connect(path)
    db.executescript(SCHEMA)
    return db


class Sink:
    def __init__(self, db: sqlite3.Connection, csv_dir: str | None) -> None:
        self.db = db
        self._csv: dict[str, tuple] = {}
        self.csv_dir = Path(csv_dir) if csv_dir else None
        if self.csv_dir:
            self.csv_dir.mkdir(parents=True, exist_ok=True)

    def event(self, kind: str, detail: str = "") -> None:
        self.db.execute("INSERT INTO events VALUES (?,?,?)", (time.time(), kind, detail))
        self.db.commit()
        print(f"[{time.strftime('%H:%M:%S')}] {kind} {detail}", flush=True)

    def write(self, msg) -> None:
        table = TABLES.get(msg.type)
        if not table:
            return
        row = {"host_ts": time.time(), **msg.data}
        cols = list(row)
        self.db.execute(f"INSERT INTO {table} ({','.join(cols)}) VALUES ({','.join('?' * len(cols))})",
                        [row[c] for c in cols])
        if self.csv_dir:
            if table not in self._csv:
                f = open(self.csv_dir / f"{table}.csv", "a", newline="")
                w = csv.DictWriter(f, fieldnames=cols)
                if f.tell() == 0:
                    w.writeheader()
                self._csv[table] = (f, w)
            self._csv[table][1].writerow(row)

    def flush(self) -> None:
        self.db.commit()
        for f, _ in self._csv.values():
            f.flush()


def run(port: str, db_path: str, csv_dir: str | None) -> None:
    import serial

    sink = Sink(open_db(db_path), csv_dir)
    while True:
        try:
            with serial.Serial(port, 115200, timeout=0.2) as ser:
                sink.event("connected", port)
                dec = Decoder()
                last_flush = last_report = time.time()
                prev = (0, 0, 0)
                while True:
                    chunk = ser.read(4096)
                    for m in dec.feed(chunk):
                        sink.write(m)
                    now = time.time()
                    if now - last_flush >= 1.0:
                        sink.flush()
                        last_flush = now
                    s = dec.stats
                    cur = (s.missing, s.crc_errors, s.device_resets)
                    if cur != prev:
                        sink.event("link", f"missing={s.missing} crc_errors={s.crc_errors} resets={s.device_resets}")
                        prev = cur
                    if now - last_report >= 10:
                        print(f"[{time.strftime('%H:%M:%S')}] frames={s.frames} missing={s.missing} "
                              f"crc_err={s.crc_errors} resync_bytes={s.resync_bytes}", flush=True)
                        last_report = now
        except (serial.SerialException, OSError) as e:
            sink.event("disconnected", str(e))
            time.sleep(1.0)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port")
    ap.add_argument("--db", default="data/capture.db")
    ap.add_argument("--csv-dir", default=None)
    a = ap.parse_args()
    try:
        run(a.port, a.db, a.csv_dir)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
