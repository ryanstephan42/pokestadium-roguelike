"""Seal current six-party HOST readiness against an existing linked candidate."""

import argparse
import hashlib
import json
import struct
import subprocess
import tempfile
from pathlib import Path

from tools.rltest.frontend import HarnessError, Symbols
from tools.rltest import story_probe

ROOT = story_probe.ROOT
CATALOG_IDENTITY = "cc2fc13257f1e82205661a8fb282cddc0257f3f8a261a48a2650e29c45a4ee83"
BASE_IDENTITY = "42abbcfced4b4e824b8495b0579b0b443d14715001e85d5700588b7a534eb42e"
FILES = ("codec", "sc_data.c", "data.json", "sc_data.o", "checkpoint.sc01",
         "host-proof.jsonl", "note.bin", "state.bin", "precommit.bin",
         "codec-refusals.json", "host-validation.log")
MANIFEST_FIELDS = {
    "schema", "abi", "catalog_identity", "base_identity", "sources", "rom_sha256",
    "map_sha256", "elf_path", "elf_sha256", "catalog_object_path",
    "catalog_object_sha256", "catalog_source_sha256", "catalog_data_sha256", "files", "scope",
}


def require(condition, message):
    if not condition:
        raise HarnessError(message)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def source_paths():
    paths = {str(path.relative_to(ROOT)) for path in (ROOT / "src/story").glob("*.[ch]")}
    paths.update(str(path.relative_to(ROOT)) for path in (ROOT / "src/roguelike").glob("*.h"))
    paths.update(("include/rl_battle.h", "src/22630.h", "src/29BA0.h",
                  "tools/story_data.py", "tools/story_graph.py", "tools/story_stones.py",
                  "tools/rltest/story_six_fixture.c", "tools/rltest/story_six_fixture.h",
                  "tools/rltest/story_ember_fixture.c", "tools/rltest/story_ember_fixture.h",
                  "tools/rltest/story_probe.py", "tools/rltest/story_six_natural.py",
                  "tools/rltest/seal_story_current_six.py", "host_tests/test_story_ember.c",
                  "host_tests/test_story_six.c", "host_tests/run_story_current_reload.sh",
                  "host_tests/test_story_current_reload_fixture.c"))
    return sorted(paths)


class Elf:
    """Only the ELF32/MSB/MIPS structures needed for catalog/constant proof."""

    def __init__(self, path):
        self.data = Path(path).read_bytes()
        require(len(self.data) >= 52 and self.data[:6] == b"\x7fELF\x01\x02",
                "Current six requires ELF32 big-endian candidate/object")
        require(struct.unpack_from(">H", self.data, 18)[0] == 8, "Expected MIPS ELF")
        self.kind = struct.unpack_from(">H", self.data, 16)[0]
        offset = struct.unpack_from(">I", self.data, 32)[0]
        size, count, names = struct.unpack_from(">HHH", self.data, 46)
        require(size == 40 and count and names < count, "Invalid ELF section table")
        self.sections = [struct.unpack(">10I", self.slice(offset + i * size, size))
                         for i in range(count)]
        self.names = [self.string(self.sections[names], section[0]) for section in self.sections]
        self.symbols = {}
        self.tables = {}
        for index, section in enumerate(self.sections):
            if section[1] != 2:
                continue
            require(section[9] == 16 and section[5] % 16 == 0 and section[6] < count,
                    "Invalid ELF symbol table")
            entries = []
            for position in range(0, section[5], 16):
                symbol = struct.unpack(">IIIBBH", self.slice(section[4] + position, 16))
                name = self.string(self.sections[section[6]], symbol[0])
                entries.append(symbol)
                if name and symbol[3] >> 4 in (1, 2) and symbol[5] not in (0, 0xFFF1):
                    require(symbol[5] < count, "Unsupported ELF symbol section")
                    require(name not in self.symbols or self.symbols[name] == symbol,
                            f"Ambiguous ELF symbol: {name}")
                    self.symbols[name] = symbol
            self.tables[index] = entries

    def slice(self, offset, size):
        require(0 <= offset <= len(self.data) and 0 <= size <= len(self.data) - offset,
                "Truncated ELF input")
        return self.data[offset:offset + size]

    def string(self, section, offset):
        raw = self.slice(section[4], section[5])
        require(offset < len(raw) and b"\0" in raw[offset:], "Invalid ELF string offset")
        return raw[offset:raw.index(b"\0", offset)].decode("ascii")

    def symbol(self, name):
        require(name in self.symbols, f"Missing current ELF symbol: {name}")
        return self.symbols[name]

    def read(self, address, size):
        matches = [section for section in self.sections if section[1] == 1 and section[2] & 2
                   and section[3] <= address and address - section[3] + size <= section[5]]
        require(len(matches) == 1, f"ELF address not uniquely file-backed: {address:#x}")
        section = matches[0]
        return self.slice(section[4] + address - section[3], size)

    def rom_read(self, rom, address, size):
        offset = struct.unpack_from(">I", self.data, 28)[0]
        entry_size, count = struct.unpack_from(">HH", self.data, 42)
        require(entry_size == 32, "Invalid ELF program header size")
        locations = []
        for index in range(count):
            segment = struct.unpack(">8I", self.slice(offset + index * entry_size, entry_size))
            kind, _, virtual, physical, length, _, _, _ = segment
            if kind == 1 and virtual <= address and address - virtual + size <= length:
                locations.append(physical + address - virtual)
        require(len(locations) == 1, "Candidate address lacks unique ROM load mapping")
        position = locations[0]
        require(position + size <= len(rom), "Truncated candidate ROM")
        return rom[position:position + size]


def verify_link(elf_path, object_path, rom_path, map_path):
    elf, obj = Elf(elf_path), Elf(object_path)
    require(elf.kind == 2 and obj.kind == 1, "Expected linked ELF and relocatable catalog object")
    rom = Path(rom_path).read_bytes()
    require(rom[:4] == b"\x80\x37\x12\x40", "Expected big-endian N64 ROM")
    symbols = Symbols(map_path)
    abi = {**story_probe.CURRENT_STORY_ABI, **story_probe.CURRENT_SIX_ABI}
    for name, expected in {**abi, "gScProbeSixProfile": 1, "gScProbeKoProfile": 1}.items():
        symbol = elf.symbol(name)
        require(symbol[2] == 4 and symbols.address(name) == symbol[1],
                f"Current map/ELF constant mismatch: {name}")
        raw = elf.read(symbol[1], 4)
        require(struct.unpack(">I", raw)[0] == expected and
                elf.rom_read(rom, symbol[1], 4) == raw,
                f"Current compiled ABI/profile/ROM mismatch: {name}")
    bases = {}
    for name in ("sc_species", "sc_pp", "sc_encounters", "sc_catalog"):
        source, target = obj.symbol(name), elf.symbol(name)
        require(symbols.address(name) == target[1] and source[2] == target[2],
                f"Catalog map/ELF symbol mismatch: {name}")
        base = target[1] - source[1]
        require(source[5] not in bases or bases[source[5]] == base,
                "Inconsistent linked catalog section bases")
        bases[source[5]] = base
    require({obj.names[index] for index in bases} == {".data", ".rodata"},
            "Unsupported catalog object section layout")
    for index, base in bases.items():
        section = obj.sections[index]
        linked = bytearray(obj.slice(section[4], section[5]))
        for relocation in obj.sections:
            if relocation[1] != 9 or relocation[7] != index:
                continue
            require(relocation[9] == 8 and relocation[5] % 8 == 0 and
                    relocation[6] in obj.tables, "Invalid catalog relocation table")
            for position in range(0, relocation[5], 8):
                address, info = struct.unpack(">II", obj.slice(relocation[4] + position, 8))
                require(info & 255 == 2 and address + 4 <= len(linked) and
                        info >> 8 < len(obj.tables[relocation[6]]),
                        "Catalog requires supported R_MIPS_32 relocations only")
                symbol = obj.tables[relocation[6]][info >> 8]
                require(symbol[5] in bases, "Catalog relocation references external storage")
                addend = struct.unpack_from(">I", linked, address)[0]
                struct.pack_into(">I", linked, address, (bases[symbol[5]] + symbol[1] + addend) & 0xFFFFFFFF)
        require(elf.read(base, len(linked)) == linked and
                elf.rom_read(rom, base, len(linked)) == linked,
                f"Catalog object/linked ELF/ROM bytes differ: {obj.names[index]}")
    return abi


def workspace_path(path):
    resolved = Path(path).resolve()
    require(resolved.is_relative_to(ROOT.resolve()), "Candidate input outside current workspace")
    return str(resolved.relative_to(ROOT.resolve()))


def seal(rom, map_path, elf, catalog_object, output=None):
    rom, map_path, elf, catalog_object = (Path(path).resolve()
                                         for path in (rom, map_path, elf, catalog_object))
    for path in (rom, map_path, elf, catalog_object):
        workspace_path(path)
    build = map_path.parent
    require(elf.is_relative_to(build) and catalog_object.is_relative_to(build),
            "Current six ELF/catalog object must belong to the candidate build")
    output = Path(output) if output else build / "story/current-six"
    workspace_path(output)
    require(not output.exists(), "Current six package output must be fresh")
    sources = {name: digest(ROOT / name) for name in source_paths()}
    candidate_paths = (rom, map_path, elf, catalog_object, build / "story/sc_data.c",
                       build / "story/data.json")
    candidate_hashes = {str(path): digest(path) for path in candidate_paths}
    abi = verify_link(elf, catalog_object, rom, map_path)
    with tempfile.TemporaryDirectory(prefix="current-six-", dir=build) as temporary:
        fixture = Path(temporary) / "fixture"
        checked = subprocess.run(["bash", "host_tests/run_story_current_reload.sh", str(fixture)],
                                 cwd=ROOT, capture_output=True, text=True)
        require(checked.returncode == 0,
                f"Current six HOST fixture/codec validation failed:\n{checked.stdout}{checked.stderr}")
        for name in ("sc_data.c", "data.json"):
            require((fixture / name).read_bytes() == (build / "story" / name).read_bytes(),
                    f"Candidate catalog differs from fresh audited Moon fixture: {name}")
        compiled = fixture / "sc_data.o"
        command = ["tools/ido/linux/7.1/cc", "-c", "-O2", "-mips2", "-EB", "-G", "0",
                   "-non_shared", "-I.", str(fixture / "sc_data.c"), "-o", str(compiled)]
        target = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        require(target.returncode == 0, f"Current catalog IDO compilation failed: {target.stderr}")
        verify_link(elf, compiled, rom, map_path)
        # The passed candidate object and fresh source compile must independently
        # reconstruct the same complete linked catalog sections.
        (fixture / "sc_data.o").write_bytes(catalog_object.read_bytes())
        (fixture / "host-validation.log").write_text(checked.stdout + checked.stderr +
                                                    target.stdout + target.stderr)
        require(sources == {name: digest(ROOT / name) for name in source_paths()},
                "Sources changed during current six sealing")
        require(candidate_hashes == {str(path): digest(path) for path in candidate_paths},
                "Candidate changed during current six sealing")
        manifest = dict(schema=1, abi=abi, catalog_identity=CATALOG_IDENTITY,
                        base_identity=BASE_IDENTITY, sources=sources,
                        rom_sha256=digest(rom), map_sha256=digest(map_path),
                        elf_path=workspace_path(elf), elf_sha256=digest(elf),
                        catalog_object_path=workspace_path(catalog_object),
                        catalog_object_sha256=digest(catalog_object),
                        catalog_source_sha256=digest(fixture / "sc_data.c"),
                        catalog_data_sha256=digest(fixture / "data.json"),
                        files={name: digest(fixture / name) for name in FILES},
                        scope="Current six HOST/IDO readiness only; no native execution or fit approval")
        output.mkdir(parents=True)
        for name in FILES:
            (output / name).write_bytes((fixture / name).read_bytes())
            (output / name).chmod(0o555 if name == "codec" else 0o444)
        data = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode()
        (output / "manifest.json").write_bytes(data)
        (output / "manifest.sha256").write_text(hashlib.sha256(data).hexdigest() + "\n")
        (output / "manifest.json").chmod(0o444)
        (output / "manifest.sha256").chmod(0o444)
        output.chmod(0o555)
    story_probe.verify_current_six_package(output, rom, map_path)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("rom", "map", "elf", "catalog-object"):
        parser.add_argument("--" + name, required=True, type=Path)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    try:
        output = seal(args.rom, args.map, args.elf, args.catalog_object, args.out)
    except (HarnessError, OSError, ValueError, KeyError, struct.error) as error:
        parser.exit(1, f"Current six seal refused: {error}\n")
    print(f"Current six readiness sealed: {output}; no emulator/native acceptance")


if __name__ == "__main__":
    main()
