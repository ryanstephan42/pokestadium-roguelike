#!/usr/bin/env python3
"""Verify every RL anchor landed exactly on its input section.

gen_rl_ld.py rewrites literal main-segment addresses relative to anchors placed
before each input section; this check guarantees the anchors are exact (e.g. no
alignment padding slipped between an anchor and its section).

Usage: check_rl_anchors.py <anchors.txt> <rl.map>
"""

import os
import re
import sys

sys.path.insert(0, os.path.dirname(__file__))
from gen_rl_ld import parse_map  # noqa: E402


def main():
    anchors_path, map_path = sys.argv[1], sys.argv[2]
    build_dir = os.path.dirname(os.path.dirname(os.path.abspath(anchors_path)))
    bd = os.path.relpath(build_dir)
    sym = {}
    with open(map_path) as f:
        for l in f:
            m = re.match(r"^\s+0x([0-9a-f]+)\s+(__rla_\d+) = ", l)
            if m:
                sym[m.group(2)] = int(m.group(1), 16)
    sections = parse_map(map_path, 0x80000400, 0x80800000)
    bad = 0
    n = 0
    for l in open(anchors_path):
        name, _vaddr, size, path, sec = l.split()
        if int(size, 16) == 0:
            continue
        rl_path = re.sub(r"^build/", bd + "/", path)
        if rl_path == f"{bd}/asm/us/entry.o":
            rl_path = f"{bd}/src/roguelike/rl_entry.o"
        got = sections.get((rl_path, sec))
        if got is None or name not in sym:
            print(f"check_rl_anchors: {name} ({path} {sec}) missing from RL map")
            bad += 1
            continue
        n += 1
        if got[0] != sym[name]:
            print(f"check_rl_anchors: {name} = 0x{sym[name]:08X} but {rl_path}({sec}) at 0x{got[0]:08X}")
            bad += 1
    if bad:
        sys.exit(f"check_rl_anchors: {bad} bad anchors")
    print(f"check_rl_anchors: {n} anchors OK")


if __name__ == "__main__":
    main()
