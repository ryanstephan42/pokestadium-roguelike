#!/usr/bin/env python3
"""Fail-closed check of raw jump thunks in a regenerated native fragment.

Raw thunk tables (e.g. fragment31_unk_table) are textbin blobs: their j/jal words carry no ELF
relocation, so after a fragment's functions move they must be rebound by gen_rl_relocs.py. For
every R_MIPS_26 entry of the vanilla table whose location is inside the fragment's blob range,
check in the linked ELF that:
  * the loader table keeps the entry at the same location;
  * a same-fragment target is rebound to the exact linked address of the vanilla function symbol;
  * a fragment62 target keeps its original address, which the load-time archive import repair
    (src/roguelike/rl_native_imports.c) maps exactly once;
  * any other foreign target is a function whose address did not move;
  * fragment31 only (archive fragment id 0, the one rl_relocate_archive_imports rewrites): no other
    R_MIPS_26 entry targets fragment62, which the import repair would map a second time.
Also checks the loader table is the ELF relocations plus those raw entries, the header entry and
original raw R_MIPS_32 data entries.

Usage: check_native_thunks.py <elf> <map> <vanilla.elf> <vanilla.map> <fragment> <reloc.bin> [--report]
"""

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gen_rl_relocs import map_functions, read_relocs  # noqa: E402

IMPORT_LO, IMPORT_HI = 0x84300000, 0x84400000


def sections(path):
    data = Path(path).read_bytes()
    shoff, = struct.unpack_from(">I", data, 0x20)
    size, count, strndx = struct.unpack_from(">HHH", data, 0x2E)
    headers = [struct.unpack_from(">10I", data, shoff + i * size) for i in range(count)]
    strtab = headers[strndx][4]
    out = {}
    for h in headers:
        name = data[strtab + h[0]:data.index(b"\0", strtab + h[0])].decode()
        out[name] = (h[3], data[h[4]:h[4] + h[5]] if h[1] != 8 else bytes(h[5]))
    return out


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    report = "--report" in sys.argv
    elf, map_path, vanilla_elf, vanilla_map, number, original = args
    name = f".fragment{number}"
    new_secs, old_secs = sections(elf), sections(vanilla_elf)
    base, image = new_secs[name]
    old_base, old_image = old_secs[name]
    if base != old_base:
        sys.exit(f"check_native_thunks: {name} VRAM moved")
    table = new_secs[name + "_relocs"][1]
    count, = struct.unpack_from(">I", table)
    entries = [w & 0x7FFFFFFF for w in struct.unpack_from(f">{count}I", table, 4)]

    raw = Path(original).read_bytes()
    n, = struct.unpack_from(">I", raw)
    vanilla = [w & 0x7FFFFFFF for w in struct.unpack_from(f">{n}I", raw, 4)]
    old_funcs = {a: s for s, a in map_functions(vanilla_map).items()}
    new_funcs = map_functions(map_path)
    elf_relocs = set(read_relocs(elf, name.encode()))
    covered = {e & 0xFFFFFF for e in elf_relocs}

    blobs = []
    for line in Path(map_path).read_text().splitlines():
        parts = line.split()
        if (len(parts) == 4 and parts[0] in (".text", ".data", ".rodata") and "/asm/" in parts[3]
                and parts[1].startswith("0x")):
            start, size = int(parts[1], 16), int(parts[2], 16)
            if base <= start < base + len(image) and size:
                blobs.append((start - base, size))

    raw_entries, failures, rows = set(), [], []
    for word in vanilla:
        kind, offset = word >> 24, word & 0xFFFFFF
        if kind != 4 or offset == 0 or not any(o <= offset < o + z for o, z in blobs):
            continue
        if offset in covered:
            failures.append(f"{base + offset:08X}: raw jump also has an ELF relocation")
        old_word, = struct.unpack_from(">I", old_image, offset)
        new_word, = struct.unpack_from(">I", image, offset)
        old_target = 0x80000000 | ((old_word & 0x03FFFFFF) << 2)
        new_target = 0x80000000 | ((new_word & 0x03FFFFFF) << 2)
        symbol = old_funcs.get(old_target)
        raw_entries.add(word)
        if (old_word ^ new_word) & 0xFC000000:
            failures.append(f"{base + offset:08X}: opcode changed {old_word:08X} -> {new_word:08X}")
            continue
        if symbol is None:
            failures.append(f"{base + offset:08X}: vanilla target {old_target:08X} is not a function start")
            continue
        if base <= old_target < base + len(old_image):
            want, how = new_funcs.get(symbol), "local"
        elif IMPORT_LO <= old_target < IMPORT_HI:
            want, how = old_target, "import"
        else:
            want, how = old_target, "foreign"
            if new_funcs.get(symbol) != old_target:
                failures.append(f"{base + offset:08X}: foreign {symbol} moved without a load-time repair")
        if new_target != want:
            failures.append(f"{base + offset:08X}: {symbol} -> {new_target:08X}, expected {want or 0:08X}")
        if word not in entries:
            failures.append(f"{base + offset:08X}: loader table lost the raw R_MIPS_26 entry")
        rows.append(f"{base + offset:08X} {how:7} {symbol} vanilla {old_target:08X} linked "
                    f"{new_funcs.get(symbol, 0):08X} word-target {new_target:08X}")
    expected = elf_relocs | raw_entries
    header = [w for w in vanilla if w & 0xFFFFFF == 0]
    expected |= set(header)
    extra = set(entries) - expected
    if (not expected <= set(entries) or len(entries) != len(set(entries))
            or any(w >> 24 != 2 for w in extra)):
        failures.append(f"loader table {len(entries)} entries is not ELF {len(elf_relocs)} + raw "
                        f"{len(raw_entries)} + header {len(header)} + original R_MIPS_32 data")
    imports = {r.split()[0] for r in rows if r.split()[1] == "import"}
    for word in entries if number == "31" else ():
        if word >> 24 == 4:
            value, = struct.unpack_from(">I", image, word & 0xFFFFFF)
            target = 0x80000000 | ((value & 0x03FFFFFF) << 2)
            if IMPORT_LO <= target < IMPORT_HI and f"{base + (word & 0xFFFFFF):08X}" not in imports:
                failures.append(f"{base + (word & 0xFFFFFF):08X}: R_MIPS_26 into fragment62 outside "
                                "the raw import thunks would be remapped twice")
    if report:
        print("\n".join(rows))
    if failures:
        sys.exit("check_native_thunks: " + "\n  ".join(failures))
    kinds = {h: sum(r.split()[1] == h for r in rows) for h in ("local", "import", "foreign")}
    print(f"check_native_thunks: {name} {len(rows)} raw thunks bound "
          f"(local {kinds['local']}, import {kinds['import']}, foreign {kinds['foreign']}), "
          f"table {len(entries)} entries")


if __name__ == "__main__":
    main()
