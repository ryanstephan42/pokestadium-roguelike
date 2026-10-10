"""Confirm live battle HP/PP with actual attacks, never injected RAM values."""

import argparse
import json
from pathlib import Path

from tools.rltest.frontend import Frontend, HarnessError


def c_button(emu, direction, after=120):
    x, y = {"up": (0, -32767), "down": (0, 32767),
            "left": (-32767, 0), "right": (32767, 0)}[direction]
    emu.stick(x, y, right=True)
    emu.step(12)
    emu.stick(right=True)
    emu.step(after)


def verify(emu):
    before = emu.battle_mons()
    if not before or before[0]["species"] != 1:
        raise HarnessError("Expected initialized rental Bulbasaur")
    sides = [mon["side"] for mon in before]
    moves = list(emu.read(sides[0] + 0x6AB, 4))
    if moves[2] != 34:
        raise HarnessError(f"Expected Body Slam in slot 2, got {moves}")
    packed = [emu.read(side + 0x6BE, 4) for side in sides]
    party = [emu.read_u32(side + 0x724) for side in sides]
    indices = [emu.read_u32(side + 0x65C) for side in sides]
    records = [base + 0x1C + index * 0x54 for base, index in zip(party, indices)]
    frame = emu.frame
    c_button(emu, "down", after=0)  # Core mapping selects slot 2.
    events = []
    previous = before
    for _ in range(2100):
        current = emu.battle_mons()
        if not current or [mon["side"] for mon in current] != sides:
            raise HarnessError("Battle lifetime changed during observation")
        if current != previous:
            events.append({"frame": emu.frame, "mons": current})
            previous = current
        emu.step()
    after = emu.battle_mons()
    if after[0]["pp"][2] != before[0]["pp"][2] - 1:
        raise HarnessError("Selected Body Slam did not debit exactly one PP")
    if not after[1]["hp"] < before[1]["hp"]:
        raise HarnessError("Body Slam did not hit; rerun (native RNG is time-based)")
    for index in range(2):
        if after[index]["max_hp"] != before[index]["max_hp"]:
            raise HarnessError("Max HP unexpectedly changed")
        now = emu.read(sides[index] + 0x6BE, 4)
        if any((a & 0xC0) != (b & 0xC0) for a, b in zip(packed[index], now)):
            raise HarnessError("PP-Up bits changed")
        if emu.read_u16(records[index] + 2) != after[index]["hp"]:
            raise HarnessError("Animated original-party HP did not converge")
    return {"start_frame": frame, "before": before, "after": after,
            "moves": moves, "events": events,
            "hp_addresses": [hex(side + 0x698) for side in sides],
            "pp_addresses": [hex(side + 0x6BE) for side in sides]}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--core", default="/usr/lib/libretro/mupen64plus_next_libretro.so")
    parser.add_argument("--state", required=True,
                        help="Local matching-core rental Bulbasaur attack-menu savestate")
    args = parser.parse_args()
    output = Path("build/rl-battle-verification")
    with Frontend(args.core, "build/pokestadium-us.z64", "build/pokestadium-us.map",
                  output, timeout=600) as emu:
        emu.load(args.state)
        emu.step(2)
        result = verify(emu)
        result["identity"] = emu._identity()
        output.joinpath("result.json").write_text(json.dumps(result, indent=2) + "\n")
        print(json.dumps(result, indent=2))
        print("Battle HP/PP: OK")


if __name__ == "__main__":
    main()
