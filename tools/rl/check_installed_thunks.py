#!/usr/bin/env python3
"""Fail-closed post-repair check of fragment31 raw callback thunks in a shipped ROM.

Independent of gen_rl_relocs/check_native_thunks: works from ROM bytes and map symbol names.
For every R_MIPS_26 loader entry whose location lies in the vanilla raw thunk table (and every
operand given with --operands), it:
  1. requires the fragment header's relocOffset/sizeInRom/sizeInRam to match the linked map,
     then reads the loader reloc table at header relocOffset;
  2. applies the exact rl_relocate_archive_imports rule (0x843 targets via rl_archive_imports,
     failing where the C code returns -1);
  3. derives the expected target from the vanilla ROM word at the same offset: vanilla function
     name at the raw target -> linked map address of that name;
  4. requires the repaired target to equal it, to be a linked function start, and the opcode
     to be unchanged.
Memmap_RelocateFragment then only rebases the 0x81 segment; offsets are not changed.
The entry path is gated positively as well: the header magic/headerSize and its inst[0..1]
(j to the stub at BASE+0x20; nop) must equal vanilla, the header jump's loader R26 entry must
be present iff vanilla has one, and the stub at BASE+0x20 must be a checked binding to the linked
address of the vanilla entry function.

Usage: check_installed_thunks.py <rom> <elf> <map> <vanilla_rom> <vanilla_map> [--operands FILE]
FILE lists hex operand addresses (first hex token of each BIND line works).
"""

import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_native_thunks import sections  # noqa: E402

BASE = 0x81000000
IMPORT_LO, IMPORT_HI = 0x84300000, 0x84400000


def symbols(path):
    found, funcs = {}, {}
    for line in Path(path).read_text().splitlines():
        m = re.fullmatch(r"\s+0x([0-9a-f]+)\s+([A-Za-z_][A-Za-z0-9_]*)(\s*=.*)?\s*", line)
        if m:
            found.setdefault(m[2], int(m[1], 16))
            if m[2].startswith("func_") and not m[3]:
                funcs.setdefault(int(m[1], 16), m[2])
    return found, funcs


def fragment(rom, syms):
    start = syms["fragment31_ROM_START"]
    relocs = syms["fragment31_relocs_ROM_START"]
    header = struct.unpack_from(">8I", rom, start)
    want = (relocs - start, syms["fragment31_relocs_ROM_END"] - start,
            syms["fragment31_VRAM_END"] - syms["fragment31_VRAM"])
    if tuple(header[5:8]) != want:
        sys.exit("check_installed_thunks: header relocOffset/sizeInRom/sizeInRam "
                 f"{[hex(x) for x in header[5:8]]} disagree with map {[hex(x) for x in want]}")
    count, = struct.unpack_from(">I", rom, relocs)
    entries = struct.unpack_from(f">{count}I", rom, relocs + 4)
    return start, [e for e in entries if (e >> 24) & 0x7F == 4]


def target(word):
    return 0x80000000 | ((word & 0x03FFFFFF) << 2)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    operands = set()
    if "--operands" in sys.argv:
        path = sys.argv[sys.argv.index("--operands") + 1]
        args.remove(path)
        for line in Path(path).read_text().splitlines():
            m = re.search(r"\b(81[0-9a-f]{6})\b", line, re.I)
            if m and line.startswith("BIND"):
                operands.add(int(m[1], 16))
    rom_path, elf, map_path, vrom_path, vmap = args
    rom, vrom = Path(rom_path).read_bytes(), Path(vrom_path).read_bytes()
    syms, funcs = symbols(map_path)
    vsyms, vfuncs = symbols(vmap)
    by_name = {n: a for a, n in funcs.items()}

    imports_at, count_at = syms["rl_archive_imports"], syms["rl_archive_import_count"]
    imports = None
    for base, data in sections(elf).values():
        if base <= count_at < base + len(data) and base <= imports_at:
            n, = struct.unpack_from(">I", data, count_at - base)
            imports = dict(struct.unpack_from(">2I", data, imports_at - base + 8 * i) for i in range(n))
    if imports is None:
        sys.exit("check_installed_thunks: rl_archive_imports not found")

    start, r26 = fragment(rom, syms)
    vstart, vr26 = fragment(vrom, vsyms)
    table = syms["fragment31_unk_table_VRAM"] if "fragment31_unk_table_VRAM" in syms else None
    lo, hi = 0x20, 0x20 + 480
    if table is not None and table != BASE + lo:
        sys.exit("check_installed_thunks: unexpected thunk table address")
    raw = sorted({e & 0xFFFFFF for e in vr26 if lo <= (e & 0xFFFFFF) < hi})
    checked = sorted(set(raw) | {op - BASE for op in operands})
    loader = {e & 0xFFFFFF for e in r26}

    failures, kinds, entry = [], {"local": 0, "import": 0, "foreign": 0}, None
    head, vhead = rom[start:start + 0x14], vrom[vstart:vstart + 0x14]
    inst0, = struct.unpack_from(">I", head)
    if head != vhead or head[8:16] != b"FRAGMENT" or inst0 >> 26 != 2 or target(inst0) != BASE + lo:
        failures.append(f"entry header {head.hex()} vs vanilla {vhead.hex()} (want j {BASE + lo:08X}; nop)")
    if (0 in loader) != (0 in {e & 0xFFFFFF for e in vr26}):
        failures.append("entry header jump: loader R26 entry differs from vanilla")
    if lo not in checked:
        failures.append(f"entry stub {BASE + lo:08X}: not a checked binding")
    for off in checked:
        word, = struct.unpack_from(">I", rom, start + off)
        vword, = struct.unpack_from(">I", vrom, vstart + off)
        if off not in loader:
            failures.append(f"{BASE + off:08X}: no loader R26 entry")
            continue
        if word >> 26 != vword >> 26 or word >> 26 not in (2, 3):
            failures.append(f"{BASE + off:08X}: opcode {word:08X} vs vanilla {vword:08X}")
            continue
        raw_t, vt = target(word), target(vword)
        repaired = raw_t
        if IMPORT_LO <= raw_t < IMPORT_HI:
            mapped = imports.get(raw_t, 0)
            if not mapped or mapped & 3 or not IMPORT_LO <= mapped < IMPORT_HI:
                failures.append(f"{BASE + off:08X}: import {raw_t:08X} unmapped (loader returns -1)")
                continue
            repaired = mapped
        name = vfuncs.get(vt)
        want = by_name.get(name) if name else None
        kind = "import" if IMPORT_LO <= vt < IMPORT_HI else "local" if vt >> 20 == BASE >> 20 else "foreign"
        if want is None or repaired != want or funcs.get(repaired) != name:
            failures.append(f"{BASE + off:08X}: raw {raw_t:08X} repaired {repaired:08X}, vanilla "
                            f"{vt:08X} {name or 'NO-FUNC'} -> {want or 0:08X}")
            continue
        kinds[kind] += 1
        if off == lo:
            entry = name
        if "--report" in sys.argv:
            print(f"BIND {BASE + off:08X} raw {raw_t:08X} repaired {repaired:08X} {name} {kind} PASS")
    if failures:
        print("\n".join(failures))
        sys.exit(f"check_installed_thunks: {len(failures)} failures")
    print(f"check_installed_thunks: entry j {BASE + lo:08X} -> stub -> {entry} {by_name[entry]:08X} PASS")
    print(f"check_installed_thunks: {len(checked)} bindings PASS ({kinds}); operands {len(operands)}")


if __name__ == "__main__":
    main()
