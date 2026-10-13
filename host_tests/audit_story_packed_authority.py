"""Independently decode the sealed packed table against every ELF-derived row."""
import copy
import hashlib
import json
import struct
import sys
from pathlib import Path


def require(condition, message):
    if not condition:
        raise ValueError(message)


def pack(rows):
    result = bytearray()
    for row in rows:
        require(row["kind"] in (2, 4, 5, 6), "kind")
        require(0 <= row["offset"] <= 0xFFFFFF, "packed offset overflow")
        require(type(row["internal"]) is bool, "internal flag")
        require(0 <= row["word"] <= 0xFFFFFFFF, "word overflow")
        result.extend(struct.pack(">II", row["kind"] << 24 | row["offset"] |
                                  int(row["internal"]) << 31, row["word"]))
    return bytes(result)


def decode(blob, rows, image, payload, ram):
    require(len(blob) == len(rows) * 8, "record length")
    highs, used, locations = {}, set(), set()
    decoded = []
    for index, row in enumerate(rows):
        descriptor, word = struct.unpack_from(">II", blob, index * 8)
        kind, offset, internal = descriptor >> 24 & 0x7F, descriptor & 0xFFFFFF, bool(descriptor >> 31)
        require(kind in (2, 4, 5, 6), "packed kind")
        require(offset % 4 == 0 and offset <= payload - 4 and
                (offset == 0 or offset >= 32), "packed location")
        require((kind, offset, word, internal) ==
                (row["kind"], row["offset"], row["word"], row["internal"]),
                "record mismatch")
        require(struct.unpack_from(">I", image, offset)[0] == word, "instruction mismatch")
        opcode = word >> 26
        if kind == 5:
            require(opcode == 15, "HI instruction")
            reg = word >> 16 & 31
            highs[reg] = (word & 65535, offset, row["symbol"], internal)
            locations.add(offset)
            target = 0
        elif kind == 2:
            target = word
        elif kind == 4:
            require(opcode in (2, 3), "jump instruction")
            target = 0x80000000 | ((word & 0x03FFFFFF) << 2)
        else:
            require(opcode in (9, 32, 33, 35, 36, 37, 40, 41, 43, 49, 53, 57, 61),
                    "LO instruction")
            reg = word >> 21 & 31
            require(reg in highs, "LO register context")
            high, location, symbol, high_internal = highs[reg]
            require(symbol == row["symbol"], "HI/LO symbol context")
            require(high_internal == internal, "HI/LO internal context")
            low = struct.unpack(">h", struct.pack(">H", word & 65535))[0]
            target = ((high << 16) + low) & 0xFFFFFFFF
            used.add(location)
        require(target == row["target"], "exact target")
        if internal and kind != 5:
            require(0x8F900000 <= target < 0x8F900000 + ram, "internal extent")
        decoded.append((kind, offset, word, target, int(internal)))
    require(used == locations, "unused HI context")
    return decoded


def audit(directory):
    manifest = json.loads((directory / "authority.json").read_text())
    rows = json.loads((directory / "authority-records.json").read_text())
    blob = (directory / "authority-packed.bin").read_bytes()
    image = (directory / "image.bin").read_bytes()
    unpacked = (directory / "authority-unpacked.bin").read_bytes()
    require(len(rows) == manifest["relocations"], "relocation count")
    for name, key in (("authority-records.json", "record_reference_sha256"),
                      ("authority-packed.bin", "packed_table_sha256"),
                      ("authority-unpacked.bin", "unpacked_table_sha256"),
                      ("image.bin", "image_sha256"),
                      ("retail-reference.bin", "reference_relocated_sha256")):
        require(hashlib.sha256((directory / name).read_bytes()).hexdigest() == manifest[key],
                "sealed hash " + name)
    require(pack(rows) == blob, "packed round trip")
    decoded = decode(blob, rows, image, manifest["payload"], manifest["ram"])
    require(b"".join(struct.pack(">5I", *row) for row in decoded) == unpacked,
            "unpacked reference equality")
    require(len(manifest["imports"]) == 40, "import count")
    for row in rows:
        if not row["internal"] and row["kind"] != 5:
            binding = manifest["imports"][row["symbol"]]
            require(binding["value"] == row["symbol_value"] and row["target"] in binding["targets"],
                    "import target identity")

    faults = []

    def refused(name, action):
        try:
            action()
        except (ValueError, struct.error):
            faults.append(name)
        else:
            raise AssertionError("malformed authority accepted: " + name)

    def changed_descriptor(index, value):
        altered = bytearray(blob)
        struct.pack_into(">I", altered, index * 8, value)
        return altered

    refused("short record", lambda: decode(blob[:-1], rows, image, manifest["payload"], manifest["ram"]))
    refused("invalid packed kind", lambda: decode(changed_descriptor(0, 3 << 24), rows, image,
                                                  manifest["payload"], manifest["ram"]))
    refused("unaligned packed offset", lambda: decode(changed_descriptor(0, 2 << 24 | 1), rows,
                                                       image, manifest["payload"], manifest["ram"]))
    refused("outside payload", lambda: decode(changed_descriptor(0, 2 << 24 | 0xFFFFFC), rows,
                                              image, manifest["payload"], manifest["ram"]))
    for field, value in (("offset", 0x1000000), ("word", 0x100000000), ("internal", 2)):
        altered = copy.deepcopy(rows)
        altered[0][field] = value
        refused(field + " overflow", lambda altered=altered: pack(altered))
    lo_index = next(i for i, row in enumerate(rows) if row["kind"] == 6)
    altered = copy.deepcopy(rows)
    altered[lo_index]["symbol"] += "_foreign"
    refused("HI/LO symbol", lambda: decode(blob, altered, image, manifest["payload"], manifest["ram"]))
    row = rows[lo_index]
    refused("missing HI register context",
            lambda: decode(pack([row]), [row], image, manifest["payload"], manifest["ram"]))
    hi = next(row for row in rows if row["kind"] == 5)
    refused("unused HI context",
            lambda: decode(pack([hi]), [hi], image, manifest["payload"], manifest["ram"]))
    report = {"records": len(decoded), "exact_unpacked_equality": True,
              "ordered_context": True, "malformed_refusals": faults,
              "authority_sha256": hashlib.sha256((directory / "authority.json").read_bytes()).hexdigest(),
              "scope": "metadata decoding; runtime relocated-byte equality is a separate C proof"}
    (directory / "packed-audit.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"PACKED AUTHORITY PASSED: {len(decoded)} exact records; {len(faults)} malformed refusals")


if __name__ == "__main__":
    audit(Path(sys.argv[1]))
