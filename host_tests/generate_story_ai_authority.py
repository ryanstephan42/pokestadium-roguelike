"""Generate exact authority for historical or explicitly supplied isolated ID233."""
import hashlib
import json
import struct
import sys
import zlib
from pathlib import Path

OUT = Path(sys.argv[1])
OUT.mkdir(parents=True, exist_ok=True)
BASE = Path(".local/rltest/story-six-selector-prototype-stage2-fit-blocked")
assert len(sys.argv) in (2, 3, 4)
native_current = len(sys.argv) == 4 and sys.argv[3] == "--native"
assert len(sys.argv) != 4 or native_current
corrected = len(sys.argv) >= 3
ELF = Path(sys.argv[2]) if corrected else BASE / "pokestadium-rl.elf"
data = ELF.read_bytes()
if not native_current:
    baseline = (BASE / "pokestadium-rl.elf").read_bytes()
    assert hashlib.sha256(baseline).hexdigest() == (
        "835b530f2ed160165637962c65d9ace75e92b9497bb165d32cd2d222bd022931")
    if not corrected:
        assert data == baseline
off = struct.unpack_from(">I", data, 32)[0]
stride, count, name_index = struct.unpack_from(">HHH", data, 46)
sections = [struct.unpack_from(">10I", data, off + i * stride) for i in range(count)]
names_section = sections[name_index]
names = data[names_section[4]:names_section[4] + names_section[5]]


def string(blob, start):
    return blob[start:blob.index(0, start)].decode()


indexed = {string(names, s[0]): (i, s) for i, s in enumerate(sections)}
target_index, target = indexed[".fragmentAI"]
bss_index, bss = indexed[".fragmentAI_bss"]
_, table = indexed[".fragmentAI_relocs"]
_, rel = indexed[".rel.fragmentAI"]
symtab = sections[rel[6]]
strings_section = sections[symtab[6]]
strings = data[strings_section[4]:strings_section[4] + strings_section[5]]
symbols = [struct.unpack_from(">IIIBBH", data, p)
           for p in range(symtab[4], symtab[4] + symtab[5], 16)]
by_name = {string(strings, s[0]): s for s in symbols if s[0]}
assert target[3] == 0x8F900000
assert bss[3] == target[3] + target[5]
if not corrected:
    assert bss[5] == 5984
payload = data[target[4]:target[4] + target[5]]
packed = data[table[4]:table[4] + table[5]]
image = payload + packed
payload_size, rom_size, ram_size = target[5], len(image), target[5] + bss[5]
assert struct.unpack_from(">4I", payload, 16) == (32, payload_size, rom_size, ram_size)
assert payload[8:16] == b"FRAGMENT"
rom_start = by_name["fragmentAI_ROM_START"][1]
rom_end = by_name["fragmentAI_relocs_ROM_END"][1]
if not native_current:
    assert rom_start == 0x206A770
assert rom_end - rom_start == len(image)
if not corrected:
    assert (rom_start, rom_end) == (0x206A770, 0x2078A00)
    assert (BASE / "pokestadium-rl.z64").read_bytes()[rom_start:rom_end] == image
approved_path = (Path(__file__).with_name("story_ai_imports.json") if native_current
                 else BASE / "actual-imports.json")
approved_names = json.loads(approved_path.read_text())
assert len(approved_names) == 40 and all(isinstance(name, str) for name in approved_names)
approved = set(approved_names)
assert len(approved) == 40
records = []
for p in range(rel[4], rel[4] + rel[5], 8):
    location, info = struct.unpack_from(">II", data, p)
    kind, index = info & 255, info >> 8
    offset = location - target[3]
    assert kind in (2, 4, 5, 6) and 0 <= offset <= len(payload) - 4
    symbol = symbols[index]
    name = string(strings, symbol[0])
    if 16 <= offset < 32:
        assert name.startswith("fragmentAI_HDR_")
        continue
    assert offset >= 32 or offset == 0
    internal = symbol[5] in (target_index, bss_index)
    assert internal or name in approved, (name, hex(symbol[1]))
    records.append({"kind": kind, "offset": offset, "symbol": name,
                    "symbol_value": symbol[1], "internal": internal,
                    "word": struct.unpack_from(">I", payload, offset)[0]})
packed_count = struct.unpack_from(">I", packed)[0]
assert packed_count == len(records)
assert [r["kind"] << 24 | r["offset"] for r in records] == list(
    struct.unpack_from(f">{packed_count}I", packed, 4))
assert not any(packed[4 + 4 * packed_count:])

# Reference is the retail ordered algorithm, except explicit private mapping.
reference = bytearray(payload + bytes(ram_size - len(payload)))
high = {}
high_locations, used_high_locations = set(), set()
bindings = {}
if corrected and not native_current:
    native_low = by_name["story_native_base"][1]
    native_end = by_name["story_native_end"][1]
else:
    native_low = indexed[".fragment62"][1][3]
    native_end = indexed[".fragment62_bss"][1][3] + indexed[".fragment62_bss"][1][5]


def mapped(value):
    if 0x8F900000 <= value < 0x8F900000 + ram_size:
        return 0x80400000 + value - 0x8F900000
    if native_low <= value < native_end:
        return 0x80200000 + value - native_low
    assert 0x80000000 <= value < 0x81000000, hex(value)
    return value


def put(offset, value):
    struct.pack_into(">I", reference, offset, value & 0xFFFFFFFF)


for r in records:
    word, kind, offset = r["word"], r["kind"], r["offset"]
    if kind == 5:
        assert word >> 26 == 15, r
        high[word >> 16 & 31] = (offset, word, r["symbol"])
        high_locations.add(offset)
        r["target"] = 0
        continue
    if kind == 2:
        value = word
    elif kind == 4:
        assert word >> 26 in (2, 3)
        value = ((word * 4) & 0xFFFFFFC) + 0x80000000
    else:
        reg = word >> 21 & 31
        assert reg in high, r
        hi_offset, hi_word, hi_symbol = high[reg]
        used_high_locations.add(hi_offset)
        assert hi_symbol == r["symbol"], ("HI/LO symbol mismatch", r)
        # Only actual signed-immediate instructions; ORI is not permitted.
        assert word >> 26 in (9, 32, 33, 35, 36, 37, 40, 41, 43, 49, 53, 57, 61), r
        value = ((hi_word << 16) + struct.unpack(">h", struct.pack(">H", word & 65535))[0]) & 0xFFFFFFFF
    r["target"] = value
    if r["internal"]:
        assert 0x8F900000 <= value < 0x8F900000 + ram_size
    else:
        # Compiler bias/addend may precede a symbol; this is address construction,
        # not authorization for a raw load at that intermediate address.
        bindings.setdefault(r["symbol"], {"value": r["symbol_value"], "targets": []})
        bindings[r["symbol"]]["targets"].append(value)
    resolved = mapped(value)
    if kind == 2:
        put(offset, resolved)
    elif kind == 4:
        put(offset, (word & 0xFC000000) | ((resolved & 0xFFFFFFF) >> 2))
    else:
        put(hi_offset, (struct.unpack_from(">I", reference, hi_offset)[0] & 0xFFFF0000) |
            (((resolved >> 16) + bool(resolved & 0x8000)) & 65535))
        put(offset, (word & 0xFFFF0000) | (resolved & 65535))
if corrected and not native_current:
    prior = json.loads(Path(".local/rltest/story-real-image-destination-v2/authority.json").read_text())
    expected_imports = {k: v["value"] for k, v in prior["imports"].items()}
    assert {k: v["value"] for k, v in bindings.items()} == expected_imports
else:
    assert len(bindings) == 40
if native_current:
    assert set(bindings) == approved
    assert all(value["value"] == by_name[name][1] for name, value in bindings.items())
assert high_locations == used_high_locations, "Unmatched HI16 relocation"
manifest = {
    "source_elf_sha256": hashlib.sha256(data).hexdigest(),
    "image_sha256": hashlib.sha256(image).hexdigest(),
    "image_crc32": zlib.crc32(image), "id": 233,
    "rom_start": rom_start, "rom_end": rom_end,
    "payload": payload_size, "ram": ram_size, "relocations": packed_count,
    "noninstalled_corrected_candidate": corrected,
    "native_encoded_base": native_low, "native_encoded_end": native_end,
    "imports": bindings,
    "reference_relocated_sha256": hashlib.sha256(reference).hexdigest(),
    "threat_model": "Trusted linked ROM/compiled manifest; runtime CRC detects accidental corruption, not adversarial authenticity",
}
if native_current:
    manifest["import_policy_sha256"] = hashlib.sha256(approved_path.read_bytes()).hexdigest()
    manifest["generator_sha256"] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
(OUT / "authority-records.json").write_text(json.dumps(records, indent=2) + "\n")
packed_authority = b"".join(struct.pack(
    ">II", r["kind"] << 24 | r["offset"] | int(r["internal"]) << 31, r["word"])
    for r in records)
unpacked_authority = b"".join(struct.pack(
    ">5I", r["kind"], r["offset"], r["word"], r["target"], int(r["internal"]))
    for r in records)
(OUT / "authority-packed.bin").write_bytes(packed_authority)
(OUT / "authority-unpacked.bin").write_bytes(unpacked_authority)
manifest["record_reference_sha256"] = hashlib.sha256(
    (OUT / "authority-records.json").read_bytes()).hexdigest()
manifest["packed_table_sha256"] = hashlib.sha256(packed_authority).hexdigest()
manifest["unpacked_table_sha256"] = hashlib.sha256(unpacked_authority).hexdigest()
manifest["import_relocations"] = [
    {"kind": r["kind"], "offset": r["offset"], "word": r["word"],
     "symbol": r["symbol"], "symbol_value": r["symbol_value"],
     "target": r["target"], "addend": r["target"] - r["symbol_value"]}
    for r in records if not r["internal"] and r["kind"] != 5]
(OUT / "authority.json").write_text(json.dumps(manifest, indent=2) + "\n")
(OUT / "image.bin").write_bytes(image)
(OUT / "retail-reference.bin").write_bytes(reference)
header = [
    "#define AI_AUTHORITY_PACKED 1",
    f"#define AI_PAYLOAD {payload_size}u", f"#define AI_ROM_BYTES {rom_size}u",
    f"#define AI_RAM {ram_size}u", f"#define AI_COUNT {packed_count}u",
    f"#define AI_CRC {zlib.crc32(image)}u",
    f"#define AI_ROM_START {rom_start}u", f"#define AI_ROM_END {rom_end}u",
    f"#define AI_ENTRY_OFFSET {by_name['sc_ai_entry'][1] - 0x8F900000}u",
    f"#define AI_NATIVE_END {native_end}u",
    "static const AiReloc ai_table[AI_COUNT] = {",
]
for r in records:
    descriptor = r["kind"] << 24 | r["offset"] | int(r["internal"]) << 31
    assert descriptor & 0xffffff == r["offset"]
    assert descriptor >> 24 & 0x7f == r["kind"]
    assert descriptor >> 31 == int(r["internal"])
    header.append("{%du, %du}," % (descriptor, r["word"]))
header.append("};")
(OUT / "ai_sizes.h").write_text("\n".join(header[:10]) + "\n")
(OUT / "ai_authority.h").write_text("\n".join(header) + "\n")
print("EXACT AI AUTHORITY:", len(image), "bytes;", len(records), "ordered relocations;",
      len(bindings), "imports")
