#!/usr/bin/env python3
"""Recompute the N64 ROM header CRCs (offsets 0x10/0x14) in place.

Usage: n64crc.py <rom.z64> [--check]

The CIC variant is detected from the IPL3 boot code checksum; Pokémon Stadium
uses CIC-NUS-6102/7101.
"""

import struct
import sys
import zlib

CIC_SEEDS = {6101: 0xF8CA4DDC, 6102: 0xF8CA4DDC, 6103: 0xA3886759, 6105: 0xDF26F436, 6106: 0x1FEA617A}
CIC_BY_IPL3_CRC = {
    0x6170A4A1: 6101, 0x90BB6CB5: 6102, 0x0B050EE0: 6103, 0x98BC2C86: 6105, 0xACC8580A: 6106,
}
M = 0xFFFFFFFF


def rol(v, n):
    n &= 31
    return ((v << n) | (v >> (32 - n))) & M


def calc(rom, cic):
    seed = CIC_SEEDS[cic]
    t1 = t2 = t3 = t4 = t5 = t6 = seed
    start, length = 0x1000, 0x100000
    words = struct.unpack_from(">%dI" % (length // 4), rom.ljust(start + length, b"\0"), start)
    if cic == 6105:
        ipl = struct.unpack_from(">%dI" % (0x100 // 4), rom, 0x750)
    for i, d in enumerate(words):
        if (t6 + d) & M < t6:
            t4 = (t4 + 1) & M
        t6 = (t6 + d) & M
        t3 ^= d
        r = rol(d, d & 0x1F)
        t5 = (t5 + r) & M
        if t2 > d:
            t2 ^= r
        else:
            t2 ^= t6 ^ d
        if cic == 6105:
            t1 = (t1 + (ipl[i & 0x3F] ^ d)) & M
        else:
            t1 = (t1 + (t5 ^ d)) & M
    if cic == 6103:
        return (t6 ^ t4) + t3 & M, (t5 ^ t2) + t1 & M
    if cic == 6106:
        return (t6 * t4 + t3) & M, (t5 * t2 + t1) & M
    return t6 ^ t4 ^ t3, t5 ^ t2 ^ t1


def detect_cic(rom):
    return CIC_BY_IPL3_CRC.get(zlib.crc32(rom[0x40:0x1000]) & M, 6102)


def main():
    path = sys.argv[1]
    check = "--check" in sys.argv[2:]
    with open(path, "rb") as f:
        rom = bytearray(f.read())
    cic = detect_cic(rom)
    c1, c2 = calc(bytes(rom), cic)
    old = struct.unpack_from(">II", rom, 0x10)
    if check:
        ok = old == (c1, c2)
        print(f"n64crc: CIC {cic} header {old[0]:08X} {old[1]:08X} computed {c1:08X} {c2:08X} -> {'OK' if ok else 'MISMATCH'}")
        sys.exit(0 if ok else 1)
    struct.pack_into(">II", rom, 0x10, c1, c2)
    with open(path, "wb") as f:
        f.write(rom)
    print(f"n64crc: CIC {cic} CRC1={c1:08X} CRC2={c2:08X}")


if __name__ == "__main__":
    main()
