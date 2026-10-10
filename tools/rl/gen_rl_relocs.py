#!/usr/bin/env python3
"""Emit loader relocation tables for roguelike or native fragments.

Reads the linked ELF (linked with --emit-relocs), collects every REL entry that
applies to the `.fragmentRL` output section and writes an assembly file with the
table in the format consumed by Memmap_RelocateFragment:

    .word nRelocations
    .word (type << 24) | offset_from_fragment_start   ; repeated

The loader only rewrites values that point into 0x81000000-0x8FFFFFFF, so
relocations against resident (0x80xxxxxx) or absolute symbols are harmless and
are kept in ELF order (the HI16/LO16 pairing depends on that order).

Usage: gen_rl_relocs.py <elf> <out.s>   (pass "-" as elf for an empty table)
"""

import struct
import sys
import argparse
import bisect
import re

SUPPORTED = {2, 4, 5, 6}  # R_MIPS_32, R_MIPS_26, R_MIPS_HI16, R_MIPS_LO16
FRAG_SECTION = b".fragmentRL"


def read_relocs(elf_path, section=FRAG_SECTION, original=None, anchors=None, map_path=None, patch=False):
    with open(elf_path, "rb") as f:
        data = f.read()
    if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 2:
        sys.exit("gen_rl_relocs: expected 32-bit big-endian ELF")
    e_shoff, = struct.unpack_from(">I", data, 0x20)
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from(">HHH", data, 0x2E)

    shdrs = []
    for i in range(e_shnum):
        shdrs.append(struct.unpack_from(">IIIIIIIIII", data, e_shoff + i * e_shentsize))
    strtab_off = shdrs[e_shstrndx][4]

    def name(sh):
        start = strtab_off + sh[0]
        return data[start:data.index(b"\0", start)]

    target_idx = None
    target_addr = None
    for i, sh in enumerate(shdrs):
        if name(sh) == section:
            target_idx, target_addr = i, sh[3]
    if target_idx is None:
        sys.exit(f"gen_rl_relocs: {section.decode()} section not found")

    out = []
    for sh in shdrs:
        sh_type, sh_offset, sh_size, sh_info = sh[1], sh[4], sh[5], sh[7]
        if sh_type == 9 and sh_info == target_idx:  # SHT_REL
            for off in range(sh_offset, sh_offset + sh_size, 8):
                r_offset, r_info = struct.unpack_from(">II", data, off)
                rtype = r_info & 0xFF
                if rtype == 0:
                    continue
                if rtype not in SUPPORTED:
                    sys.exit(f"gen_rl_relocs: unsupported reloc type {rtype} at 0x{r_offset:08X}")
                frag_off = r_offset - target_addr
                if not 0 <= frag_off < 0x1000000:
                    sys.exit(f"gen_rl_relocs: reloc outside fragment at 0x{r_offset:08X}")
                out.append((rtype << 24) | frag_off)
        elif sh_type == 4 and sh_info == target_idx:  # SHT_RELA
            sys.exit("gen_rl_relocs: RELA relocations are not supported")
    if original is not None:
        if not anchors or not map_path:
            sys.exit("gen_rl_relocs: native tables require --anchors and --map")
        mappings = []
        symbols = {}
        with open(map_path) as f:
            for line in f:
                match = re.match(r"^\s+0x([0-9a-f]+)\s+(__rla_\d+) = ", line)
                if match:
                    symbols[match[2]] = int(match[1], 16)
        with open(anchors) as f:
            for line in f:
                symbol, addr, size, *_ = line.split()
                mappings.append((int(addr, 16), int(size, 16), symbols[symbol]))
        mappings.sort()
        starts = [item[0] for item in mappings]

        def remap(addr):
            index = bisect.bisect_right(starts, addr) - 1
            if index >= 0:
                old, size, new = mappings[index]
                if old <= addr < old + size:
                    return new + addr - old
            return addr

        with open(original, "rb") as f:
            raw = f.read()
        count, = struct.unpack_from(">I", raw)
        if 4 + count * 4 > len(raw):
            sys.exit("gen_rl_relocs: truncated original relocation table")
        # Original data can contain raw pointer words absent from ELF REL sections.
        # Keep those entries, moving their locations and values with input sections.
        known = set(out)
        image = bytearray(data)
        for word in struct.unpack_from(f">{count}I", raw, 4):
            word &= 0x7FFFFFFF  # Native tables also carry a loader-ignored flag.
            kind, offset = word >> 24, word & 0xFFFFFF
            if kind not in SUPPORTED:
                sys.exit(f"gen_rl_relocs: unsupported original relocation {word:08X}")
            if kind != 2 and offset != 0:
                continue
            location = remap(target_addr + offset)
            entry = (kind << 24) | (location - target_addr)
            if entry in known:
                continue
            out.append(entry)
            known.add(entry)
            if patch:
                file_offset = shdrs[target_idx][4] + location - target_addr
                value, = struct.unpack_from(">I", image, file_offset)
                if kind == 4:
                    target = remap(0x80000000 | ((value & 0x03FFFFFF) << 2))
                    value = (value & 0xFC000000) | ((target & 0x0FFFFFFF) >> 2)
                else:
                    value = remap(value)
                struct.pack_into(">I", image, file_offset, value)
        if patch:
            with open(elf_path, "wb") as f:
                f.write(image)
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf")
    parser.add_argument("out")
    parser.add_argument("--section", default=".fragmentRL")
    parser.add_argument("--original")
    parser.add_argument("--anchors")
    parser.add_argument("--map")
    parser.add_argument("--patch-data", action="store_true")
    args = parser.parse_args()
    relocs = [] if args.elf == "-" else read_relocs(
        args.elf, args.section.encode(), args.original, args.anchors, args.map, args.patch_data)
    with open(args.out, "w") as f:
        f.write("/* generated by tools/rl/gen_rl_relocs.py */\n")
        f.write('.section .rodata, "a"\n')
        f.write(f".word {len(relocs)}\n")
        for r in relocs:
            f.write(f".word 0x{r:08X}\n")
        f.write(".balign 16, 0\n")
    print(f"gen_rl_relocs: {len(relocs)} relocations")


if __name__ == "__main__":
    main()
