import copy
import hashlib
import json
import struct
import tempfile
import unittest
from types import SimpleNamespace
from pathlib import Path
from unittest.mock import patch

from tools.rltest import seal_story_current_six as seal
from tools.rltest import story_probe
from tools.rltest.frontend import HarnessError


def elf_fixture(linked):
    """Synthetic MIPS load image: not a game or candidate acceptance fixture."""
    rodata = b"species!pp!!"
    data = bytearray(24)
    struct.pack_into(">I", data, 4, 0x80400000 if linked else 0)
    constants = {**story_probe.CURRENT_STORY_ABI, **story_probe.CURRENT_SIX_ABI,
                 "gScProbeSixProfile": 1, "gScProbeKoProfile": 1}
    metadata = b"".join(struct.pack(">I", value) for value in constants.values())
    names = ["", ".strings", ".symtab", ".rodata", ".data", ".metadata", ".rel.data", ".names"]
    strings = bytearray(b"\0")
    entries = [bytes(16)]
    symbols = {}

    def symbol(name, value, size, section, info=0x11):
        offset = len(strings)
        strings.extend(name.encode() + b"\0")
        entries.append(struct.pack(">IIIBBH", offset, value, size, info, 0, section))
        symbols[name] = value

    symbol(".rodata", 0x80400000 if linked else 0, len(rodata), 3, 3)
    symbol(".data", 0x80410000 if linked else 0, len(data), 4, 3)
    for name, offset, size, section, base in (
            ("sc_species", 0, 4, 3, 0x80400000), ("sc_pp", 4, 4, 3, 0x80400000),
            ("sc_encounters", 0, 4, 4, 0x80410000), ("sc_catalog", 4, 20, 4, 0x80410000)):
        symbol(name, base + offset if linked else offset, size, section)
    if linked:
        for index, name in enumerate(constants):
            symbol(name, 0x80004000 + index * 4, 4, 5)
    section_names = b"\0".join(name.encode() for name in names) + b"\0"
    payloads = [b"", bytes(strings), b"".join(entries), rodata, bytes(data),
                metadata if linked else b"", b"" if linked else struct.pack(">II", 4, 0x102),
                section_names]
    image = bytearray(52 + (96 if linked else 0))
    headers, offsets = [], []
    for index, payload in enumerate(payloads):
        while len(image) % 4:
            image.append(0)
        offset = len(image)
        image.extend(payload)
        offsets.append(offset)
        address = {3: 0x80400000, 4: 0x80410000, 5: 0x80004000}.get(index, 0) if linked else 0
        kind = {0: 0, 1: 3, 2: 2, 6: 9, 7: 3}.get(index, 1)
        flags = 2 if index in (3, 4, 5) else 0
        headers.append((sum(len(name) + 1 for name in names[:index]), kind, flags,
                        address, offset, len(payload), 1 if index == 2 else 2 if index == 6 else 0,
                        4 if index == 6 else 0, 4, 16 if index == 2 else 8 if index == 6 else 0))
    section_offset = len(image)
    for header in headers:
        image.extend(struct.pack(">10I", *header))
    struct.pack_into(">16sHHIIIIIHHHHHH", image, 0,
                     b"\x7fELF\x01\x02\x01" + bytes(9), 2 if linked else 1, 8, 1, 0,
                     52 if linked else 0, section_offset, 0, 52, 32 if linked else 0,
                     3 if linked else 0, 40, len(headers), 7)
    rom = bytearray(b"\x80\x37\x12\x40" + bytes(0x3FFC))
    if linked:
        for index, section in enumerate((3, 4, 5)):
            position = 0x1000 * (index + 1)
            rom[position:position + len(payloads[section])] = payloads[section]
            struct.pack_into(">8I", image, 52 + index * 32, 1, offsets[section],
                             headers[section][3], position, len(payloads[section]),
                             len(payloads[section]), 4, 4)
    return bytes(image), bytes(rom), symbols


class CurrentSixManifestTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.build = self.root / "candidate"
        self.build.mkdir()
        self.elf = self.build / "candidate.elf"
        self.obj = self.build / "story/sc_data.o"
        self.obj.parent.mkdir()
        self.rom = self.build / "candidate.z64"
        self.map = self.build / "candidate.map"
        image, rom, symbols = elf_fixture(True)
        self.elf.write_bytes(image)
        self.rom.write_bytes(rom)
        self.obj.write_bytes(elf_fixture(False)[0])
        self.map.write_text("".join(f"0x{address:08x} {name}\n" for name, address in symbols.items()))

    def verify(self):
        return seal.verify_link(self.elf, self.obj, self.rom, self.map)

    def test_actual_relocation_reconstruction_and_rom_binding(self):
        self.assertEqual(self.verify(), {**story_probe.CURRENT_STORY_ABI, **story_probe.CURRENT_SIX_ABI})
        self.obj.write_bytes(self.obj.read_bytes()[:-1])
        with self.assertRaises(HarnessError):
            self.verify()

    def test_changed_catalog_object_link_rom_map_and_metadata_refused(self):
        paths = (self.obj, self.elf, self.rom, self.map)
        originals = {path: path.read_bytes() for path in paths}
        elf = seal.Elf(self.elf)
        obj = seal.Elf(self.obj)
        cases = [
            (self.obj, obj.sections[3][4]),
            (self.elf, elf.sections[3][4]),
            (self.rom, 0x1000),
            (self.elf, elf.sections[5][4]),
            (self.rom, 0x3000),
            (self.obj, obj.sections[6][4] + 7),
        ]
        for path, offset in cases:
            with self.subTest(path=path.name, offset=offset):
                changed = bytearray(originals[path])
                changed[offset] ^= 1
                path.write_bytes(changed)
                with self.assertRaises(HarnessError):
                    self.verify()
                path.write_bytes(originals[path])
        self.map.write_text(self.map.read_text().replace("80400000 sc_species", "80400004 sc_species"))
        with self.assertRaises(HarnessError):
            self.verify()

    def package(self):
        package = self.build / "story/current-six"
        package.mkdir()
        (self.root / "source.c").write_text("tracked source\n")
        for name in seal.FILES:
            (package / name).write_bytes(self.obj.read_bytes() if name == "sc_data.o"
                                        else (name + "\n").encode())
        for name in ("sc_data.c", "data.json"):
            (self.obj.parent / name).write_bytes((package / name).read_bytes())
        manifest = dict(schema=1, abi=self.verify(), catalog_identity=seal.CATALOG_IDENTITY,
                        base_identity=seal.BASE_IDENTITY,
                        rom_sha256=seal.digest(self.rom), map_sha256=seal.digest(self.map),
                        elf_path="candidate/candidate.elf", elf_sha256=seal.digest(self.elf),
                        catalog_object_path="candidate/story/sc_data.o",
                        catalog_object_sha256=seal.digest(self.obj),
                        catalog_source_sha256=seal.digest(package / "sc_data.c"),
                        catalog_data_sha256=seal.digest(package / "data.json"),
                        scope="Synthetic manifest fixture, not runtime proof",
                        files={name: seal.digest(package / name) for name in seal.FILES},
                        sources={"source.c": seal.digest(self.root / "source.c")})
        self.write_manifest(package, manifest)
        return package, manifest

    @staticmethod
    def write_manifest(package, manifest):
        data = json.dumps(manifest).encode()
        (package / "manifest.json").write_bytes(data)
        (package / "manifest.sha256").write_text(hashlib.sha256(data).hexdigest() + "\n")

    def test_package_requires_exact_sources_files_and_same_candidate(self):
        package, manifest = self.package()
        with patch.object(story_probe, "ROOT", self.root), patch.object(seal, "ROOT", self.root), \
                patch.object(seal, "source_paths", return_value=["source.c"]):
            self.assertEqual(story_probe.verify_current_six_package(package, self.rom, self.map), manifest)
            for key in ("abi", "catalog_identity", "base_identity", "sources", "files",
                        "elf_sha256", "catalog_object_sha256", "schema"):
                changed = copy.deepcopy(manifest)
                changed[key] = {} if isinstance(changed[key], dict) else "invalid"
                self.write_manifest(package, changed)
                with self.subTest(key=key), self.assertRaises(HarnessError):
                    story_probe.verify_current_six_package(package, self.rom, self.map)
            self.write_manifest(package, manifest)
            (self.root / "source.c").write_text("changed\n")
            with self.assertRaises(HarnessError):
                story_probe.verify_current_six_package(package, self.rom, self.map)

    def test_package_seal_truncation_and_escape_refused(self):
        package, manifest = self.package()
        with patch.object(story_probe, "ROOT", self.root), patch.object(seal, "ROOT", self.root), \
                patch.object(seal, "source_paths", return_value=["source.c"]):
            (package / "manifest.sha256").write_text("wrong\n")
            with self.assertRaises(HarnessError):
                story_probe.verify_current_six_package(package, self.rom, self.map)
            changed = copy.deepcopy(manifest)
            changed["elf_path"] = "../outside.elf"
            self.write_manifest(package, changed)
            with self.assertRaises(HarnessError):
                story_probe.verify_current_six_package(package, self.rom, self.map)
            self.write_manifest(package, manifest)
            (package / "codec").write_bytes(b"truncated")
            with self.assertRaises(HarnessError):
                story_probe.verify_current_six_package(package, self.rom, self.map)

    def test_producer_publishes_fresh_bound_package_and_rejects_reuse(self):
        (self.root / "source.c").write_text("tracked source\n")
        for name in ("sc_data.c", "data.json"):
            (self.obj.parent / name).write_text(name + "\n")

        def host_command(command, **kwargs):
            if command[0] == "bash":
                fixture = Path(command[-1])
                fixture.mkdir()
                for name in seal.FILES:
                    if name in ("sc_data.o", "host-validation.log"):
                        continue
                    fixture.joinpath(name).write_bytes(
                        (self.obj.parent / name).read_bytes() if name in ("sc_data.c", "data.json")
                        else (name + "\n").encode())
            else:
                Path(command[-1]).write_bytes(self.obj.read_bytes())
            return SimpleNamespace(returncode=0, stdout="Synthetic HOST fixture only\n", stderr="")

        with patch.object(story_probe, "ROOT", self.root), patch.object(seal, "ROOT", self.root), \
                patch.object(seal, "source_paths", return_value=["source.c"]), \
                patch.object(seal.subprocess, "run", side_effect=host_command) as commands:
            package = seal.seal(self.rom, self.map, self.elf, self.obj)
            self.assertEqual(commands.call_count, 2)
            self.assertEqual(story_probe.verify_current_six_package(package, self.rom, self.map)["abi"],
                             {**story_probe.CURRENT_STORY_ABI, **story_probe.CURRENT_SIX_ABI})
            self.assertEqual(package.stat().st_mode & 0o777, 0o555)
            with self.assertRaises(HarnessError):
                seal.seal(self.rom, self.map, self.elf, self.obj)

    def test_producer_refuses_failed_host_and_stale_catalog_before_publication(self):
        (self.root / "source.c").write_text("tracked source\n")
        for name in ("sc_data.c", "data.json"):
            (self.obj.parent / name).write_text(name + "\n")
        output = self.build / "story/current-six"

        def stale_fixture(command, **kwargs):
            fixture = Path(command[-1])
            fixture.mkdir()
            (fixture / "sc_data.c").write_text("stale catalog\n")
            return SimpleNamespace(returncode=0, stdout="Synthetic HOST fixture only\n", stderr="")

        with patch.object(story_probe, "ROOT", self.root), patch.object(seal, "ROOT", self.root), \
                patch.object(seal, "source_paths", return_value=["source.c"]):
            for result in (SimpleNamespace(returncode=1, stdout="", stderr="explicit failure"),
                           None):
                with self.subTest(host_failure=result is not None), \
                        patch.object(seal.subprocess, "run", return_value=result,
                                     side_effect=stale_fixture if result is None else None), \
                        self.assertRaises(HarnessError):
                    seal.seal(self.rom, self.map, self.elf, self.obj)
                self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
