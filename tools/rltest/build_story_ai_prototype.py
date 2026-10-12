#!/usr/bin/env python3
"""Generate isolated selector fixtures from an explicitly supplied audited ELF."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess


def function(text, name):
    match = re.search(r"(?m)^[\w *]+\b" + name + r"\([^;{}]*?\)\s*\{", text)
    if not match:
        raise ValueError(name)
    depth, end = 1, match.end()
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[match.start():end]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    source = Path("src/fragments/62/fragment62_361050.c")
    text = source.read_text()
    owned = function(text, "func_84383B5C")
    owned = owned.replace("func_84383B5C(UNUSED s32 arg0)", "sc_ai_owned_select(s32 owner)")
    owned = owned.replace("D_84390010[0]", "D_84390010[1 - owner]")
    owned = owned.replace("D_84390010[1]", "D_84390010[owner]")
    owned = owned.replace("D_843C5568[0].unk_003", "D_843C5568[1 - owner].unk_003")
    if "s32 owner" not in owned:
        raise ValueError("unexpected selector signature")
    (args.out / "owned-selector.inc").write_text(owned + "\n")
    dependency = Path("src/fragments/62/fragment62_359F90.c")
    (args.out / "native-dependencies.inc").write_text(
        function(dependency.read_text(), "func_8436FD54") + "\n")
    data = args.elf.read_bytes()
    if data[:6] != b"\x7fELF\x01\x02":
        raise ValueError("ELF32BE required")
    shoff = struct.unpack_from(">I", data, 0x20)[0]
    entsize, count = struct.unpack_from(">HH", data, 0x2E)
    sections = [struct.unpack_from(">10I", data, shoff + i * entsize) for i in range(count)]
    symbols = {}
    for line in subprocess.check_output(["mips-linux-gnu-nm", "-n", str(args.elf)], text=True).splitlines():
        parts = line.split()
        if len(parts) >= 3:
            symbols[parts[2]] = int(parts[0], 16)

    def read(address, size):
        for section in sections:
            if section[1] != 8 and section[3] <= address and address + size <= section[3] + section[5]:
                offset = section[4] + address - section[3]
                return data[offset:offset + size]
        raise ValueError(f"missing bytes {address:x}+{size:x}")

    sizes = {"D_80070FA0": 151 * 0x17, "D_80072B00": 165 * 6,
             "D_8438AC60": 0x50, "D_8438ACB8": 0x1A,
             "D_8438ACD4": 0x1A, "D_8438ACF0": 0xF9, "_7820E0": 0x1680}
    output, manifest = [], {}
    for symbol, size in sizes.items():
        raw = read(symbols[symbol], size)
        output.append("static const unsigned char fixture_" + symbol + "[] = {\n" +
                      ", ".join(hex(value) for value in raw) + "\n};")
        manifest[symbol] = dict(address=hex(symbols[symbol]), size=size,
                                sha256=hashlib.sha256(raw).hexdigest())
    (args.out / "actual-tables.inc").write_text("\n".join(output) + "\n")
    manifest["source"] = hashlib.sha256(source.read_bytes()).hexdigest()
    manifest["dependency_source"] = hashlib.sha256(dependency.read_bytes()).hexdigest()
    manifest["elf"] = hashlib.sha256(data).hexdigest()
    manifest["scope"] = "actual source selector and byte-only stat/move/type tables; no ROM/media exports"
    (args.out / "fixture-provenance.json").write_text(json.dumps(manifest, indent=2) + "\n")
    disassembly = subprocess.check_output(
        ["mips-linux-gnu-objdump", "-d", str(args.elf)], text=True)
    calls, current = {}, None
    for line in disassembly.splitlines():
        match = re.match(r"[0-9a-f]+ <([^>]+)>:", line)
        if match:
            current = match[1]
            calls.setdefault(current, set())
        match = re.search(r"\b(?:jal|j)\s+[0-9a-f]+\s+<([^>+]+)", line)
        if current and match:
            calls[current].add(match[1])
    closure = set()

    def visit(name):
        if name in closure:
            return
        closure.add(name)
        for target in calls.get(name, []):
            visit(target)

    visit("func_84383B5C")
    paths = [source, dependency]
    definitions = {}
    for path in paths:
        body = path.read_text()
        for name in re.findall(r"(?m)^[\w *]+\b(func_[0-9A-Fa-f]+)\([^;{}]*?\)\s*\{", body):
            definitions[name] = function(body, name)
    local = sorted(closure.intersection(definitions))
    transform_score = definitions["func_8437B0CC"]
    for index, expression in [
            (0, "1 - D_843C5568[0].unk_000"),
            (1, "D_843C5568[0].unk_000")]:
        before = f"D_84390010[{index}]->unk_654.unk_BE"
        if transform_score.count(before) != 1:
            raise ValueError("unexpected Transform physical-side dependency")
        transform_score = transform_score.replace(
            before, f"D_84390010[{expression}]->unk_654.unk_BE")
    definitions["func_8437B0CC"] = transform_score
    macros = ["#define " + name + " " +
              ("sc_ai_owned_select" if name == "func_84383B5C" else "sc_owned_" + name[5:])
              for name in local]
    declarations = []
    for line in text[:text.index("static u8 D_8438AFB4")].splitlines():
        if re.match(r"(?:unk_\w+|u\d+|s\d+)\s*\*?\s+.*D_\w+.*;", line):
            declarations.append("extern " + re.sub(r"\s*=\s*[^;]+", "", line))
    tables = text[text.index("static u8 D_8438AFB4"):text.index("void func_84376790")]
    bodies = [owned if name == "func_84383B5C" else definitions[name] for name in local]
    (args.out / "owned-closure.c").write_text(
        "\n".join(macros) + '\n#include "src/fragments/62/fragment62.h"\n' +
        '#include "src/22630.h"\n#include "include/math.h"\n' +
        "\n".join(declarations) + "\n" + tables + "\n" + "\n\n".join(bodies) + "\n")
    (args.out / "closure-import-scope.json").write_text(
        json.dumps(dict(closure=sorted(closure), generated=local,
                        imported_functions=sorted(closure - set(local))), indent=2) + "\n")
    imports = ["PROVIDE(" + name + " = 0x" + format(value, "x") + ");"
               for name, value in sorted(symbols.items())
               if re.fullmatch(r"[A-Za-z_]\w*", name)]
    (args.out / "native-imports.ld").write_text("\n".join(imports) + "\n")
    linker = """OUTPUT_ARCH(mips)
SECTIONS {
 .fragmentAI 0x8F900000 : {
  *sc_ai_header.o(.text)
  *(.text*) *(.data*) *(.rodata*) . = ALIGN(16);
 }
 fragmentAI_HDR_RELOC_OFFSET = SIZEOF(.fragmentAI);
 .fragmentAI_relocs 0 : { *(.ai_loader_table) }
 .fragmentAI_bss 0x8F900000 + SIZEOF(.fragmentAI) (NOLOAD) : {
  *(.bss*) *(COMMON) . = ALIGN(16);
 }
 fragmentAI_HDR_SIZE_IN_ROM = SIZEOF(.fragmentAI) + SIZEOF(.fragmentAI_relocs);
 fragmentAI_HDR_SIZE_IN_RAM = SIZEOF(.fragmentAI) + SIZEOF(.fragmentAI_bss);
 /DISCARD/ : { *(.reginfo) *(.MIPS.abiflags) *(.mdebug) *(.pdr) *(.comment) }
}
"""
    (args.out / "ai.ld").write_text(linker)
    host_header = Path("src/fragments/62/fragment62.h").read_text()
    replacements = {
        "u8 unk_05[1];\n    /* 0x0C */ char unk06[0xC];":
            "u8 unk_05[7];\n    /* 0x0C */ char unk06[6];",
        "u8 unk_07[1];\n    /* 0x08 */ u8 unk_08;":
            "union { u8 unk_07[2]; struct { u8 host_pad07; u8 unk_08; }; };",
        "u8 unk_0A[1];\n    /* 0x0B */ char unk0B[0x5];": "u8 unk_0A[6];",
        "unk_D_843C60AC unk_14[1];": "unk_D_843C60AC unk_14[6];",
        "u8 unk_00C[1];\n    /* 0x00D */ char unk00D[0x5];": "u8 unk_00C[6];",
        "unk_D_843C60AC unk_016[1];\n    /* 0x08C */ char unk08C[0x510];":
            "unk_D_843C60AC unk_016[6];\n    char host_tail[0x2C2];",
        "u8 unk_00[1];\n    /* 0x01 */ char unk01[9];": "u8 unk_00[10];",
        "u8 unk_0A[1];\n    /* 0x0B */ char unk0B[0x15];": "u8 unk_0A[22];",
        "s32 unk_04[1];\n    /* 0x08 */ char unk08[0xC];": "s32 unk_04[4];",
    }
    for before, after in replacements.items():
        if host_header.count(before) != 1:
            raise ValueError("unexpected native host view: " + before)
        host_header = host_header.replace(before, after)
    (args.out / "host-native-views.h").write_text(host_header)
    stats_header = Path("src/22630.h").read_text()
    before = ("u8 unk_0F[1];\n    /* 0x10 */ char unk10[0x3];\n"
              "    /* 0x13 */ u8 unk_13;\n    /* 0x14 */ char unk14[0x3];")
    after = ("union { u8 unk_0F[8]; struct { u8 host_pad0F[4]; "
             "u8 unk_13; char unk14[3]; }; };")
    if stats_header.count(before) != 2:
        raise ValueError("unexpected species bitmask views")
    (args.out / "host-stats-views.h").write_text(stats_header.replace(before, after))


if __name__ == "__main__":
    main()
