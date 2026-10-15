# RF experiments

Run the logger for each condition, then compare with SQL or `host/dashboard.py`.
Record the setup (distance, location, antenna orientation, time) in each folder's `NOTES.md`.

| Folder | Experiment | Procedure |
|---|---|---|
| `rssi/` | Distance vs RSSI | Fixed AP; capture 60 s at 1, 2, 5, 10 m, antenna orientation constant. Compare `rssi_avg` for that BSSID. |
| `channel/` | Channel environment | Build with `-DANALYZER_FIXED_CHANNEL=1` / `6` / `11`, capture 5 min each. Compare frames/s, retry rate, AP count. |
| `packet_loss/` | Packet reception | Known transmitter sending numbered frames (e.g. a second ESP32 beaconing with a counter in the SSID); compare sent vs received. Also watch `missing`/`ring_dropped` for pipeline loss, which is separate from RF loss. |

```bash
cd host && .venv/bin/python logger.py /dev/ttyACM0 --db ../experiments/rssi/1m.db
```
