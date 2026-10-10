"""Exact vanilla function symbol selection, including IDO NOTYPE symbols."""
import struct
import tempfile
import unittest
from pathlib import Path

from tools.rl.gen_archive_imports import functions


def elf(rows):
    strings = bytearray(b"\0")
    symbols = bytearray()
    for name, address, info, section in rows:
        offset = len(strings)
        strings.extend(name.encode("ascii") + b"\0")
        symbols.extend(struct.pack(">IIIBBH", offset, address, 0, info, 0, section))
    data = bytearray(0x100)
    data[:6] = b"\x7fELF\x01\x02"
    struct.pack_into(">I", data, 0x20, 0x40)
    struct.pack_into(">HH", data, 0x2E, 40, 3)
    struct.pack_into(">10I", data, 0x68, 0, 2, 0, 0, 0x100, len(symbols), 2, 0, 4, 16)
    struct.pack_into(">10I", data, 0x90, 0, 3, 0, 0, 0x100 + len(symbols),
                     len(strings), 0, 0, 1, 0)
    return data + symbols + strings


class GeneratorTests(unittest.TestCase):
    def read(self, rows):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "vanilla.elf"
            path.write_bytes(elf(rows))
            return functions(path)

    def test_exact_function_selection(self):
        self.assertEqual(self.read([
            ("func_8434E6AC", 0x8434E6AC, 0x12, 1),
            ("func_8434D890", 0x8434D890, 0x10, 1),
            ("func_object", 0x84340000, 0x11, 1),
            ("func_undefined", 0x84341000, 0x12, 0),
            ("func_local", 0x84342000, 0x02, 1),
            ("func_other_fragment", 0x84240000, 0x12, 1),
            ("D_84343000", 0x84343000, 0x10, 1),
        ]), [(0x8434D890, "func_8434D890"), (0x8434E6AC, "func_8434E6AC")])

    def test_ambiguous_alias_rejected(self):
        with self.assertRaisesRegex(ValueError, "Ambiguous"):
            self.read([("func_a", 0x8434D890, 0x12, 1),
                       ("func_b", 0x8434D890, 0x12, 1)])

    def test_missing_functions_rejected(self):
        with self.assertRaisesRegex(ValueError, "No global"):
            self.read([])


if __name__ == "__main__":
    unittest.main()
