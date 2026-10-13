#!/usr/bin/env python3
"""Audit an isolated AI link; never treats static evidence as runtime clearance."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess


def symbols(path):
    result = {}
    for line in subprocess.check_output(
            ["mips-linux-gnu-nm", "-n", str(path)], text=True).splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[1] != "U":
            result[fields[2]] = int(fields[0], 16)
    return result


def function_sizes(path):
    data = path.read_bytes()
    shoff, = struct.unpack_from(">I", data, 0x20)
    stride, count = struct.unpack_from(">HH", data, 0x2E)
    sections = [struct.unpack_from(">10I", data, shoff + i * stride) for i in range(count)]
    result = {}
    zero_sized = {}
    for section in sections:
        if section[1] != 2:
            continue
        for offset in range(section[4], section[4] + section[5], 16):
            _, value, size, info, _, _ = struct.unpack_from(">IIIBBH", data, offset)
            if info & 15 == 2 and value:
                if size:
                    result[value] = size
                else:
                    zero_sized[value] = None
    addresses = sorted(set(result) | set(zero_sized))
    for address in zero_sized:
        index = addresses.index(address)
        if index + 1 < len(addresses):
            result[address] = addresses[index + 1] - address
    return result


def imports(base, out, objects):
    known = symbols(base)
    undefined, defined = set(), set()
    for obj in objects:
        defined.update(symbols(obj))
        for line in subprocess.check_output(
                ["mips-linux-gnu-nm", "-u", obj], text=True).splitlines():
            undefined.add(line.split()[-1])
    linker_constants = {"fragmentAI_HDR_RELOC_OFFSET", "fragmentAI_HDR_SIZE_IN_ROM",
                        "fragmentAI_HDR_SIZE_IN_RAM"}
    required = sorted(undefined - defined - linker_constants)
    bindings = {}
    for name in required:
        if name not in known:
            raise ValueError("unresolved import " + name)
        value = known[name]
        if not (0x80000000 <= value < 0x81000000 or
                0x84300000 <= value < 0x84400000):
            raise ValueError(f"unapproved import {name} at {value:08x}")
        bindings[name] = f"0x{value:08x}"
    (out / "native-imports.ld").write_text("".join(
        f"PROVIDE({name} = {value});\n" for name, value in bindings.items()))
    (out / "actual-imports.json").write_text(json.dumps(bindings, indent=2) + "\n")


def image(base, out):
    path = out / "ai.elf"
    data = path.read_bytes()
    if data[:6] != b"\x7fELF\x01\x02":
        raise ValueError("MIPS ELF32BE required")
    shoff, = struct.unpack_from(">I", data, 0x20)
    stride, count, names_index = struct.unpack_from(">HHH", data, 0x2E)
    sections = [struct.unpack_from(">10I", data, shoff + i * stride) for i in range(count)]
    names = sections[names_index][4]
    indexed = {}
    for i, section in enumerate(sections):
        start = names + section[0]
        name = data[start:data.index(b"\0", start)].decode()
        indexed[name] = (i, section)
    target_index, target = indexed[".fragmentAI"]
    _, table = indexed[".fragmentAI_relocs"]
    _, bss = indexed[".fragmentAI_bss"]
    if target[3] != 0x8F900000 or bss[3] != target[3] + target[5]:
        raise ValueError("unexpected ID233 layout")
    header = struct.unpack_from(">8I", data, target[4])
    expected = (32, target[5], target[5] + table[5], target[5] + bss[5])
    if header[4:] != expected or data[target[4] + 8:target[4] + 16] != b"FRAGMENT":
        raise ValueError("invalid loader header")
    kinds = Counter()
    locations = []
    for section in sections:
        if section[1] != 9 or section[7] != target_index:
            continue
        symtab = sections[section[6]]
        for offset in range(section[4], section[4] + section[5], 8):
            location, info = struct.unpack_from(">II", data, offset)
            if info >> 8 >= symtab[5] // symtab[9]:
                raise ValueError("invalid ELF relocation symbol")
            kind = info & 255
            if kind not in {2, 4, 5, 6}:
                raise ValueError("unsupported relocation")
            if not target[3] <= location < target[3] + target[5]:
                raise ValueError("relocation outside image")
            kinds[kind] += 1
            if not 16 <= location - target[3] < 32:
                locations.append((kind << 24) | (location - target[3]))
    packed_count, = struct.unpack_from(">I", data, table[4])
    packed = list(struct.unpack_from(f">{packed_count}I", data, table[4] + 4))
    if packed != locations:
        raise ValueError("loader table differs from ELF relocation order")
    report = {
        "base_elf_sha256": hashlib.sha256(base.read_bytes()).hexdigest(),
        "candidate_elf_sha256": hashlib.sha256(data).hexdigest(),
        "id": 233, "vma": "0x8f900000", "abi": "ELF32BE MIPS o32",
        "code_data_bytes": target[5], "relocation_table_bytes": table[5],
        "bss_bytes": bss[5], "loader_allocation_bytes": max(expected[2:]),
        "relocation_count": packed_count, "elf_relocation_kinds": dict(kinds),
        "imports": json.loads((out / "actual-imports.json").read_text()),
        "assets": "no asset sections copied; byte tables referenced from native BATL",
        "unproven": ["runtime exclusive lease", "loader/callback integration",
                     "preinit hook delta", "postinit native allocation peak",
                     "complete callsite stack", "ROM append/table install"],
    }
    (out / "image-audit.json").write_text(json.dumps(report, indent=2) + "\n")
    payload = data[target[4]:target[4] + target[5]] + data[table[4]:table[4] + table[5]]
    (out / "ai-image.bin").write_bytes(payload)
    print(f"audited ID233: {packed_count} ordered loader relocations; "
          f"{report['loader_allocation_bytes']} image bytes")


def stack(base, out):
    functions = {}
    for path in [base, out / "ai.elf"]:
        sizes = function_sizes(path)
        disassembly = subprocess.check_output(
            ["mips-linux-gnu-objdump", "-d", str(path)], text=True)
        current = None
        for line in disassembly.splitlines():
            match = re.match(r"([0-9a-f]+) <([^>]+)>:", line)
            if match:
                current = int(match[1], 16)
                if current not in sizes or (path == base and
                                            0x8F900000 <= current < 0x8FA00000):
                    current = None
                    continue
                functions[current] = dict(name=match[2], frame=0, calls=set(), indirect=[])
                continue
            if current is None:
                continue
            instruction = re.match(r"\s*([0-9a-f]+):", line)
            if not instruction or int(instruction[1], 16) >= current + sizes[current]:
                continue
            frame = re.search(r"\baddiu\s+sp,sp,-(\d+)", line)
            if frame:
                functions[current]["frame"] += int(frame[1])
            call = re.search(r"\b(?:jal|j)\s+([0-9a-f]+)\s+<([^>]+)>", line)
            if call:
                if call[2].startswith(functions[current]["name"] + "+"):
                    continue
                functions[current]["calls"].add(int(call[1], 16))
            if re.search(r"\bjalr\b", line):
                functions[current]["indirect"].append(line.strip())
    missing, indirect = set(), {}

    def walk(address, trail=()):
        if address not in functions:
            missing.add(f"0x{address:08x}")
            return 0, [f"missing {address:08x}"]
        fn = functions[address]
        if address in trail:
            # The audited sole self recursion lowers argument 12 to 11 once.
            if fn["name"] not in {"func_84378B0C", "sc_owned_84378B0C"}:
                raise ValueError("unaudited recursive stack edge " + fn["name"])
            if trail.count(address) == 2:
                return 0, []
        if fn["indirect"]:
            indirect[fn["name"]] = fn["indirect"]
        best, best_path = 0, []
        for callee in fn["calls"]:
            size, path = walk(callee, trail + (address,))
            if size > best:
                best, best_path = size, path
        return fn["frame"] + best, [f"{fn['name']}:{fn['frame']}"] + best_path

    results = {}
    entries = symbols(out / "ai.elf")
    for name in ["sc_ai_entry", "sc_ai_select", "sc_ai_owned_select",
                 "sc_ai_player_init", "sc_ai_transaction_begin", "sc_ai_transaction_end"]:
        size, path = walk(entries[name])
        results[name] = dict(conservative_bytes=size, maximum_path=path)
    report = dict(roots=results, missing=sorted(missing), indirect=indirect,
                  qualification="current linked overlay + imported native closure only; "
                  "tail calls conservatively charged; 78B0C depth <=2",
                  unproven=["not yet wired native selector ancestors",
                            "loader/init/free callbacks outside overlay roots",
                            "actual execution stack high-water"])
    (out / "scoped-stack.json").write_text(json.dumps(report, indent=2) + "\n")
    if missing or indirect:
        raise ValueError("incomplete overlay-root stack closure; see scoped-stack.json")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=["imports", "image", "stack"])
    parser.add_argument("base", type=Path)
    parser.add_argument("out", type=Path)
    parser.add_argument("objects", nargs="*")
    args = parser.parse_args()
    if args.mode == "imports":
        imports(args.base, args.out, args.objects)
    elif args.mode == "image":
        image(args.base, args.out)
    else:
        stack(args.base, args.out)


if __name__ == "__main__":
    main()
