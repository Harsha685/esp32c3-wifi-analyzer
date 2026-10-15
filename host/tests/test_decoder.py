import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import decoder as d  # noqa: E402


def sys_payload(**kw):
    v = dict(ts=1, heap=2, minheap=3, cap=256, hw=4, rdrop=5, tdrop=6, ch=7, host=1, dwell=200)
    v.update(kw)
    return struct.pack("<IIIHHIIBBH", v["ts"], v["heap"], v["minheap"], v["cap"], v["hw"],
                       v["rdrop"], v["tdrop"], v["ch"], v["host"], v["dwell"])


class CrcTest(unittest.TestCase):
    def test_check_value(self):
        self.assertEqual(d.crc16(b"123456789"), 0x29B1)


class DecoderTest(unittest.TestCase):
    def test_roundtrip(self):
        msgs = Decoder = d.Decoder().feed(d.encode(d.SYSTEM_STATUS, 0, sys_payload()))
        self.assertEqual(len(msgs), 1)
        self.assertEqual(msgs[0].name, "SYSTEM_STATUS")
        self.assertEqual(msgs[0].data["current_channel"], 7)
        self.assertEqual(msgs[0].data["ring_capacity"], 256)

    def test_byte_at_a_time(self):
        dec = d.Decoder()
        out = []
        for b in d.encode(d.SYSTEM_STATUS, 0, sys_payload()) * 3:
            out += dec.feed(bytes([b]))
        self.assertEqual(len(out), 3)

    def test_resync_after_garbage_and_boot_log(self):
        dec = d.Decoder()
        stream = b"ESP-ROM:esp32c3\r\nI (24) boot: junk \xaa\xaa\x55" + d.encode(d.SYSTEM_STATUS, 0, sys_payload())
        out = dec.feed(stream)
        self.assertEqual(len(out), 1)
        self.assertGreater(dec.stats.resync_bytes, 0)

    def test_corrupt_crc_dropped_and_recovers(self):
        good = d.encode(d.SYSTEM_STATUS, 1, sys_payload())
        bad = bytearray(d.encode(d.SYSTEM_STATUS, 0, sys_payload()))
        bad[12] ^= 0xFF
        dec = d.Decoder()
        out = dec.feed(bytes(bad) + good)
        self.assertEqual([m.seq for m in out], [1])
        self.assertEqual(dec.stats.crc_errors, 1)

    def test_truncated_frame_then_valid(self):
        f = d.encode(d.SYSTEM_STATUS, 0, sys_payload())
        dec = d.Decoder()
        out = dec.feed(f[:20] + d.encode(d.SYSTEM_STATUS, 1, sys_payload()))
        self.assertEqual([m.seq for m in out], [1])

    def test_sequence_gap_and_reset(self):
        dec = d.Decoder()
        for s in (0, 1, 5, 6):
            dec.feed(d.encode(d.SYSTEM_STATUS, s, sys_payload()))
        self.assertEqual(dec.stats.missing, 3)
        dec.feed(d.encode(d.SYSTEM_STATUS, 0, sys_payload()))   # device rebooted
        self.assertEqual(dec.stats.device_resets, 1)
        self.assertEqual(dec.stats.missing, 3)

    def test_sequence_wraparound(self):
        dec = d.Decoder()
        dec.feed(d.encode(d.SYSTEM_STATUS, 0xFFFFFFFF, sys_payload()))
        dec.feed(d.encode(d.SYSTEM_STATUS, 0, sys_payload()))
        self.assertEqual(dec.stats.missing, 0)
        self.assertEqual(dec.stats.device_resets, 0)

    def test_oversize_length_rejected(self):
        dec = d.Decoder()
        evil = d.MAGIC + struct.pack("<BHI", 1, 60000, 0) + b"x" * 20
        out = dec.feed(evil + d.encode(d.SYSTEM_STATUS, 0, sys_payload()))
        self.assertEqual(len(out), 1)

    def test_wrong_payload_size_counted(self):
        dec = d.Decoder()
        out = dec.feed(d.encode(d.PACKET_STATS, 0, b"\x00" * 5))
        self.assertEqual(out, [])
        self.assertEqual(dec.stats.bad_payloads, 1)

    def test_ap_info(self):
        p = struct.pack("<6sBbbbbBIIIB32s", bytes.fromhex("aabbcc001122"), 6, -40, -42, -50, -35, 4,
                        10, 20, 30, 7, b"TestNet".ljust(32, b"\0"))
        m = d.Decoder().feed(d.encode(d.AP_INFO, 0, p))[0]
        self.assertEqual(m.data["bssid"], "aa:bb:cc:00:11:22")
        self.assertEqual(m.data["ssid"], "TestNet")
        self.assertEqual(m.data["security"], "WPA2+")
        self.assertEqual(m.data["rssi_min"], -50)


class GoldenTest(unittest.TestCase):
    """Frame produced by the C encoder (firmware/test_native) decodes in Python."""
    path = os.path.join(os.path.dirname(__file__), "..", "..", "firmware", "test_native", "golden_frame.bin")

    @unittest.skipUnless(os.path.exists(path), "run `make -C firmware/test_native` first")
    def test_c_encoder_matches_python(self):
        with open(self.path, "rb") as f:
            raw = f.read()
        self.assertEqual(raw, d.encode(d.SYSTEM_STATUS, 0x01020304, bytes([1, 2, 3])))


if __name__ == "__main__":
    unittest.main()
