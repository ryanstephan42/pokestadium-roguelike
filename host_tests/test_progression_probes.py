"""Host checks for tracked native progression probe framing and provenance."""

import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
import zlib

from tools.rltest.test_permanent_progression import committed_currency, restore_checkpoint
from tools.rltest.test_save_migration import as_v3, read_note
from tools.rltest.frontend import HarnessError


def pak_image():
    data = bytearray(32768)
    header = bytearray(32)
    header[24:28] = b"\x00\x01\x01\x00"
    words = struct.unpack(">14H", header[:28])
    struct.pack_into(">HH", header, 28, sum(words) & 65535,
                     sum((~word) & 65535 for word in words) & 65535)
    for block in (1, 3, 4, 6):
        data[block * 32:(block + 1) * 32] = header
    data[768:774] = b"RL01RL"
    data[774:776] = b"\x00\x05"
    for page in range(5, 15):
        struct.pack_into(">H", data, 256 + page * 2, 1 if page == 14 else page + 1)
    return data


def commit(data, slot, sequence, currency, suspend):
    payload = bytearray(972 if suspend else 416)
    payload[:8] = b"RLSV\x04\x00" + bytes((suspend, 0))
    struct.pack_into("<I", payload, 357, currency)
    if suspend:
        payload[412:-4] = bytes(i % 256 for i in range(556))
    struct.pack_into("<I", payload, len(payload) - 4, zlib.crc32(payload[:-4]))
    header = bytearray(32)
    header[:4] = b"RLPK"
    struct.pack_into("<IIII", header, 4, 1, sequence, len(payload), zlib.crc32(payload))
    struct.pack_into("<I", header, 28, zlib.crc32(header[:28]))
    data[1280 + slot * 1280:1280 + slot * 1280 + 32] = header
    data[1312 + slot * 1280:1312 + slot * 1280 + len(payload)] = payload


class TestProgressionProbes(unittest.TestCase):
    def test_v3_conversion_preserves_suspend_and_owned_bytes(self):
        data = pak_image()
        commit(data, 0, 1, 45, 0)
        commit(data, 1, 2, 50, 1)
        migrated = as_v3(data)
        self.assertEqual(data[:1280], migrated[:1280])
        self.assertEqual(data[3840:], migrated[3840:])
        original = read_note(data)
        note = read_note(migrated)
        for slot, length in ((0, 361), (1280, 917)):
            header = note[slot:slot + 32]
            self.assertEqual(struct.unpack_from("<I", header, 12)[0], length)
            payload = note[slot + 32:slot + 32 + length]
            self.assertEqual(payload[:8], b"RLSV\x03\x00" + original[slot + 38:slot + 40])
            self.assertEqual(zlib.crc32(header[:28]), struct.unpack_from("<I", header, 28)[0])
            self.assertEqual(zlib.crc32(payload), struct.unpack_from("<I", header, 16)[0])
            self.assertEqual(zlib.crc32(payload[:-4]), struct.unpack_from("<I", payload, length - 4)[0])
            if slot:
                self.assertEqual(payload[357:-4], original[slot + 32 + 412:slot + 32 + 968])

    def test_reads_actual_latest_committed_debit(self):
        data = pak_image()
        commit(data, 0, 1, 100, 0)
        commit(data, 1, 2, 70, 1)
        with patch("tools.rltest.test_permanent_progression.save_ram", return_value=(None, data)):
            self.assertEqual(committed_currency(None), 70)
        data[1312 + 1280 + 357] ^= 1
        with patch("tools.rltest.test_permanent_progression.save_ram", return_value=(None, data)):
            with self.assertRaises(HarnessError):
                committed_currency(None)

    def test_rejects_checkpoint_from_different_rom(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "fixture.bin"
            path.write_bytes(b"fixture")
            path.with_suffix(".json").write_text(json.dumps({
                "head": "fixture", "codec_version": 4,
                "save_sha256": hashlib.sha256(b"fixture").hexdigest(),
                "rom_sha256": "not-current-rom",
            }))
            original_read = Path.read_bytes
            def fixture_read(candidate):
                if candidate == Path("build_rl/pokestadium-rl.z64"):
                    return b"mock current ROM"
                return original_read(candidate)
            with patch.object(Path, "read_bytes", fixture_read):
                with self.assertRaises(HarnessError):
                    restore_checkpoint(None, path)


if __name__ == "__main__":
    unittest.main()
