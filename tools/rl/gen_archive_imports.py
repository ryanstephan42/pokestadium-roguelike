#!/usr/bin/env python3
"""Generate exact old-function imports for the native battle animation archive."""

import argparse
import struct
from pathlib import Path


def functions(path):
    data = Path(path).read_bytes()
    if data[:6] != b"\x7fELF\x01\x02":
        raise ValueError("Expected a 32-bit big-endian vanilla ELF")
    offset = struct.unpack_from(">I", data, 0x20)[0]
    stride, count = struct.unpack_from(">HH", data, 0x2E)
    sections = [struct.unpack_from(">10I", data, offset + i * stride)
                for i in range(count)]
    found = {}
    for section in sections:
        if section[1] != 2:
            continue
        strings = sections[section[6]]
        for pos in range(section[4], section[4] + section[5], section[9]):
            name, value, _, info, _, index = struct.unpack_from(">IIIBBH", data, pos)
            start = strings[4] + name
            symbol = data[start:data.index(b"\0", start)].decode("ascii")
            if (index and info >> 4 == 1 and (info & 15) in (0, 2) and symbol.startswith("func_")
                    and 0x84300020 <= value < 0x84384350):
                if value in found and found[value] != symbol:
                    raise ValueError(f"Ambiguous function at {value:#x}")
                found[value] = symbol
    if not found:
        raise ValueError("No global fragment62 functions in vanilla ELF")
    return sorted(found.items())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf")
    parser.add_argument("output")
    parser.add_argument("--regen-fragments", nargs="*", type=int, default=[62])
    args = parser.parse_args()
    rows = functions(args.elf) if 62 in args.regen_fragments else []
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    lines = [".section .rodata", ".balign 4", ".globl rl_archive_imports",
             "rl_archive_imports:"]
    lines += [f"    .word 0x{address:08X}, {symbol}" for address, symbol in rows]
    lines += [".globl rl_archive_import_count", "rl_archive_import_count:",
              f"    .word {len(rows)}"]
    output.write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
