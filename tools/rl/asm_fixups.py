#!/usr/bin/env python3
"""RL-build-only fixups for extracted asm that is not position independent.

Some words in asm/ are data that splat disassembled as instructions; their
PC-relative/absolute operands point into unrelated data and stop assembling
(or silently change meaning) once main code shifts. For the listed VRAMs we
replace the instruction with the original raw word (taken from the splat
comment `/* romoff vram WORD */`), which is exactly what the vanilla ROM holds.

Usage: asm_fixups.py <in.s>   (writes the fixed asm to stdout)
"""

import re
import sys

# vram -> reason
RAW_WORDS = {
    0x80048738: "49190.s func_80048738: F3DEX2 constant table disassembled as jal",
    0x8004873C: "49190.s func_80048738: beql into .L8006873C (data), PC16 overflows after shift",
}

LINE_RE = re.compile(r"^(\s*)/\* [0-9A-F]+ ([0-9A-F]{8}) ([0-9A-F]{8}) \*/\s+\S.*$")


def main():
    hit = set()
    for line in open(sys.argv[1], encoding="utf-8"):
        m = LINE_RE.match(line)
        if m and int(m.group(2), 16) in RAW_WORDS:
            vram = int(m.group(2), 16)
            hit.add(vram)
            sys.stdout.write(f"{m.group(1)}.word 0x{m.group(3)} /* rl fixup: {RAW_WORDS[vram]} */\n")
        else:
            sys.stdout.write(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
