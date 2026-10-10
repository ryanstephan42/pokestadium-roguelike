#!/usr/bin/env python3
"""Run a deterministic host balance estimate, NOT a native battle/emulator test."""

import argparse
from pathlib import Path
import subprocess
import tempfile

from rl_extract_rosters import extract


ROOT = Path(__file__).resolve().parents[1]
ROSTERS = (
    "poke_poke", "poke_great", "poke_ultra", "poke_master",
    "prime_poke", "prime_great", "prime_ultra", "prime_master",
    "pewter", "cerulean", "vermilion", "celadon", "fuchsia", "saffron",
    "cinnabar", "viridian", "elite_four", "rival", "mewtwo",
)


def mon_initializer(mon, rom):
    # CalculateStatValue indexes the 23-byte PokemonStats array at 0x80070F84.
    offset = 0x71B84 + mon["species"] * 23
    base = list(rom[offset + 6:offset + 11])
    growth = {0: 0, 3: 3, 4: 1, 5: 2}[rom[offset + 19]]
    array = lambda values: "{" + ",".join(map(str, values)) + "}"
    return (
        "{%d,%d,%d,0,%d,%d,%s,%s,{0,0,0,0,0},%s,%s,%s,"
        "0,0,0,RL_NO_SLOT,\"\"}"
        % (mon["species"], mon["level"], growth, mon["exp"], mon["dvs"],
           array(mon["stat_exp"]), array(base), array(mon["moves"]),
           array(mon["pp"]), array(mon["pp"]))
    )


def write_fixture(rom, output):
    data = extract(rom)
    trainers = {entry["id"]: entry for entry in data["trainer_sets"]}
    rentals = {entry["id"]: entry for entry in data["rental_sets"]}
    lines = ["/* Local ROM-derived fixture: never commit. */"]
    for round_index in range(2):
        for roster_index, name in enumerate(ROSTERS):
            entry = trainers[name + ("_r2" if round_index else "")]
            for trainer in entry["trainers"]:
                ident = "team_%d_%d_%d" % (
                    round_index, roster_index, trainer["index"])
                lines.append("static const RlMon %s[] = {%s};" % (
                    ident, ",".join(mon_initializer(m, rom) for m in trainer["mons"])))
    lines.append("static const RlPool balance_pools[2][RL_ROSTER_COUNT][8] = {")
    for round_index in range(2):
        lines.append("{")
        for roster_index, name in enumerate(ROSTERS):
            entry = trainers[name + ("_r2" if round_index else "")]
            lines.append("{%s}," % ",".join(
                "{team_%d_%d_%d,%d,1}" % (
                    round_index, roster_index, t["index"], len(t["mons"]))
                for t in entry["trainers"]))
        lines.append("},")
    lines.append("};")
    lines.append("static const RlMon balance_rentals[] = {%s};" % ",".join(
        mon_initializer(mon, rom) for mon in rentals["poke"]["mons"]))
    lines.append("static const RlPool balance_starters = "
                 "{balance_rentals,sizeof(balance_rentals)/sizeof(RlMon),0};")
    output.write_text("\n".join(lines) + "\n", encoding="ascii")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=Path, default=ROOT / "baseroms/us/baserom.z64")
    parser.add_argument("--runs", type=int, default=1000)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    if not 1 <= args.runs <= 100000 or not 0 <= args.seed <= 0xFFFFFFFF:
        parser.error("runs must be 1..100000; seed must be an unsigned 32-bit integer")
    rom = args.rom.read_bytes()
    if rom[:4] != b"\x80\x37\x12\x40" or rom[0x3B:0x3F] != b"NPOE":
        parser.error("expected local Stadium US big-endian ROM")
    with tempfile.TemporaryDirectory(prefix="rl-balance-") as directory:
        directory = Path(directory)
        write_fixture(rom, directory / "balance_data.h")
        binary = directory / "balance"
        sources = ["core", "map", "content", "enemy", "party", "rewards", "relics"]
        command = ["gcc", "-std=c89", "-pedantic", "-Wall", "-Wextra", "-Werror",
                   "-O2", "-I" + str(ROOT / "src/roguelike"), "-I" + str(directory)]
        if args.sanitize:
            command += ["-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        command += [str(ROOT / "tools/rl_balance.c")]
        command += [str(ROOT / "src/roguelike" / ("rl_%s.c" % name)) for name in sources]
        subprocess.run(command + ["-o", str(binary)], check=True)
        subprocess.run([str(binary), str(args.runs), str(args.seed)], check=True)


if __name__ == "__main__":
    main()
