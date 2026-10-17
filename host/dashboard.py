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


def with_gaps(pts: list, max_gap: float = 10.0) -> tuple[list, list]:
    """(x, y) lists with a None inserted wherever samples are > max_gap s apart, so the line breaks
    instead of joining across a period when nothing was logged."""
    xs: list = []
    ys: list = []
    for i, (t, v) in enumerate(pts):
        if i and t - pts[i - 1][0] > max_gap:
            xs.append(t)
            ys.append(None)
        xs.append(t)
        ys.append(v)
    return xs, ys


def draw(axes, d, name_filter: str | None = None) -> None:
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
        if name_filter:
            chosen = [b for b in series if name_filter.lower() in names[b].lower()]
        else:
            mean = lambda b: sum(r for _, r in series[b]) / len(series[b])
            chosen = sorted(series, key=lambda b: -mean(b))[:6]
        for bssid in chosen:
            xs, ys = with_gaps(series[bssid])
            ax_rssi.plot([x - t0 for x in xs], ys, label=names[bssid])
        if chosen:
            ax_rssi.legend(fontsize=7, loc="lower right")
    ax_rssi.set(title="RSSI vs time", xlabel="s", ylabel="dBm")

    # 2. Channel activity: frames per second spent listening. Counters are cumulative and restart at 0
    #    when the board resets, so sum the per-sample increases and skip any step that goes backwards.
    prev: dict[int, tuple] = {}
    frames_sum = {c: 0 for c in CHANNELS}
    dwell_sum = {c: 0 for c in CHANNELS}
    for _, ch, dwell, frames, _ in d["ch"]:
        if ch in prev and dwell >= prev[ch][0] and frames >= prev[ch][1]:
            dwell_sum[ch] += dwell - prev[ch][0]
            frames_sum[ch] += frames - prev[ch][1]
        prev[ch] = (dwell, frames)
    rates = [frames_sum[c] / (dwell_sum[c] / 1000) if dwell_sum[c] else 0 for c in CHANNELS]
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
        ts = [t - t0 for t, _ in d["pkt"]]
        ys = [p for _, p in d["pkt"]]
        ax_pps.plot(*with_gaps(list(zip(ts, ys))), alpha=0.4, label="1 s")
        w = 13
        if len(ys) >= w:
            sm = [sum(ys[i - w + 1:i + 1]) / w for i in range(w - 1, len(ys))]
            ax_pps.plot(*with_gaps(list(zip(ts[w - 1:], sm))), label="13 s mean (one hop cycle)")
        ax_pps.legend(fontsize=7)
    ax_pps.set(title="Packets per second", xlabel="s", ylabel="frames/s")


def main() -> None:
    import matplotlib.pyplot as plt

    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--db", default="data/capture.db")
    ap.add_argument("--save", help="write a PNG and exit")
    ap.add_argument("--minutes", type=float,
                    help="only the last N minutes (live default: 10; with --save default: everything)")
    ap.add_argument("--ssid", help="plot only networks whose name contains this text")
    ap.add_argument("--interval", type=float, default=2.0, help="live refresh seconds")
    a = ap.parse_args()
    if a.minutes is None and not a.save:
        a.minutes = 10  # live view: otherwise old runs in the same DB squash new data to the right edge

    if a.save:
        import matplotlib
        matplotlib.use("Agg")
    db = sqlite3.connect(a.db, timeout=30)
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
