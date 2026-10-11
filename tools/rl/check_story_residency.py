#!/usr/bin/env python3
"""Reject resident ELF imports into the unloadable story address window."""

import argparse
import struct
from pathlib import Path


def check(path):
    data = Path(path).read_bytes()
    if data[:6] != b"\x7fELF\x01\x02":
        raise ValueError("Expected 32-bit big-endian ELF")
    shoff = struct.unpack_from(">I", data, 32)[0]
    shsize, count, names = struct.unpack_from(">HHH", data, 46)
    sections = [struct.unpack_from(">10I", data, shoff + shsize * i) for i in range(count)]
    names_offset = sections[names][4]

    def name(index):
        start = names_offset + sections[index][0]
        return data[start:data.index(b"\0", start)].decode("ascii")

    cold = next(section for i, section in enumerate(sections) if name(i) == ".fragmentSC")
    cold_bss = next(section for i, section in enumerate(sections) if name(i) == ".fragmentSC_bss")
    low, high = cold[3], cold_bss[3] + cold_bss[5]
    imports = []
    for section in sections:
        if section[1] != 9 or name(section[7]) not in (".text", ".heap_bss"):
            continue
        symbols = sections[section[6]]
        strings = sections[symbols[6]]
        for offset in range(section[4], section[4] + section[5], 8):
            address, info = struct.unpack_from(">II", data, offset)
            symbol = struct.unpack_from(">IIIBBH", data, symbols[4] + (info >> 8) * symbols[9])
            if low <= symbol[1] < high:
                start = strings[4] + symbol[0]
                imports.append((address, data[start:data.index(b"\0", start)].decode("ascii")))
    if imports:
        raise ValueError(f"Resident references to unloadable story storage/code: {imports}")
    return high - low


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf")
    size = check(parser.parse_args().elf)
    print(f"Story resident imports: OK; cold RAM image {size} bytes (not runtime proof)")
