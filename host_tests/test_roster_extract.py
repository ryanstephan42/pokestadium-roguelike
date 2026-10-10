import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location(
    "rosters", Path(__file__).resolve().parents[1] / "tools/rl_extract_rosters.py")
rosters = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(rosters)


class RosterTests(unittest.TestCase):
    def mon(self):
        data = bytearray(0x54)
        data[0] = 25
        data[0x24] = 50
        data[9] = 85
        data[0x20] = 0xCF
        data[0x1E:0x20] = bytes.fromhex("1234")
        return data

    def test_mon_fields(self):
        mon = rosters.parse_mon(self.mon())
        self.assertEqual(mon["species"], 25)
        self.assertEqual(mon["level"], 50)
        self.assertEqual(mon["moves"], [85, 0, 0, 0])
        self.assertEqual(mon["pp"], [15, 0, 0, 0])
        self.assertEqual(mon["pp_ups"], [3, 0, 0, 0])
        self.assertEqual(mon["dvs"], 0x1234)

    def test_trainer_stride(self):
        data = bytearray(4 + 2 * 0x230)
        data[:4] = struct.pack(">I", 2)
        for index in range(2):
            base = 4 + index * 0x230
            data[base + 0x34:base + 0x38] = struct.pack(">HH", 0x1240, 1)
            data[base + 0x38:base + 0x8C] = self.mon()
        trainers = rosters.parse_trainer_file(data)
        self.assertEqual(len(trainers), 2)
        self.assertEqual(trainers[1]["portrait"], 0x12)
        self.assertEqual(trainers[1]["ai"], 64)

    def test_bad_counts_rejected(self):
        with self.assertRaises(ValueError):
            rosters.parse_rental_file(struct.pack(">I", 152))
        with self.assertRaises(ValueError):
            rosters.parse_trainer_file(struct.pack(">I", 3))

    def test_yay0_overlap(self):
        # Literal A followed by three copies at distance one.
        data = b"Yay0" + struct.pack(">III", 4, 20, 22)
        data += struct.pack(">IH", 0x80000000, 0x1000) + b"A"
        self.assertEqual(rosters.yay0_decompress(data), b"AAAA")

    def test_pers_reloc_then_zero_fill(self):
        compressed = b"Yay0" + struct.pack(">III", 4, 20, 20)
        compressed += struct.pack(">I", 0xF0000000) + b"ABCD"
        header = b"PERS-SZP" + struct.pack(">IIII", 40, 4, 8, 2)
        header += struct.pack(">IIII", 0x12345678, 0, 0xFFFFFFFF, 4)
        self.assertEqual(rosters.unpack_file(header + compressed), bytes.fromhex("1234567800000000"))

    def test_generated_ids_unique(self):
        rom_path = Path(__file__).resolve().parents[1] / "baseroms/us/baserom.z64"
        if not rom_path.exists():
            self.skipTest("local baserom not installed")
        data = rosters.extract(rom_path.read_bytes())
        for kind in ("trainer_sets", "rental_sets"):
            ids = [entry["id"] for entry in data[kind]]
            self.assertEqual(len(ids), len(set(ids)))
        self.assertEqual(len(data["trainer_sets"]), 48)
        self.assertEqual(len(data["rental_sets"]), 14)
        with tempfile.TemporaryDirectory() as directory:
            rosters.write_c(data, Path(directory) / "rl_rosters.h")


if __name__ == "__main__":
    unittest.main()
