#!/usr/bin/env python3
"""Generate the roguelike (shifted) linker scripts from the vanilla splat output.

The vanilla build places every object at a byte-exact address, and the
undefined-symbol scripts pin ~1700 symbols to literal addresses. Once roguelike
code is added to the resident main segment, everything after the insertion point
moves, so those literal addresses must be re-expressed relative to the object
that contains them. We do that with "anchors":

  * Each input-section line inside the main segment gets an anchor symbol
    `__rla_N = ALIGN(16);` immediately before it (main uses SUBALIGN(16)).
  * The vanilla map tells us where that input section lived originally.
  * A literal symbol `D_x = 0x80xxxxxx;` that fell inside main is rewritten as
    `D_x = __rla_N + offset;` using the nearest preceding input section.

Other transformations:
  * `build/` paths -> `<build_dir>/`.
  * entry.o is replaced by src/roguelike/rl_entry.o (clears the real .bss size
    instead of the hardcoded vanilla size).
  * Roguelike objects are appended to main .text/.data/.rodata/.bss.
  * A new fragment `fragmentRL` (VRAM 0x8F800000) is appended to the ROM,
    after all fixed-address game data.
  * `ALIGN(__romPos, 0x7C0000)` becomes an assert + fixed position so the
    hard-coded ROM offsets used by the game (0x7C0000, 0x820000, ...) stay valid.
"""

import argparse
import bisect
import os
import re
import sys

MAIN_START = 0x80000400
RL_FRAGMENT_VRAM = 0x8F800000
GB_TOWER_ROM = 0x7C0000

OBJ_LINE_RE = re.compile(r"^(\s*)(build/[^\s(]+)\((\.[A-Za-z0-9_.]+)\);\s*$")
SYM_RE = re.compile(r"^(\s*)([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(0x[0-9A-Fa-f]+)\s*;(.*)$")
MAP_FULL_RE = re.compile(r"^ (\.[A-Za-z0-9_.]+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)\s*$")
MAP_NAME_RE = re.compile(r"^ (\.[A-Za-z0-9_.]+)\s*$")
MAP_CONT_RE = re.compile(r"^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)\s*$")


def script_key(path, section):
    # build/lib/libultra.a:foo.o -> build/lib/libultra.a(foo.o), matching ld's map syntax
    if ":" in path:
        archive, member = path.split(":", 1)
        path = f"{archive}({member})"
    return (path, section)


def parse_map(map_path, lo, hi):
    """Return {(file, section): (addr, size)} for input sections within [lo, hi)."""
    out = {}
    pending = None
    with open(map_path) as f:
        for line in f:
            line = line.rstrip("\n")
            m = MAP_FULL_RE.match(line)
            if m:
                sec, addr, size, path = m.group(1), int(m.group(2), 16), int(m.group(3), 16), m.group(4)
                pending = None
            elif pending is not None:
                m2 = MAP_CONT_RE.match(line)
                sec = pending
                pending = None
                if not m2:
                    continue
                addr, size, path = int(m2.group(1), 16), int(m2.group(2), 16), m2.group(3)
            else:
                m3 = MAP_NAME_RE.match(line)
                if m3:
                    pending = m3.group(1)
                continue
            if lo <= addr < hi and (path, sec) not in out:
                out[(path, sec)] = (addr, size)
    return out


def find_main_bounds(lines):
    start = end = None
    for i, l in enumerate(lines):
        if start is None and re.match(r"^\s*text_ROM_START = __romPos;", l):
            start = i
        if re.match(r"^\s*heap_VRAM_END = \.;", l):
            end = i
            break
    if start is None or end is None:
        sys.exit("gen_rl_ld: could not find main/heap segment bounds")
    return start, end


def fragment_block(header_obj, frag_objs, relocs_obj):
    objs = [header_obj] + frag_objs
    b = ["", "    /* ---- roguelike overlay fragment (ROGUELIKE build only) ---- */"]
    b += [
        "    fragmentRL_ROM_START = __romPos;",
        "    fragmentRL_VRAM = ADDR(.fragmentRL);",
        f"    .fragmentRL 0x{RL_FRAGMENT_VRAM:08X} : AT(fragmentRL_ROM_START) SUBALIGN(16)",
        "    {",
        "        FILL(0x00000000);",
        "        fragmentRL_TEXT_START = .;",
    ]
    b += [f"        {o}(.text);" for o in objs]
    b += ["        fragmentRL_TEXT_END = .;", "        fragmentRL_DATA_START = .;"]
    b += [f"        {o}(.data);" for o in objs]
    b += ["        fragmentRL_DATA_END = .;", "        fragmentRL_RODATA_START = .;"]
    for o in objs:
        b += [f"        {o}(.rodata);", f"        {o}(.rodata.*);"]
    b += [
        "        . = ALIGN(16);",
        "        fragmentRL_RODATA_END = .;",
        "    }",
        "    fragmentRL_bss_VRAM = ADDR(.fragmentRL_bss);",
        "    .fragmentRL_bss (NOLOAD) : SUBALIGN(16)",
        "    {",
        "        fragmentRL_BSS_START = .;",
    ]
    for o in objs:
        b += [f"        {o}(.bss);", f"        {o}(COMMON);"]
    b += [
        "        . = ALIGN(16);",
        "        fragmentRL_BSS_END = .;",
        "    }",
        "    __romPos += SIZEOF(.fragmentRL);",
        "    fragmentRL_ROM_END = __romPos;",
        "    fragmentRL_VRAM_END = .;",
        "    __romPos = ALIGN(__romPos, 16);",
        "    . = ALIGN(., 16);",
        "    fragmentRL_relocs_ROM_START = __romPos;",
        "    .fragmentRL_relocs 0x0 : AT(fragmentRL_relocs_ROM_START) SUBALIGN(16)",
        "    {",
        f"        {relocs_obj}(.rodata);",
        "    }",
        "    __romPos += SIZEOF(.fragmentRL_relocs);",
        "    fragmentRL_relocs_ROM_END = __romPos;",
        "    __romPos = ALIGN(__romPos, 16);",
        "    . = ALIGN(., 16);",
        "    fragmentRL_HDR_RELOC_OFFSET = SIZEOF(.fragmentRL);",
        "    fragmentRL_HDR_SIZE_IN_ROM = SIZEOF(.fragmentRL) + SIZEOF(.fragmentRL_relocs);",
        "    fragmentRL_HDR_SIZE_IN_RAM = SIZEOF(.fragmentRL) + SIZEOF(.fragmentRL_bss);",
        '    __rl_assert_bss = ASSERT(fragmentRL_bss_VRAM == fragmentRL_VRAM + SIZEOF(.fragmentRL), "rl fragment bss must follow data");',
        "",
    ]
    return b


def gen(args):
    with open(args.vanilla_ld) as f:
        lines = f.read().split("\n")

    heap_end = None
    with open(args.vanilla_map) as f:
        for l in f:
            m = re.match(r"^\s+0x([0-9a-f]+)\s+heap_VRAM_END = \.", l)
            if m:
                heap_end = int(m.group(1), 16)
                break
    if heap_end is None:
        sys.exit("gen_rl_ld: heap_VRAM_END not found in vanilla map")

    sections = parse_map(args.vanilla_map, MAIN_START, 0x90000000)
    main_lo, main_hi = find_main_bounds(lines)
    rl_objs = [o for o in args.main_objs if o]

    out = []
    anchors = []
    in_main = False
    in_native = False
    for idx, l in enumerate(lines):
        if idx == main_lo:
            in_main = True
        for number in args.regen_fragments:
            if re.match(rf"^\s*fragment{number}_ROM_START = __romPos;", l):
                in_native = True
            if re.match(rf"^\s*fragment{number}_relocs_VRAM_END = \.;", l):
                in_native = False
                out.extend([
                    f"    fragment{number}_HDR_RELOC_OFFSET = SIZEOF(.fragment{number});",
                    f"    fragment{number}_HDR_SIZE_IN_ROM = SIZEOF(.fragment{number}) + SIZEOF(.fragment{number}_relocs);",
                    f"    fragment{number}_HDR_SIZE_IN_RAM = SIZEOF(.fragment{number}) + SIZEOF(.fragment{number}_bss);",
                ])
        m = OBJ_LINE_RE.match(l)
        if (in_main or in_native) and m:
            indent, path, sec = m.groups()
            key = script_key(path, sec)
            if key in sections:
                addr, size = sections[key]
                name = f"__rla_{len(anchors)}"
                out.append(f"{indent}{name} = ALIGN(16);")
                anchors.append((addr, size, name, key))
            if path == "build/asm/us/entry.o":
                l = f"{indent}build/src/roguelike/rl_entry.o({sec});"
        for number in args.regen_fragments:
            l = l.replace(f"build/asm/us/data/fragments/{number}/fragment{number}_header.o",
                          f"build/rl/fragment{number}_header.o")
            l = l.replace(f"build/asm/us/data/fragments/{number}/fragment{number}_reloc.o",
                          f"build/rl/fragment{number}_relocs.o")
        if in_main:
            for marker, sec in (("text_TEXT_END", ".text"), ("text_DATA_END", ".data"),
                                ("text_RODATA_END", ".rodata"), ("text_BSS_END", ".bss")):
                if re.match(rf"^\s*{marker} = \.;", l):
                    for o in rl_objs:
                        out.append(f"        {o}({sec});")
                        if sec == ".bss":
                            out.append(f"        {o}(COMMON);")
                    if sec == ".bss":
                        out.append("        . = ALIGN(8); /* rl_entry clears 8 bytes per iteration */")
        if idx == main_hi:
            in_main = False

        if re.match(r"^\s*__romPos = ALIGN\(__romPos, 0x7C0000\);", l):
            out.append(f'    __rl_assert_rom = ASSERT(__romPos <= 0x{GB_TOWER_ROM:X}, "rl ROM overflowed into gb_tower_roms at 0x7C0000");')
            out.append(f"    __romPos = 0x{GB_TOWER_ROM:X};")
            continue

        if re.match(r"^\s*/DISCARD/\s*:", l):
            out.extend(fragment_block(args.frag_header, [o for o in args.frag_objs if o], args.frag_relocs))
        out.append(l)

    text = "\n".join(out)
    text = re.sub(r"(?<![A-Za-z0-9_/])build/", args.build_dir + "/", text)
    with open(args.out_ld, "w") as f:
        f.write(text)

    anchors.sort()
    starts = [a[0] for a in anchors]

    os.makedirs(args.out_sym_dir, exist_ok=True)
    nrewritten = 0
    for sym_path in args.sym_files:
        with open(sym_path) as f:
            sl = f.read().split("\n")
        res = []
        for l in sl:
            m = SYM_RE.match(l)
            if m:
                indent, name, val, rest = m.groups()
                v = int(val, 16)
                i = bisect.bisect_right(starts, v) - 1
                if MAIN_START <= v < heap_end or (
                    i >= 0 and anchors[i][0] <= v < anchors[i][0] + anchors[i][1]
                ):
                    i = bisect.bisect_right(starts, v) - 1
                    if i < 0:
                        sys.exit(f"gen_rl_ld: no anchor for {name}={val}")
                    a_addr, _, a_name, _ = anchors[i]
                    res.append(f"{indent}{name} = {a_name} + 0x{v - a_addr:X};{rest}")
                    nrewritten += 1
                    continue
            res.append(l)
        with open(os.path.join(args.out_sym_dir, os.path.basename(sym_path)), "w") as f:
            f.write("\n".join(res))

    with open(args.out_anchors, "w") as f:
        for a_addr, size, name, key in anchors:
            f.write(f"{name} 0x{a_addr:08X} 0x{size:X} {key[0]} {key[1]}\n")

    print(f"gen_rl_ld: {len(anchors)} anchors, {nrewritten} absolute symbols re-anchored")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--vanilla-ld", required=True)
    p.add_argument("--vanilla-map", required=True)
    p.add_argument("--build-dir", required=True)
    p.add_argument("--out-ld", required=True)
    p.add_argument("--out-sym-dir", required=True)
    p.add_argument("--out-anchors", required=True)
    p.add_argument("--frag-header", required=True)
    p.add_argument("--frag-relocs", required=True)
    p.add_argument("--main-objs", nargs="*", default=[])
    p.add_argument("--frag-objs", nargs="*", default=[])
    p.add_argument("--sym-files", nargs="*", default=[])
    p.add_argument("--regen-fragments", nargs="*", type=int, default=[])
    gen(p.parse_args())


if __name__ == "__main__":
    main()
