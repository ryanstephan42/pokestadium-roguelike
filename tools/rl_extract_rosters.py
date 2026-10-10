#!/usr/bin/env python3
"""Extract Pokemon Stadium (US) trainer rosters and rental pools for the roguelike.

Reads the "Trainer data" BinArchive at ROM 0x898000 (yamls/us/rom.yaml late_assets_3,
loaded in-game by func_8002C128 / func_800044F4(0x898000, ...)) and writes:

    <out>/rl_rosters.json   full data, for tools and tests
    <out>/rl_rosters.h      compact C tables for the roguelike build

The output is copyrighted game data: it is generated at build time into build/
(git-ignored) and must never be committed. See docs/roguelike/re-notes/battle-entry.md.

Usage: tools/rl_extract_rosters.py [--rom baseroms/us/baserom.z64] [--out build/rl_gen]
"""

import argparse
import json
import os
import struct
import sys

ARCHIVE_ROM = 0x898000
TRAINER_STRIDE = 0x230
MON_SIZE = 0x54
ROUND2_OFFSET = 0x1F

# Archive file index -> (kind, id, label, extra). Indices are for round 1; round 2
# ("R2", D_800AE540.unk_11F2 != 0) uses index + 0x1F with the same meaning.
FILES = {
    0: ("cup", "petit", "Petit Cup", {"mode": 4}),
    1: ("cup", "pika", "Pika Cup", {"mode": 5}),
    2: ("cup", "prime_poke", "Prime Cup Poke Ball", {"mode": 6, "sub": 0}),
    3: ("cup", "prime_great", "Prime Cup Great Ball", {"mode": 6, "sub": 1}),
    4: ("cup", "prime_ultra", "Prime Cup Ultra Ball", {"mode": 6, "sub": 2}),
    5: ("cup", "prime_master", "Prime Cup Master Ball", {"mode": 6, "sub": 3}),
    6: ("cup", "mode1", "Mode 1 (alias of Poke Cup Poke Ball data)", {"mode": 1}),
    7: ("cup", "mode2", "Mode 2 (alias of Poke Cup Great Ball data)", {"mode": 2}),
    8: ("cup", "poke_poke", "Poke Cup Poke Ball", {"mode": 3, "sub": 0}),
    9: ("cup", "poke_great", "Poke Cup Great Ball", {"mode": 3, "sub": 1}),
    10: ("cup", "poke_ultra", "Poke Cup Ultra Ball", {"mode": 3, "sub": 2}),
    11: ("cup", "poke_master", "Poke Cup Master Ball", {"mode": 3, "sub": 3}),
    12: ("glc", "pewter", "Gym Leader Castle: Pewter (Brock)", {"mode": 7, "sub": 0}),
    13: ("glc", "cerulean", "Gym Leader Castle: Cerulean (Misty)", {"mode": 7, "sub": 1}),
    14: ("glc", "vermilion", "Gym Leader Castle: Vermilion (Lt. Surge)", {"mode": 7, "sub": 2}),
    15: ("glc", "celadon", "Gym Leader Castle: Celadon (Erika)", {"mode": 7, "sub": 3}),
    16: ("glc", "fuchsia", "Gym Leader Castle: Fuchsia (Koga)", {"mode": 7, "sub": 4}),
    17: ("glc", "saffron", "Gym Leader Castle: Saffron (Sabrina)", {"mode": 7, "sub": 5}),
    18: ("glc", "cinnabar", "Gym Leader Castle: Cinnabar (Blaine)", {"mode": 7, "sub": 6}),
    19: ("glc", "viridian", "Gym Leader Castle: Viridian (Giovanni)", {"mode": 7, "sub": 7}),
    20: ("glc", "elite_four", "Elite Four", {"mode": 7, "sub": 8}),
    21: ("glc", "rival", "Rival (variant chosen by D_800AE540.unk_11F0)", {"mode": 7, "sub": 9}),
    22: ("boss", "mewtwo", "Mewtwo", {"mode": 8}),
    23: ("rental", "petit", "Rentals: Petit Cup", {"rules": 4}),
    24: ("rental", "pika", "Rentals: Pika Cup", {"rules": 5}),
    25: ("rental", "prime", "Rentals: Prime Cup / Mewtwo", {"rules": [6, 8]}),
    26: ("rental", "poke", "Rentals: Poke Cup / Free Battle", {"rules": [0, 1]}),
    27: ("rental", "poke_rules2", "Rentals: rules 2 (same data as 26)", {"rules": 2}),
    28: ("rental", "poke_rules3", "Rentals: rules 3 (same data as 26)", {"rules": 3}),
    29: ("battle_now", "battle_now", "Battle Now! teams", {"mode": 9}),
    30: ("rental", "glc", "Rentals: Gym Leader Castle", {"rules": 7}),
}

# Stadium text encoding is ASCII plus a few glyphs; these two appear in trainer names.
GLYPHS = {0xA9: "\u2642", 0xBE: "\u2640"}


def decode_name(raw):
    raw = raw.split(b"\0")[0]
    return "".join(GLYPHS.get(b, chr(b)) if (b >= 0x20 and b != 0x7F) else "" for b in raw)


def yay0_decompress(src):
    if len(src) < 16 or src[:4] != b"Yay0":
        raise ValueError("not Yay0")
    size, link_off, chunk_off = struct.unpack(">III", src[4:16])
    if size > 0x2000000 or not (16 <= link_off <= chunk_off <= len(src)):
        raise ValueError("invalid Yay0 sizes/stream offsets")
    dst = bytearray()
    mask_off = 16
    mask = bits = 0
    while len(dst) < size:
        if bits == 0:
            mask = struct.unpack(">I", src[mask_off:mask_off + 4])[0]
            mask_off += 4
            bits = 32
        if mask & 0x80000000:
            dst.append(src[chunk_off])
            chunk_off += 1
        else:
            link = struct.unpack(">H", src[link_off:link_off + 2])[0]
            link_off += 2
            dist = (link & 0xFFF) + 1
            n = link >> 12
            if n == 0:
                n = src[chunk_off] + 0x12
                chunk_off += 1
            else:
                n += 2
            if dist > len(dst) or len(dst) + n > size:
                raise ValueError("invalid Yay0 back-reference")
            for _ in range(n):
                dst.append(dst[-dist])
        mask = (mask << 1) & 0xFFFFFFFF
        bits -= 1
    return bytes(dst)


def unpack_file(data):
    """Return raw file contents; handles PERS-SZP like func_80003890 (relocs + bss)."""
    if data[:8] != b"PERS-SZP":
        return data
    hdr_size, dsize1, dsize2, n_relocs = struct.unpack(">IIII", data[8:24])
    if not (24 + n_relocs * 8 <= hdr_size <= len(data)) or not (dsize1 <= dsize2 <= 0x2000000):
        raise ValueError("invalid PERS-SZP header")
    out = bytearray(yay0_decompress(data[hdr_size:]))
    if len(out) != dsize1:
        raise ValueError("PERS-SZP decompressed size mismatch")
    out += bytes(dsize2 - dsize1)
    rel = 24
    for _ in range(n_relocs):
        value, off = struct.unpack(">II", data[rel:rel + 8])
        if off % 4 or off + 4 > dsize2:
            raise ValueError("invalid PERS-SZP relocation")
        out[off:off + 4] = struct.pack(">I", value)
        rel += 8
    out[dsize1:] = bytes(dsize2 - dsize1)
    return bytes(out)


def read_archive(rom):
    flags, _, _, total, count = struct.unpack(">HHIII", rom[ARCHIVE_ROM:ARCHIVE_ROM + 16])
    if flags != 0 or count != 62 or total < 16 + count * 16 or ARCHIVE_ROM + total > len(rom):
        raise ValueError("not the Stadium 1 US roster archive")
    files = []
    for i in range(count):
        e = ARCHIVE_ROM + 16 + i * 16
        off, size = struct.unpack(">II", rom[e:e + 8])
        if off < 16 + count * 16 or off + size > total:
            raise ValueError("archive entry %d is out of bounds" % i)
        files.append(unpack_file(rom[ARCHIVE_ROM + off:ARCHIVE_ROM + off + size]))
    return files


def parse_mon(b):
    """Decode one 0x54 battle-format mon (struct unk_func_80026268_arg0, src/29BA0.h)."""
    if len(b) != MON_SIZE or not 1 <= b[0] <= 151 or not 1 <= b[0x24] <= 100:
        raise ValueError("invalid Stadium mon record")
    if any(move > 165 for move in b[9:13]):
        raise ValueError("invalid Stadium move id")
    u16 = lambda o: struct.unpack(">H", b[o:o + 2])[0]
    return {
        "species": b[0x00],
        "gb_species": b[0x01],
        "hp": u16(0x02),
        "status": b[0x05],
        "types": [b[0x06], b[0x07]],
        "item": b[0x08],
        "moves": list(b[0x09:0x0D]),
        "ot_id": u16(0x0E),
        "exp": struct.unpack(">I", b[0x10:0x14])[0],
        "stat_exp": [u16(0x14), u16(0x16), u16(0x18), u16(0x1A), u16(0x1C)],
        "dvs": u16(0x1E),
        "pp": [x & 0x3F for x in b[0x20:0x24]],
        "pp_ups": [x >> 6 for x in b[0x20:0x24]],
        "level": b[0x24],
        "stats": [u16(0x26), u16(0x28), u16(0x2A), u16(0x2C), u16(0x2E)],
        "nickname": decode_name(b[0x30:0x3B]),
        "ot_name": decode_name(b[0x3B:0x46]),
    }


def parse_trainer_file(data):
    count = struct.unpack(">I", data[:4])[0]
    if count == 0 or 4 + count * TRAINER_STRIDE > len(data):
        raise ValueError("invalid trainer file count")
    trainers = []
    for t in range(count):
        base = 4 + t * TRAINER_STRIDE
        rec = data[base:base + TRAINER_STRIDE]
        tid, n = struct.unpack(">HH", rec[0x34:0x38])
        if not 1 <= n <= 6:
            raise ValueError("invalid trainer team count")
        trainers.append({
            "index": t,
            "name": decode_name(rec[0x00:0x0C]),
            "class": decode_name(rec[0x0C:0x20]),
            "portrait": tid >> 8,
            "ai": tid & 0xFF,
            "mons": [parse_mon(rec[0x38 + k * MON_SIZE:0x38 + (k + 1) * MON_SIZE]) for k in range(n)],
        })
    return trainers


def parse_rental_file(data):
    count = struct.unpack(">I", data[:4])[0]
    if not 1 <= count <= 151 or 4 + count * MON_SIZE > len(data):
        raise ValueError("invalid rental file count")
    return [parse_mon(data[4 + k * MON_SIZE:4 + (k + 1) * MON_SIZE]) for k in range(count)]


def extract(rom):
    files = read_archive(rom)
    out = {"source": "baserom us, archive 0x%X" % ARCHIVE_ROM, "num_files": len(files),
           "trainer_sets": [], "rental_sets": []}
    for rnd, base in ((1, 0), (2, ROUND2_OFFSET)):
        for idx, (kind, ident, label, extra) in sorted(FILES.items()):
            fi = base + idx
            if fi >= len(files):
                continue
            entry = {"id": ident if rnd == 1 else ident + "_r2", "round": rnd, "file": fi,
                     "kind": kind, "label": label}
            entry.update(extra)
            if kind == "rental":
                entry["mons"] = parse_rental_file(files[fi])
                out["rental_sets"].append(entry)
            else:
                entry["trainers"] = parse_trainer_file(files[fi])
                out["trainer_sets"].append(entry)
    return out


def c_ident(s):
    return "".join(ch if ch.isalnum() else "_" for ch in s).upper()


def c_str(s):
    return '"' + "".join(c if (c.isascii() and c.isprintable() and c not in '"\\') else "\\%03o" % (
        0xA9 if c == "\u2642" else 0xBE if c == "\u2640" else ord("?")) for c in s) + '"'


def c_mon(m):
    return "{ %d, %d, { %s }, { %s }, 0x%04X, { %s } }" % (
        m["species"], m["level"], ", ".join(str(x) for x in m["moves"]),
        ", ".join(str(x) for x in m["pp_ups"]), m["dvs"],
        ", ".join("0x%04X" % x for x in m["stat_exp"]))


def write_c(data, path):
    L = []
    L.append("/* GENERATED by tools/rl_extract_rosters.py from the baserom. DO NOT COMMIT. */")
    L.append("#ifndef RL_ROSTERS_GEN_H\n#define RL_ROSTERS_GEN_H\n")
    L.append("typedef struct RlGenMon {\n    unsigned char species; /* National Dex */\n"
             "    unsigned char level;\n    unsigned char moves[4];\n    unsigned char pp_ups[4];\n"
             "    unsigned short dvs;\n"
             "    unsigned short stat_exp[5];\n} RlGenMon;\n")
    L.append("typedef struct RlGenTrainer {\n    const char* name;\n    const char* trainer_class;\n"
             "    unsigned char portrait;\n    unsigned char ai;\n    unsigned char count;\n"
             "    RlGenMon mons[6];\n} RlGenTrainer;\n")
    L.append("typedef struct RlGenTrainerSet {\n    const char* id;\n    unsigned char round;\n"
             "    unsigned char count;\n    const RlGenTrainer* trainers;\n} RlGenTrainerSet;\n")
    L.append("typedef struct RlGenRentalSet {\n    const char* id;\n    unsigned char round;\n"
             "    unsigned short count;\n    const RlGenMon* mons;\n} RlGenRentalSet;\n")
    L.append("#ifdef RL_ROSTERS_DEFINE\n")
    for s in data["trainer_sets"]:
        L.append("static const RlGenTrainer RL_TRAINERS_%s[%d] = {" % (c_ident(s["id"]), len(s["trainers"])))
        for t in s["trainers"]:
            mons = ", ".join(c_mon(m) for m in t["mons"]) or "{ 0 }"
            L.append("    { %s, %s, %d, %d, %d, { %s } }," % (
                c_str(t["name"]), c_str(t["class"]), t["portrait"], t["ai"], len(t["mons"]), mons))
        L.append("};")
    for s in data["rental_sets"]:
        L.append("static const RlGenMon RL_RENTALS_%s[%d] = {" % (c_ident(s["id"]), len(s["mons"])))
        for m in s["mons"]:
            L.append("    %s," % c_mon(m))
        L.append("};")
    L.append("\nconst RlGenTrainerSet gRlTrainerSets[%d] = {" % len(data["trainer_sets"]))
    for s in data["trainer_sets"]:
        L.append("    { %s, %d, %d, RL_TRAINERS_%s }," % (c_str(s["id"]), s["round"], len(s["trainers"]),
                                                          c_ident(s["id"])))
    L.append("};")
    L.append("const RlGenRentalSet gRlRentalSets[%d] = {" % len(data["rental_sets"]))
    for s in data["rental_sets"]:
        L.append("    { %s, %d, %d, RL_RENTALS_%s }," % (c_str(s["id"]), s["round"], len(s["mons"]),
                                                         c_ident(s["id"])))
    L.append("};")
    L.append("\n#else\n")
    L.append("extern const RlGenTrainerSet gRlTrainerSets[%d];" % len(data["trainer_sets"]))
    L.append("extern const RlGenRentalSet gRlRentalSets[%d];" % len(data["rental_sets"]))
    L.append("\n#endif /* RL_ROSTERS_DEFINE */\n")
    L.append("#define RL_NUM_TRAINER_SETS %d" % len(data["trainer_sets"]))
    L.append("#define RL_NUM_RENTAL_SETS %d" % len(data["rental_sets"]))
    for i, s in enumerate(data["trainer_sets"]):
        L.append("#define RL_TRAINER_SET_%s %d" % (c_ident(s["id"]), i))
    for i, s in enumerate(data["rental_sets"]):
        L.append("#define RL_RENTAL_SET_%s %d" % (c_ident(s["id"]), i))
    L.append("\n#endif /* RL_ROSTERS_GEN_H */")
    with open(path, "w") as f:
        f.write("\n".join(L) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--rom", default="baseroms/us/baserom.z64")
    ap.add_argument("--out", default="build/rl_gen")
    ap.add_argument("--summary", action="store_true", help="print a per-set summary")
    args = ap.parse_args()

    with open(args.rom, "rb") as f:
        rom = f.read()
    if rom[:4] != b"\x80\x37\x12\x40" or rom[0x3B:0x3F] != b"NPOE":
        sys.exit("error: %s is not a big-endian .z64 ROM" % args.rom)

    data = extract(rom)
    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, "rl_rosters.json"), "w") as f:
        json.dump(data, f, indent=1, ensure_ascii=False)
    write_c(data, os.path.join(args.out, "rl_rosters.h"))

    if args.summary:
        for s in data["trainer_sets"]:
            lv = [m["level"] for t in s["trainers"] for m in t["mons"]]
            print("%-14s file %2d: %d trainers, lv %d-%d: %s" % (
                s["id"], s["file"], len(s["trainers"]), min(lv), max(lv),
                ", ".join(t["name"] for t in s["trainers"])))
        for s in data["rental_sets"]:
            lv = [m["level"] for m in s["mons"]]
            print("%-14s file %2d: %d rentals, lv %d-%d" % (s["id"], s["file"], len(s["mons"]), min(lv), max(lv)))
    print("wrote %s/rl_rosters.{json,h}: %d trainer sets, %d rental sets" % (
        args.out, len(data["trainer_sets"]), len(data["rental_sets"])))


if __name__ == "__main__":
    main()
