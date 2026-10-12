#!/usr/bin/env python3
"""Per-build check of the story launch-path stack frames and call boundaries.

IDO has no noinline attribute, so the static launch helper (rl_prepare_sides) is checked
in the linked ELF instead: rl_start_battle must keep its capped frame and call an
out-of-line, symbol-less helper before func_80029008. The helper must return before the
battle starts, so its frame is not under the battle. sc_campaign_state must stay unreferenced
because calling it would re-nest the poll beneath the launch frame. The ordinary roguelike pending-battle
runner (static rl_main_run_pending_battle) must stay an out-of-line helper of rl_main_state_roguelike with a
capped frame, since it sits beneath the battle. This is a regression gate for the
measured frames, not a stack proof.
"""

import argparse
import struct
from pathlib import Path

# Upper bounds (bytes) for the frames measured on the reviewed candidate.
CAPS = {
    "Game_Thread": 32,
    "rl_main_state_roguelike": 32,
    "sc_runtime_probe_poll": 88,
    "sc_campaign_state_launch": 64,
    "sc_campaign_state_poll": 40,
    "rl_start_battle": 56,
    "func_80029008": 32,
    "func_800078D4": 56,
}
HELPER_CAP = 96
RUNNER_CAP = 48
OPTIONAL = {"sc_runtime_probe_poll"}  # linked only in native-probe profiles


def load(path):
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

    named = {name(i): s for i, s in enumerate(sections)}
    symtab = named[".symtab"]
    strings = sections[symtab[6]]
    funcs = {}
    for offset in range(symtab[4], symtab[4] + symtab[5], 16):
        st_name, value, size, info, _, shndx = struct.unpack_from(">IIIBBH", data, offset)
        if info & 0xF != 2 or not size or shndx == 0 or shndx >= count:
            continue
        start = strings[4] + st_name
        funcs.setdefault(data[start:data.index(b"\0", start)].decode("ascii"), (value, size, shndx))
    code = [s for s in sections if s[1] == 1 and s[2] & 4 and s[5]]
    return data, sections, funcs, code


def word(data, sections, address):
    for s in sections:
        if s[1] == 1 and s[3] <= address < s[3] + s[5]:
            return struct.unpack_from(">I", data, s[4] + address - s[3])[0]
    raise ValueError(f"address {address:08x} not in a loaded section")


def frame(data, sections, address):
    for i in range(16):
        insn = word(data, sections, address + 4 * i)
        if insn >> 16 == 0x27BD:  # addiu sp, sp, imm
            imm = insn & 0xFFFF
            return 0x10000 - imm if imm & 0x8000 else 0
        if insn == 0x03E00008:  # jr ra: leaf without a frame
            return 0
    return 0


def leaf(data, sections, address):
    """True if the code from address to its first jr ra makes no calls and allocates no frame."""
    for i in range(64):
        insn = word(data, sections, address + 4 * i)
        if insn >> 26 == 3 or insn & 0xFC00003F == 9 or insn >> 16 == 0x27BD:
            return False
        if insn == 0x03E00008:
            return True
    return False


def body(data, sections, start):
    """Size in bytes of the code from start through its first jr ra and delay slot."""
    for i in range(4096):
        if word(data, sections, start + 4 * i) == 0x03E00008:
            return 4 * (i + 2)
    raise ValueError(f"no jr ra after {start:08x}")


def jals(data, sections, start, size):
    out = []
    for pc in range(start, start + size, 4):
        insn = word(data, sections, pc)
        if insn >> 26 == 3:
            out.append((pc, ((pc + 4) & 0xF0000000) | ((insn & 0x03FFFFFF) << 2)))
    return out


def check(path):
    data, sections, funcs, code = load(path)
    report = []
    for fn, cap in CAPS.items():
        if fn not in funcs:
            if fn in OPTIONAL:
                continue
            raise ValueError(f"missing function symbol {fn}")
        size = frame(data, sections, funcs[fn][0])
        if size > cap:
            raise ValueError(f"{fn} frame {size} exceeds reviewed cap {cap}")
        report.append(f"{fn}={size}")

    starts = {v[0] for v in funcs.values()}
    start, size, _ = funcs["rl_start_battle"]
    calls = jals(data, sections, start, size)
    battle = [pc for pc, t in calls if t == funcs["func_80029008"][0]]
    if not battle:
        raise ValueError("rl_start_battle does not call func_80029008")
    helpers = [(pc, t) for pc, t in calls if pc < battle[0] and t not in starts]
    if len(helpers) != 1:
        raise ValueError(f"rl_start_battle has {len(helpers)} pre-battle local helper calls, expected 1")
    helper_frame = frame(data, sections, helpers[0][1])
    if not 0 < helper_frame <= HELPER_CAP:
        raise ValueError(f"launch helper {helpers[0][1]:08x} frame {helper_frame} outside (0,{HELPER_CAP}]")
    report.append(f"helper@{helpers[0][1]:08x}={helper_frame}")

    start, size, _ = funcs["rl_main_state_roguelike"]
    runners = [t for _, t in jals(data, sections, start, size) if t not in starts and
               any(c == funcs["rl_start_battle"][0] for _, c in jals(data, sections, t, body(data, sections, t)))]
    if len(runners) != 1:
        raise ValueError(f"rl_main_state_roguelike has {len(runners)} local battle runners, expected 1")
    runner_frame = frame(data, sections, runners[0])
    if runner_frame > RUNNER_CAP:
        raise ValueError(f"pending-battle runner {runners[0]:08x} frame {runner_frame} exceeds {RUNNER_CAP}")
    report.append(f"runner@{runners[0]:08x}={runner_frame}")

    # func_800078D4's frame-handoff helpers must stay out-of-line leaves.
    start, size, _ = funcs["func_800078D4"]
    locals_ = sorted({t for _, t in jals(data, sections, start, size) if t not in starts})
    for target in locals_:
        if not leaf(data, sections, target):
            raise ValueError(f"func_800078D4 local helper {target:08x} is not a frameless leaf")
    report.append("078D4-leaves=" + ",".join(f"{t:08x}" for t in locals_))

    composer = funcs["sc_campaign_state"][0] if "sc_campaign_state" in funcs else None
    if composer is not None:
        for s in code:
            for off in range(0, s[5] - 3, 4):
                insn = struct.unpack_from(">I", data, s[4] + off)[0]
                pc = s[3] + off
                if insn >> 26 == 3 and ((pc + 4) & 0xF0000000) | ((insn & 0x03FFFFFF) << 2) == composer:
                    raise ValueError(f"sc_campaign_state called at {pc:08x}; it must stay unreferenced")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf")
    print("Story launch frames: OK; " + " ".join(check(parser.parse_args().elf)) + " (not stack proof)")
