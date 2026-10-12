import pathlib
import struct
import tempfile
import unittest

from tools.rltest.bind_story_sequential import layout_keys, expected_words, verify_layout, bind_words, bind_boot_symbols
from tools.rltest.seal_story_current_six import Elf
from tools.rltest.frontend import HarnessError

ROOT = pathlib.Path(__file__).resolve().parents[1]


def fixture(words, control_size, run_size):
    strings = bytearray(b"\0")
    symbols = [bytes(16)]
    payload = struct.pack(">" + "I" * len(words), *words) + struct.pack(">I", 0x80008000)
    for name, value, size, section in (
            ("gScCampaignObserverLayout", 0x80004000, len(words) * 4, 3),
            ("gScCampaignObserverControl", 0x80004000 + len(words) * 4, 4, 3),
            ("gScCampaignRun", 0x80009000, run_size, 4)):
        offset = len(strings)
        strings.extend(name.encode() + b"\0")
        symbols.append(struct.pack(">IIIBBH", offset, value, size, 0x11, 0, section))
    names = ["", ".strtab", ".symtab", ".rodata", ".bss", ".shstrtab"]
    shstr = b"\0".join(name.encode() for name in names) + b"\0"
    data = bytearray(84)
    sections = []
    for i, raw in enumerate((b"", bytes(strings), b"".join(symbols), payload, b"", shstr)):
        while len(data) % 4:
            data.append(0)
        offset = len(data)
        data.extend(raw)
        sections.append((sum(len(name) + 1 for name in names[:i]),
                         [0, 3, 2, 1, 8, 3][i], 2 if i in (3, 4) else 0,
                         0x80004000 if i == 3 else 0x80009000 if i == 4 else 0,
                         offset, run_size if i == 4 else len(raw),
                         1 if i == 2 else 0, 0, 4, 16 if i == 2 else 0))
    section_offset = len(data)
    for section in sections:
        data.extend(struct.pack(">10I", *section))
    struct.pack_into(">16sHHIIIIIHHHHHH", data, 0,
                     b"\x7fELF\x01\x02\x01" + bytes(9), 2, 8, 1, 0,
                     52, section_offset, 0, 52, 32, 1, 40, len(sections), 5)
    struct.pack_into(">8I", data, 52, 1, sections[3][4], 0x80004000, 0x1000,
                     len(payload), len(payload), 4, 4)
    rom = bytearray(b"\x80\x37\x12\x40" + bytes(0xFFC) + payload)
    return data, rom


class SequentialBindingTests(unittest.TestCase):
    def test_boot_memory_symbol_comes_from_exact_elf(self):
        class Symbols:
            memory = 0x80000318
            def symbol(self, name):
                return (0, self.memory if name == "osMemSize" else 0x80001000, 0)
        elf = Symbols()
        self.assertEqual(bind_boot_symbols(elf)["osMemSize"], 0x80000318)
        elf.memory += 4
        with self.assertRaises(HarnessError):
            bind_boot_symbols(elf)
    def test_native_os_prefix_shift_is_bound_not_guessed(self):
        host = {"control": {"size": 3768, "sequential_request": 588,
                            "sequential_ledger": 2812, "ui_pointer": 80},
                "ledger": {"party_ids": 27}}
        keys = [("control", key) for key in host["control"]] + [("ledger", "party_ids")]
        words = [4104, 924, 3148, 80, 27]
        native = bind_words(host, keys, words)
        self.assertEqual(native["control"]["size"], 4104)
        self.assertEqual(native["control"]["sequential_request"], 924)
        self.assertEqual(host["control"]["sequential_request"], 588)
        for index in (1, 3, 4):
            changed = words.copy(); changed[index] += 4
            with self.assertRaises(HarnessError):
                bind_words(host, keys, changed)
        for shift in (340, 4, -4, 0):
            with self.subTest(shift=shift):
                changed = expected_words(host, keys)
                for index, (group, key) in enumerate(keys):
                    if group == "control" and (key == "size" or key.startswith("sequential_")):
                        changed[index] += shift
                with self.assertRaises(HarnessError):
                    bind_words(host, keys, changed)

    def test_real_elf_parser_layout_rom_and_residency_refusals(self):
        abi = dict(pointer_size=4, control={"size": 3768}, run={"size": 40})
        keys = [("control", "size"), ("run", "size"), ("", "pointer_size")]
        words = expected_words(abi, keys)
        words[0] = 4104
        raw, rom = fixture(words, 4104, 40)
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "synthetic.elf"
            path.write_bytes(raw)
            elf = Elf(path)
            self.assertEqual(verify_layout(elf, rom, abi, keys)["control"], 0x80008000)
            changed = bytearray(rom); changed[-1] ^= 1
            with self.assertRaises(HarnessError):
                verify_layout(elf, changed, abi, keys)
            with self.assertRaises(HarnessError):
                verify_layout(elf, rom, {**abi, "run": {"size": 44}}, keys)
            changed = bytearray(raw)
            pointer = elf.symbol("gScCampaignObserverControl")[1]
            offset = elf.sections[3][4] + pointer - 0x80004000
            changed[offset:offset + 4] = (0x80400000).to_bytes(4, "big")
            path.write_bytes(changed)
            with self.assertRaises(HarnessError):
                verify_layout(Elf(path), rom, abi, keys)
            path.write_bytes(raw[:30])
            with self.assertRaises(HarnessError):
                Elf(path)

    def test_actual_witness_schema_and_unknown_directive_refusal(self):
        keys = layout_keys((ROOT / "src/story/sc_observer_layout.inc").read_text())
        self.assertIn(("control", "ui_pointer"), keys)
        self.assertIn(("ledger", "party_ids"), keys)
        self.assertIn(("", "pointer_size"), keys)
        with self.assertRaises(HarnessError):
            layout_keys("unrecognized witness syntax")


if __name__ == "__main__":
    unittest.main()
