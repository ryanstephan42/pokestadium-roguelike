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

    Only the reviewed +336 shift of the sequential control tail is permitted. Every
    pure payload/mon/event offset and the UI pointer offset must match.
    """
    require(len(words) == len(keys), "Sequential native witness count mismatch")
    native = copy.deepcopy(host)
    values = dict(zip(keys, words))
    shift = values[("control", "size")] - host["control"]["size"]
    require(host["control"]["size"] == 3768 and
            values[("control", "size")] == 4104 and shift == 336,
            "Sequential control ABI requires reviewed HOST3768/native4104 shift+336")
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


def bind_lifecycle_words(keys, words, prefix):
    require(len(keys) == len(words), "Lifecycle witness count mismatch")
    result = {}
    for (group, key), word in zip(keys, words):
        result.setdefault(group, {})[key] = word
    # Native-only lifetime: HOST substitutes OS/display/retirement types.
    # Concrete leaf records remain source-defined, not an arbitrary ABI shift.
    fixed = {
        "snapshot": dict(size=92, frames=0, fallback=48, vi_current=52, vi_next=56,
                         crash_frame=60, waiting=64, requests=68, completions=72,
                         events=76, rsp_state=80, rdp_state=84, task_completions=88),
        "frame": dict(size=16, rsp=1, rdp=2, work=4, task=8, image=12),
        "span": dict(size=8, pixels=0, bytes=4),
    }
    require(set(result) == {"lifecycle", *fixed}, "Lifecycle schema differs")
    for group, expected in fixed.items():
        require(result[group] == expected, f"Native {group} ABI differs")
    offsets = result["lifecycle"]
    require(set(offsets) == {
        "memory_live", "memory_storage", "lifetime_live", "display_acquired",
        "display_scope", "retirement_acquired", "retirement_pending", "backing_retired",
        "snapshot_valid", "fallback_completed", "snapshot", "spans"},
        "Lifecycle control field schema differs")
    require(len(set(offsets.values())) == len(offsets), "Lifecycle fields overlap")
    for key, offset in offsets.items():
        size = 92 if key == "snapshot" else 16 if key == "spans" else 4
        require(offset % 4 == 0 and 0 <= offset <= prefix - size,
                "Native lifecycle field outside control prefix")
    ranges = [(offset, offset + (92 if key == "snapshot" else 16 if key == "spans" else 4))
              for key, offset in offsets.items()]
    ranges.sort()
    require(all(a[1] <= b[0] for a, b in zip(ranges, ranges[1:])),
            "Native lifecycle field ranges overlap")
    return result


def verify_lifecycle(elf, rom, native):
    keys = layout_keys((ROOT / "src/story/sc_observer_lifecycle.inc").read_text())
    symbol = elf.symbol("gScCampaignLifecycleLayout")
    require(symbol[2] == len(keys) * 4, "Lifecycle linked witness size mismatch")
    raw = elf.read(symbol[1], symbol[2])
    require(elf.rom_read(rom, symbol[1], symbol[2]) == raw,
            "Lifecycle ROM witness differs from ELF")
    result = bind_lifecycle_words(keys, struct.unpack(">" + "I" * len(keys), raw),
                                 native["control"]["sequential_request"])
    result["witness_sha256"] = hashlib.sha256(raw).hexdigest()
    return result


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
    native_lifecycle = verify_lifecycle(elf, args.rom.read_bytes(), native_abi)
    require(symbols.address("gScCampaignLifecycleLayout") ==
            elf.symbol("gScCampaignLifecycleLayout")[1], "Lifecycle map/ELF mismatch")
    result = dict(scope="linked-layout binding only; not catalog/heap/fence/Pak or execution acceptance",
                  binding=bound, abi=native_abi, package_sha256=hashlib.sha256(raw).hexdigest(),
                  native_lifecycle=native_lifecycle,
                  native_service=bind_service_lifecycle(elf),
                  boot_symbols=bind_boot_symbols(elf),
                  elf_sha256=hashlib.sha256(args.elf.read_bytes()).hexdigest(),
                  map_sha256=hashlib.sha256(args.map.read_bytes()).hexdigest(),
                  rom_sha256=hashlib.sha256(args.rom.read_bytes()).hexdigest())
    with args.out.open("x") as output:
        json.dump(result, output, indent=2)
        output.write("\n")


def bind_boot_symbols(elf):
    addresses = {name: elf.symbol(name)[1] for name in
                 ("gCurrentGameState", "gScCampaignRun",
                  "gScCampaignMenuReady", "gFragments")}
    memory = []
    for index, table in elf.tables.items():
        strings = elf.sections[elf.sections[index][6]]
        for symbol in table:
            if (symbol[3] >> 4 in (1, 2) and symbol[5] == 0xFFF1 and
                    elf.string(strings, symbol[0]) == "osMemSize"):
                memory.append(symbol[1])
    require(len(memory) == 1, "Missing/ambiguous ELF absolute boot memory parameter")
    addresses["osMemSize"] = memory[0]
    require(addresses["osMemSize"] == 0x80000318, "Unexpected ELF boot memory parameter")
    require(all(0x80000000 <= address < 0x80400000 for address in addresses.values()),
            "Boot witness outside resident lower RAM")
    return addresses


def bind_service_lifecycle(elf):
    symbol = elf.symbol("gScServiceLifecycle")
    require(symbol[2] == 40 and 0x80000000 <= symbol[1] <= 0x80400000 - 40,
            "Service lifecycle witness is not resident or has unexpected size")
    size = elf.symbol("gScServiceLifecycleSize")
    require(size[2] == 4 and elf.read(size[1], 4) == b"\0\0\0\x28",
            "Compiled service lifecycle size differs")
    return dict(address=symbol[1], size=40, failed_offset=36)


if __name__ == "__main__":
    main()
