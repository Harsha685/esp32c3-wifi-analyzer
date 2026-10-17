#!/usr/bin/env python3
"""Plot a capture database: RSSI vs time, channel activity, APs per channel, packets/sec.

Usage:  python dashboard.py --db data/capture.db            # live, refreshes every 2 s
        python dashboard.py --db data/capture.db --save out.png
        python dashboard.py --db data/capture.db --minutes 10 --ssid Harsha   # one network by name
"""
from __future__ import annotations

import argparse
import sqlite3
from collections import defaultdict

CHANNELS = list(range(1, 14))


def load(db: sqlite3.Connection, minutes: float | None):
    cutoff = "" if minutes is None else f"WHERE host_ts > strftime('%s','now') - {minutes * 60}"
    q = lambda sql: db.execute(sql.format(w=cutoff)).fetchall()
    return {
        "ap": q("SELECT host_ts, bssid, ssid, rssi_last, channel FROM ap_info {w} ORDER BY host_ts"),
        "ch": q("SELECT host_ts, channel, dwell_ms, frames, ap_count FROM channel_stats {w} ORDER BY host_ts"),
        "pkt": q("SELECT host_ts, pps FROM packet_stats {w} ORDER BY host_ts"),
    }


def draw(axes, d, ssid: str | None = None) -> None:
    ax_rssi, ax_act, ax_aps, ax_pps = axes
    for a in axes:
        a.clear()

    # 1. RSSI vs time: the 6 strongest APs (by mean RSSI), or every AP whose name contains --ssid
    series: dict[str, list] = defaultdict(list)
    names: dict[str, str] = {}
    for ts, bssid, ssid, rssi, _ in d["ap"]:
        series[bssid].append((ts, rssi))
        names[bssid] = ssid or bssid
    if series:
        t0 = d["ap"][0][0]
        if ssid:
            chosen = [b for b in series if ssid.lower() in names[b].lower()]
        else:
            mean = lambda b: sum(r for _, r in series[b]) / len(series[b])
            chosen = sorted(series, key=lambda b: -mean(b))[:6]
        for bssid in chosen:
            pts = series[bssid]
            ax_rssi.plot([t - t0 for t, _ in pts], [r for _, r in pts], label=names[bssid])
        if chosen:
            ax_rssi.legend(fontsize=7, loc="lower right")
    ax_rssi.set(title="RSSI vs time", xlabel="s", ylabel="dBm")

    # 2. Channel activity: frames per second spent listening (delta frames / delta dwell)
    first: dict[int, tuple] = {}
    last: dict[int, tuple] = {}
    for _, ch, dwell, frames, _ in d["ch"]:
        first.setdefault(ch, (dwell, frames))
        last[ch] = (dwell, frames)
    rates = []
    for ch in CHANNELS:
        if ch in last and last[ch][0] > first[ch][0]:
            rates.append((last[ch][1] - first[ch][1]) / ((last[ch][0] - first[ch][0]) / 1000))
        else:
            rates.append(0)
    ax_act.bar(CHANNELS, rates)
    ax_act.set(title="Channel activity", xlabel="channel", ylabel="frames/s while listening",
               xticks=CHANNELS)

    # 3. APs per channel (latest snapshot)
    latest = {}
    for _, ch, _, _, aps in d["ch"]:
        latest[ch] = aps
    ax_aps.bar(CHANNELS, [latest.get(c, 0) for c in CHANNELS], color="tab:green")
    ax_aps.set(title="APs per channel", xlabel="channel", ylabel="APs", xticks=CHANNELS)

    # 4. packets/sec  (only meaningful averaged over a hop cycle, so also plot a 13 s mean)
    if d["pkt"]:
        t0 = d["pkt"][0][0]
        xs = [t - t0 for t, _ in d["pkt"]]
        ys = [p for _, p in d["pkt"]]
        ax_pps.plot(xs, ys, alpha=0.4, label="1 s")
        w = 13
        if len(ys) >= w:
            sm = [sum(ys[i - w + 1:i + 1]) / w for i in range(w - 1, len(ys))]
            ax_pps.plot(xs[w - 1:], sm, label="13 s mean (one hop cycle)")
        ax_pps.legend(fontsize=7)
    ax_pps.set(title="Packets per second", xlabel="s", ylabel="frames/s")


def main() -> None:
    import matplotlib.pyplot as plt

    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--db", default="data/capture.db")
    ap.add_argument("--save", help="write a PNG and exit")
    ap.add_argument("--minutes", type=float, help="only the last N minutes")
    ap.add_argument("--ssid", help="plot only networks whose name contains this text")
    ap.add_argument("--interval", type=float, default=2.0, help="live refresh seconds")
    a = ap.parse_args()

    if a.save:
        import matplotlib
        matplotlib.use("Agg")
    db = sqlite3.connect(a.db)
    fig, axes = plt.subplots(2, 2, figsize=(13, 8))
    axes = axes.flatten()
    draw(axes, load(db, a.minutes), a.ssid)
    fig.tight_layout()
    if a.save:
        fig.savefig(a.save, dpi=110)
        print("saved", a.save)
        return
    plt.ion()
    plt.show()
    while plt.fignum_exists(fig.number):
        draw(axes, load(db, a.minutes), a.ssid)
        fig.tight_layout()
        plt.pause(a.interval)


if __name__ == "__main__":
    main()
