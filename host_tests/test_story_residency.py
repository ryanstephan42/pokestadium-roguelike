import struct
import tempfile
import unittest
from pathlib import Path

from tools.rl.check_story_residency import check


def fixture(target):
    names = b"\0.shstrtab\0.text\0.fragmentSC\0.fragmentSC_bss\0.strtab\0.symtab\0.rel.text\0"
    symbols = bytes(16) + struct.pack(">IIIBBH", 1, target, 4, 0x12, 0, 3)
    descriptions = [
        ("", 0, 0, b"", 0, 0, 0),
        (".shstrtab", 3, 0, names, 0, 0, 0),
        (".text", 1, 0x80000400, bytes(32), 0, 0, 0),
        (".fragmentSC", 1, 0x8F900000, bytes(128), 0, 0, 0),
        (".fragmentSC_bss", 8, 0x8F900080, bytes(64), 0, 0, 0),
        (".strtab", 3, 0, b"\0probe\0", 0, 0, 0),
        (".symtab", 2, 0, symbols, 5, 0, 16),
        (".rel.text", 9, 0, struct.pack(">II", 0x80000400, 0x104), 6, 2, 8),
    ]
    data = bytearray(64 + 40 * len(descriptions))
    data[:6] = b"\x7fELF\x01\x02"
    struct.pack_into(">I", data, 32, 64)
    struct.pack_into(">HHH", data, 46, 40, len(descriptions), 1)
    for index, (name, kind, address, payload, link, info, size) in enumerate(descriptions):
        offset = len(data)
        data.extend(payload)
        struct.pack_into(">10I", data, 64 + index * 40, names.index(name.encode() + b"\0"),
                         kind, 0, address, offset, len(payload), link, info, 4, size)
    return data


class StoryResidencyTests(unittest.TestCase):
    def test_resident_import_and_cold_boundaries(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "probe.elf"
            path.write_bytes(fixture(0x80000410))
            self.assertEqual(check(path), 192)
            for address in (0x8F900000, 0x8F900080, 0x8F9000BF):
                path.write_bytes(fixture(address))
                with self.assertRaisesRegex(ValueError, "Resident references"):
                    check(path)
            path.write_bytes(fixture(0x8F9000C0))
            self.assertEqual(check(path), 192)

    def test_non_target_elf_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "probe.elf"
            path.write_bytes(b"\x7fELF\x01\x01")
            with self.assertRaisesRegex(ValueError, "big-endian ELF"):
                check(path)


if __name__ == "__main__":
    unittest.main()
