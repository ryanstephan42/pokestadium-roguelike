"""Bind sequential HOST offset schema to an existing final MIPS ELF/ROM.

This tool builds nothing and never starts a core. Missing linked artifacts
refuse; HOST/synthetic fixtures cannot constitute native acceptance.
"""

import argparse
import copy
import hashlib
import json
import pathlib
import re
import struct

from tools.rltest.frontend import HarnessError, Symbols
from tools.rltest.seal_story_current_six import Elf

ROOT = pathlib.Path(__file__).resolve().parents[2]


def require(value, message):
    if not value:
        raise HarnessError(message)


def layout_keys(source):
    keys = []
    for line in source.splitlines():
        match = re.fullmatch(r'SC_SIZE\("([^"]*)", \w+\)', line)
        if match:
            keys.append((match[1], "size"))
            continue
        match = re.fullmatch(r'SC_OFFSET\("([^"]*)", \w+, (\w+)\)', line)
        if match:
            keys.append((match[1], match[2]))
            continue
        match = re.fullmatch(r'SC_VALUE\("([^"]*)", "([^"]*)", .+\)', line)
        require(match, "Unknown sequential layout witness directive")
        keys.append(match.groups())
    require(len(keys) == len(set(keys)), "Duplicate sequential layout witness")
    return keys


def expected_words(abi, keys):
    return [abi[group][key] if group else abi[key] for group, key in keys]


def bind_words(host, keys, words):
    """Native lifetime prefix includes real OS types, unlike the HOST seams.

    Only a uniform shift of the sequential control tail is permitted. Every
    pure payload/mon/event offset and the UI pointer offset must match.
    """
    require(len(words) == len(keys), "Sequential native witness count mismatch")
    native = copy.deepcopy(host)
    values = dict(zip(keys, words))
    shift = values[("control", "size")] - host["control"]["size"]
    for (group, key), actual, expected in zip(keys, words, expected_words(host, keys)):
        allowed = (group == "control" and (key == "size" or key.startswith("sequential_")))
        require(actual == expected + (shift if allowed else 0),
                f"Sequential native payload ABI differs: {group}.{key}")
        if group:
            native[group][key] = actual
        else:
            native[key] = actual
    control = native["control"]
    for key, offset in control.items():
        if key != "size":
            require(0 <= offset < control["size"], "Sequential native control offset out of bounds")
    return native


def verify_layout(elf, rom, abi, keys):
    require(elf.kind == 2, "Sequential binding requires final linked executable ELF")
    require(rom[:4] == b"\x80\x37\x12\x40" and abi["pointer_size"] == 4,
            "Sequential binding requires big-endian N64 ROM and 32-bit layout")
    symbol = elf.symbol("gScCampaignObserverLayout")
    require(symbol[2] == len(keys) * 4, "Sequential linked layout witness size mismatch")
    raw = elf.read(symbol[1], symbol[2])
    native = bind_words(abi, keys, struct.unpack(">" + "I" * len(keys), raw))
    require(elf.rom_read(rom, symbol[1], symbol[2]) == raw,
            "Sequential ROM layout witness differs from ELF")
    pointer = elf.symbol("gScCampaignObserverControl")
    require(pointer[2] == 4, "Sequential control pointer ABI mismatch")
    pointer_raw = elf.read(pointer[1], 4)
    control = int.from_bytes(pointer_raw, "big")
    require(0x80000000 <= control <= 0x80400000 - native["control"]["size"],
            "Sequential observer control is not resident lower RDRAM")
    require(elf.rom_read(rom, pointer[1], 4) == pointer_raw,
            "Sequential ROM control pointer differs from ELF")
    run = elf.symbol("gScCampaignRun")
    require(run[2] == abi["run"]["size"] and
            0x80000000 <= run[1] <= 0x80400000 - run[2],
            "Sequential runner metadata outside resident lower RDRAM")
    return dict(control=control, control_pointer=pointer[1], run=run[1], abi=native,
                witness=hashlib.sha256(raw).hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("package", "elf", "rom", "map", "out"):
        parser.add_argument("--" + name, type=pathlib.Path, required=True)
    args = parser.parse_args()
    raw = (args.package / "manifest.json").read_bytes()
    require(hashlib.sha256(raw).hexdigest() ==
            (args.package / "manifest.sha256").read_text().strip(),
            "Sequential source package manifest hash mismatch")
    manifest = json.loads(raw)
    require(manifest["scope"] == "source-HOST-only-not-native-seal-or-execution-authorization",
            "Wrong sequential source package scope")
    for name, expected in manifest["source_sha256"].items():
        path = (ROOT / name).resolve()
        require(path.is_relative_to(ROOT) and hashlib.sha256(path.read_bytes()).hexdigest() == expected,
                f"Sequential source package stale: {name}")
    for name, expected in manifest["files"].items():
        path = (args.package / name).resolve()
        require(path.is_relative_to(args.package.resolve()) and
                hashlib.sha256(path.read_bytes()).hexdigest() == expected,
                f"Sequential package artifact changed: {name}")
    keys = layout_keys((ROOT / "src/story/sc_observer_layout.inc").read_text())
    elf = Elf(args.elf)
    bound = verify_layout(elf, args.rom.read_bytes(), manifest["layout"], keys)
    symbols = Symbols(args.map)
    for name in ("gScCampaignObserverLayout", "gScCampaignObserverControl", "gScCampaignRun"):
        require(symbols.address(name) == elf.symbol(name)[1],
                f"Sequential map/ELF symbol mismatch: {name}")
    native_abi = bound.pop("abi")
    result = dict(scope="linked-layout binding only; not catalog/heap/fence/Pak or execution acceptance",
                  binding=bound, abi=native_abi, package_sha256=hashlib.sha256(raw).hexdigest(),
                  elf_sha256=hashlib.sha256(args.elf.read_bytes()).hexdigest(),
                  map_sha256=hashlib.sha256(args.map.read_bytes()).hexdigest(),
                  rom_sha256=hashlib.sha256(args.rom.read_bytes()).hexdigest())
    with args.out.open("x") as output:
        json.dump(result, output, indent=2)
        output.write("\n")


if __name__ == "__main__":
    main()
