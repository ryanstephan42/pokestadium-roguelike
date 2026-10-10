"""Verify native fragment62 relocation parity against the matching build."""

import struct
import tempfile
import unittest
from pathlib import Path

from tools.rl.gen_rl_ld import parse_map
from tools.rl.gen_rl_relocs import read_relocs


class NativeRelocsTest(unittest.TestCase):
    def test_unmodified_fragment62(self):
        elf = Path("build/pokestadium-us.elf")
        original = Path("assets/us/fragments/62/fragment62_reloc.rodatabin.bin")
        self.assertTrue(elf.is_file(), "build vanilla before this test")
        sections = parse_map("build/pokestadium-us.map", 0x84300000, 0x84400000)
        with tempfile.TemporaryDirectory() as directory:
            anchors = Path(directory) / "anchors.txt"
            map_path = Path(directory) / "identity.map"
            entries = []
            symbols = []
            for index, ((obj, section), (address, size)) in enumerate(sections.items()):
                name = f"__rla_{index}"
                entries.append(f"{name} 0x{address:08x} 0x{size:x} {obj} {section}\n")
                symbols.append(f" 0x{address:08x} {name} = ALIGN(16);\n")
            anchors.write_text("".join(entries))
            map_path.write_text("".join(symbols))
            regenerated = read_relocs(str(elf), b".fragment62", str(original),
                                      str(anchors), str(map_path))
        data = original.read_bytes()
        count, = struct.unpack_from(">I", data)
        expected = [word & 0x7FFFFFFF for word in struct.unpack_from(f">{count}I", data, 4)]
        self.assertEqual(len(regenerated), count)
        self.assertEqual(set(regenerated), set(expected))


if __name__ == "__main__":
    unittest.main()
