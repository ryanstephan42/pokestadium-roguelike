"""Derive section rights from exact IDO inputs and their retained linker map."""
import hashlib
import json
import re
import struct
import sys
from pathlib import Path


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def sections(path):
    data = path.read_bytes()
    assert data[:6] == b"\x7fELF\x01\x02", path
    start = struct.unpack_from(">I", data, 32)[0]
    stride, count, names_index = struct.unpack_from(">HHH", data, 46)
    records = [struct.unpack_from(">10I", data, start + i * stride)
               for i in range(count)]
    names_record = records[names_index]
    names = data[names_record[4]:names_record[4] + names_record[5]]
    return {names[r[0]:names.index(0, r[0])].decode(): r for r in records}


source, out = map(Path, sys.argv[1:3])
native = len(sys.argv) == 4 and sys.argv[3] == "--native"
assert len(sys.argv) == 3 or native
out.mkdir(parents=True, exist_ok=True)
elf = source / ("pokestadium-rl.elf" if native else "corrected.elf")
map_path = source / ("pokestadium-rl.map" if native else "corrected.map")
if not native:
    assert sha(elf) == "b93fb8cd2add3af4089b97534684610e90184d9c65b03e4898f7bea5414fee0c"
sec = sections(elf)
base, payload = sec[".fragmentAI"][3], sec[".fragmentAI"][5]
bss = sec[".fragmentAI_bss"]
assert bss[3] == base + payload and bss[1] == 8
ram = payload + bss[5]
placements = []
inputs = {}
output_section = None
for line in map_path.read_text().splitlines():
    if re.match(r"^\.\S+", line):
        output_section = line.split()[0]
    if native and output_section not in (".fragmentAI", ".fragmentAI_bss"):
        continue
    match = re.match(r" (\.\S+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+\.o)$", line)
    if not match:
        continue
    name, address, size, original = match.groups()
    address, size = int(address, 16), int(size, 16)
    if not size or not base <= address < base + ram:
        continue
    path = Path(original) if native else source / Path(original).name
    record = sections(path)[name]
    assert record[5] == size and record[2] & 2
    assert address % 16 == 0 and size % 16 == 0
    assert name in [".text", ".data", ".rodata", ".bss"]
    rights = 1 | (2 if record[2] & 1 else 0) | (4 if record[2] & 4 else 0)
    assert not (rights & 2 and rights & 4)
    inputs[str(path)] = sha(path)
    placements.append({"section": name, "offset": address - base,
                       "size": size, "rights": rights, "input": str(path)})
assert placements
spans = []
cursor = 0
ordered = sorted(placements, key=lambda p: p["offset"])
padding = []
for item in ordered:
    if native and item["offset"] > cursor:
        gap = item["offset"] - cursor
        assert spans and spans[-1]["rights"] == 5 and not (item["rights"] & 4)
        assert gap == 16 and cursor % 32 == 16 and item["offset"] % 32 == 0
        start = sec[".fragmentAI"][4] + cursor
        assert elf.read_bytes()[start:start + gap] == bytes(gap)
        padding.append({"section": ".text-align", "offset": cursor,
                        "size": gap, "rights": 5, "input": "linker ALIGN(32) zero fill"})
        spans[-1]["size"] += gap
        cursor += gap
    assert item["offset"] == cursor, item
    if spans and spans[-1]["rights"] == item["rights"]:
        spans[-1]["size"] += item["size"]
    else:
        spans.append({k: item[k] for k in ["offset", "size", "rights"]})
    cursor += item["size"]
assert cursor == ram
assert len(spans) <= 8
assert all(not (s["rights"] & 4) or not ((s["offset"] | s["size"]) & 31)
           for s in spans)
assert sum(p["size"] for p in placements if p["section"] == ".bss") == bss[5]
metadata = {
    "elf_sha256": sha(elf), "map_sha256": sha(map_path),
    "input_objects": inputs, "base": base, "payload": payload, "ram": ram,
    "data_cache_line": 16, "instruction_cache_line": 32,
    "spans": spans, "input_placements": placements, "alignment_fill": padding,
    "relocation_metadata_bytes": sec[".fragmentAI_relocs"][5],
    "scope": "exact accepted IDO layout; no runtime execution",
}
(out / "module-spans.json").write_text(json.dumps(metadata, indent=2) + "\n")
header = '#include "src/story/sc_module_arena.h"\n'
header += f"#define SC_IMAGE_PAYLOAD {payload}U\n#define SC_IMAGE_RAM {ram}U\n"
header += f"#define SC_IMAGE_SPAN_COUNT {len(spans)}U\n"
header += "static const ScMaSpan sc_image_spans[] = {\n"
header += "".join(f"    {{{s['offset']}U, {s['size']}U, {s['rights']}U}},\n" for s in spans)
header += "};\n"
(out / "module-spans.h").write_text(header)
print("Exact module spans:", spans)
