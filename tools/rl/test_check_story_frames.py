#!/usr/bin/env python3
"""Negative fixtures for check_story_frames.py: each patches an in-memory copy of a passing story ELF
and requires the checker to reject it. The unpatched ELF must pass. usage: test_check_story_frames.py ELF"""

import struct
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_story_frames as csf  # noqa: E402


def offset(sections, address):
    for s in sections:
        if s[1] == 1 and s[3] <= address < s[3] + s[5]:
            return s[4] + address - s[3]
    raise ValueError(f"{address:08x} not loaded")


def j(op, target):
    return (op << 26) | ((target >> 2) & 0x03FFFFFF)


def main(path):
    data, sections, funcs, code = csf.load(path)
    addr = {n: v[0] for n, v in funcs.items()}
    starts = set(addr.values())

    def jal_site(caller, callee):
        start, size, _ = funcs[caller]
        return next(pc for pc, t in csf.jals(data, sections, start, size) if t == addr[callee])

    def prologue(fn):
        for i in range(16):
            pc = addr[fn] + 4 * i
            if csf.word(data, sections, pc) >> 16 == 0x27BD:
                return pc
        raise ValueError(fn)

    start, size, _ = funcs["func_800078D4"]
    helper = sorted({t for _, t in csf.jals(data, sections, start, size) if t not in starts})[0]
    composer = addr["sc_campaign_state"]
    lui_lo = [0x3C080000 | (((composer + 0x8000) >> 16) & 0xFFFF), 0x25080000 | (composer & 0xFFFF)]
    rl_start = addr["rl_start_battle"]

    fixtures = {
        "probe prologue oversized to 96": [(prologue("sc_runtime_probe_poll"), 0x27BDFFA0)],
        "Game_Thread tail-j to composer": [(jal_site("Game_Thread", "sc_campaign_state_poll"), j(2, composer))],
        "078D4 helper tail-j to framed launch": [(helper, j(2, addr["sc_campaign_state_launch"]))],
        "078D4 helper backward branch": [(helper, 0x1000FFFF)],
        "078D4 helper branch with jump in delay slot": [(helper, 0x10000001), (helper + 4, j(2, rl_start))],
        "078D4 helper jalr": [(helper, 0x0100F809)],
        "rl_start_battle unrecognized prologue": [(addr["rl_start_battle"] + 4 * i, 0) for i in range(16)],
        "rl_start_battle frame raised to 64": [(prologue("rl_start_battle"), 0x27BDFFC0)],
        "composer data word": [(addr["Game_Thread"] + 4 * 200, composer)],
        "composer lui/addiu binding": [(addr["func_80029008"] + 4 * 20, lui_lo[0]),
                                       (addr["func_80029008"] + 4 * 21, lui_lo[1])],
    }
    failures = 0
    csf.check(path)
    for name, patches in fixtures.items():
        mutated = bytearray(data)
        for pc, insn in patches:
            struct.pack_into(">I", mutated, offset(sections, pc), insn)
        with tempfile.NamedTemporaryFile(suffix=".elf") as tmp:
            tmp.write(mutated)
            tmp.flush()
            try:
                csf.check(tmp.name)
            except ValueError as error:
                print(f"rejected: {name}: {error}")
                continue
        print(f"ACCEPTED (bad): {name}")
        failures += 1
    if failures:
        raise SystemExit(f"{failures} negative fixture(s) accepted")
    print(f"Story frame guard fixtures: {len(fixtures)} rejected")


if __name__ == "__main__":
    main(sys.argv[1])
