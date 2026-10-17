#!/usr/bin/env python3
"""List the networks heard recently, grouped by channel.

Usage:  python list_aps.py --db data/capture.db               # all channels, last 5 minutes
        python list_aps.py --db data/capture.db --channel 11  # one channel
"""
from __future__ import annotations

import argparse
import sqlite3
from collections import defaultdict


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--db", default="data/capture.db")
    ap.add_argument("--channel", type=int, help="only this channel")
    ap.add_argument("--minutes", type=float, default=5, help="only networks heard in the last N minutes")
    a = ap.parse_args()

    db = sqlite3.connect(a.db)
    # newest report per BSSID within the window
    rows = db.execute(
        """SELECT channel, ssid, bssid, rssi_last, security, beacon_count FROM ap_info
           WHERE host_ts > strftime('%s','now') - ? AND host_ts = (
               SELECT MAX(host_ts) FROM ap_info b WHERE b.bssid = ap_info.bssid)
           ORDER BY channel, rssi_last DESC""", (a.minutes * 60,)).fetchall()

    by_ch = defaultdict(list)
    for ch, ssid, bssid, rssi, sec, beacons in rows:
        if a.channel is None or ch == a.channel:
            by_ch[ch].append((ssid or "<hidden>", bssid, rssi, sec, beacons))
    if not by_ch:
        print(f"nothing heard in the last {a.minutes:g} min (is the logger running?)")
    for ch in sorted(by_ch):
        print(f"\nchannel {ch}: {len(by_ch[ch])} network(s)")
        print(f"  {'SSID':<26} {'BSSID':<18} {'RSSI':>5}  {'SEC':<6} beacons")
        for ssid, bssid, rssi, sec, beacons in by_ch[ch]:
            print(f"  {ssid:<26} {bssid:<18} {rssi:>5}  {sec:<6} {beacons}")


if __name__ == "__main__":
    main()
