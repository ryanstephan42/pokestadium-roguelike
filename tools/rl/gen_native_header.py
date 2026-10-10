#!/usr/bin/env python3
"""Rebuild a native overlay header using linker-computed sizes."""

import argparse
from pathlib import Path
import struct
import re


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fragment", type=int)
    parser.add_argument("original")
    parser.add_argument("output")
    parser.add_argument("--vanilla-map", required=True)
    args = parser.parse_args()
    header = Path(args.original).read_bytes()
    if len(header) != 32 or header[8:16] != b"FRAGMENT":
        raise ValueError("not a native fragment header")
    instruction = struct.unpack_from(">I", header)[0]
    if instruction >> 26 != 2 or header[4:8] != bytes(4):
        raise ValueError("expected jump/nop native header")
    target = 0x80000000 | ((instruction & 0x03FFFFFF) << 2)
    entry = None
    for line in Path(args.vanilla_map).read_text().splitlines():
        match = re.fullmatch(r"\s+0x([0-9a-f]+)\s+(func_[A-Za-z0-9_]+)", line)
        if match and int(match[1], 16) == target:
            entry = match[2]
            break
    if entry is None:
        raise ValueError(f"native entry symbol not found at {target:08X}")
    name = f"fragment{args.fragment}"
    Path(args.output).write_text(
        '.section .text, "ax"\n'
        f"j {entry}\nnop\n.ascii \"FRAGMENT\"\n.word 0x20\n"
        f".word {name}_HDR_RELOC_OFFSET\n.word {name}_HDR_SIZE_IN_ROM\n"
        f".word {name}_HDR_SIZE_IN_RAM\n")


if __name__ == "__main__":
    main()
