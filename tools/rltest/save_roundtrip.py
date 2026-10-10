"""Native Controller Pak save test; isolated libretro save RAM, never savestates."""

import argparse
import ctypes as ct
import json
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.rltest.frontend import Frontend, HarnessError
from tools.rl.boot_test import find_core


def checksum_inode(data, base):
    data[base + 257] = sum(data[base + 266:base + 512]) & 255
    data[base + 512:base + 768] = data[base + 256:base + 512]


def find_pak(data):
    for base in range(0, len(data) - 32768 + 1, 256):
        header = data[base + 32:base + 64]
        if header[26] != 1 or not (int.from_bytes(header[24:26], "big") & 1):
            continue
        words = struct.unpack(">14H", header[:28])
        if sum(words) & 65535 != int.from_bytes(header[28:30], "big"):
            continue
        if sum((~n) & 65535 for n in words) & 65535 != int.from_bytes(header[30:32], "big"):
            continue
        if all(data[base + n * 32:base + n * 32 + 32] == header for n in (3, 4, 6)):
            return base
    raise HarnessError("Installed core has no supported one-bank Controller Pak image")


def native_command(emu, command, *, runs=0, wins=0, floor=0, create=0, seed=0):
    mailbox = emu.symbols.address("gRlSaveProbe")
    values = (0, 0xFFFFFFFF, 0, create, runs, wins, floor, 0, seed)
    emu.write(mailbox, struct.pack(">9I", *values))
    emu.write_u32(mailbox, command)
    emu.hold("L")
    emu.step(4)
    emu.press("N64_A", frames=4, after=4)
    emu.wait(lambda e: e.read_u32(mailbox) == 0, frames=900, description="native save API")
    emu.release("L")
    result = struct.unpack(">9I", emu.read(mailbox, 36))
    emu.wait_state(4, frames=900)
    emu.step(180)
    return result


def save_ram(emu):
    size = emu.core.retro_get_memory_size(0)
    pointer = emu.core.retro_get_memory_data(0)
    if not size or not pointer:
        raise HarnessError("Installed core does not expose save RAM")
    return pointer, ct.string_at(pointer, size)


def boot(emu):
    emu.wait_state(2, frames=3000)
    emu.step(120)
    emu.press("START", frames=4, after=4)
    emu.wait_state(4, frames=1200)
    emu.step(180)
    emu.press("N64_A", frames=4, after=4)
    emu.step(240)


def allowed_changes(before, after, base):
    inode = after[base + 256:base + 512]
    directory = after[base + 768:base + 1280]
    matches = [i for i in range(16)
               if directory[i * 32:i * 32 + 4] == b"RL01"
               and directory[i * 32 + 4:i * 32 + 6] == b"RL"]
    if len(matches) != 1:
        raise HarnessError("Native API did not create exactly one dedicated note")
    note = matches[0]
    page = int.from_bytes(directory[note * 32 + 6:note * 32 + 8], "big")
    pages = []
    while page != 1:
        if not 5 <= page < 128 or page in pages:
            raise HarnessError("Invalid Controller Pak chain after native save")
        pages.append(page)
        page = int.from_bytes(inode[page * 2:page * 2 + 2], "big")
    if len(pages) != 10:
        raise HarnessError("Native note is not the required 2560 bytes")
    allowed = set(range(base + 768 + note * 32, base + 800 + note * 32))
    for page in pages:
        allowed.update(range(base + page * 256, base + (page + 1) * 256))
        for start in (256, 512):
            allowed.update((base + start + page * 2, base + start + page * 2 + 1))
    allowed.update((base + 257, base + 513))
    changed = [i for i, (a, b) in enumerate(zip(before, after)) if a != b and i not in allowed]
    if changed:
        raise HarnessError(f"Unowned save bytes changed: {changed[:16]}")
    return pages


def phase(args):
    directory = Path(args.out)
    options = {"mupen64plus-pak1": "none" if args.phase == "missing" else "memory"}
    with Frontend(args.core, args.rom, args.map, directory, options=options, timeout=900) as emu:
        pointer, initial = save_ram(emu)
        if args.phase == "reload":
            restored = (directory / "save-ram.bin").read_bytes()
            if len(restored) != len(initial):
                raise HarnessError("Save RAM size changed across processes")
            ct.memmove(pointer, restored, len(restored))
        boot(emu)
        # Menu Select initializes vanilla FlashRAM on first exit from a fresh image.
        # Stabilize that vanilla transition before measuring the storage API.
        native_command(emu, 1)
        pointer, before = save_ram(emu)
        if args.phase == "missing":
            result = native_command(emu, 2, create=1, runs=42)
            if result[1] != 2:
                raise HarnessError(f"Missing Pak returned {result[1]}, expected NO_DEVICE")
            if save_ram(emu)[1] != before:
                raise HarnessError("Missing-device API modified save RAM")
            print("missing device: explicit NO_DEVICE, save RAM unchanged")
            return
        if args.phase == "reload":
            result = native_command(emu, 1)
            if result[1] != 0 or result[4:9] != (43, 7, 12, 1, 0x12345678):
                raise HarnessError(f"Cold reload did not restore suspension: {result}")
            if save_ram(emu)[1] != before:
                raise HarnessError("Read-only runtime load modified save RAM")
            print("fresh process: native suspension/meta load OK, all save RAM unchanged")
            return
        base = find_pak(before)
        data = bytearray(before)
        # An unrelated synthetic vanilla note, on a separate allocated page.
        if any(data[base + 768:base + 800]):
            raise HarnessError("Test expects an isolated empty Controller Pak")
        data[base + 768:base + 800] = bytes(32)
        data[base + 768:base + 774] = b"NPSE01"
        data[base + 774:base + 776] = b"\x00\x05"
        data[base + 776] = 2
        if args.phase == "full":
            for page in range(5, 128):
                data[base + 256 + page * 2:base + 258 + page * 2] = struct.pack(
                    ">H", 1 if page == 127 else page + 1)
        else:
            data[base + 256 + 10:base + 256 + 12] = b"\x00\x01"
        data[base + 1280:base + 1536] = bytes((i * 37 + 19) & 255 for i in range(256))
        checksum_inode(data, base)
        ct.memmove(pointer, bytes(data), len(data))
        before = bytes(data)
        if args.phase == "full":
            result = native_command(emu, 2, create=1, runs=42)
            if result[1] != 5 or save_ram(emu)[1] != before:
                raise HarnessError(f"Full Pak not preserved with STORAGE_FULL: {result}")
            data[base + 513] ^= 1
            ct.memmove(pointer, bytes(data), len(data))
            result = native_command(emu, 2, create=1, runs=42)
            if result[1] != 9 or save_ram(emu)[1] != bytes(data):
                raise HarnessError(f"Damaged filesystem not preserved with BAD_DATA: {result}")
            print("full/damaged filesystem: explicit errors, save RAM unchanged")
            return
        result = native_command(emu, 1)
        if result[1] != 1:
            raise HarnessError(f"Absent note returned {result[1]}, expected NOT_FOUND")
        result = native_command(emu, 2, runs=42, wins=6, floor=11, create=1)
        if result[1] != 0:
            raise HarnessError(f"Native create failed with status {result[1]}")
        result = native_command(emu, 1)
        if result[1] != 0 or result[4:8] != (42, 6, 11, 0):
            raise HarnessError(f"Native meta roundtrip failed: {result}")
        result = native_command(emu, 3, runs=43, wins=7, floor=12, seed=0x12345678)
        if result[1] != 0:
            raise HarnessError(f"Native suspend update failed: {result[1]}")
        after = save_ram(emu)[1]
        pages = allowed_changes(before, after, base)
        (directory / "save-ram.bin").write_bytes(after)
        (directory / "evidence.json").write_text(json.dumps({
            "core": args.core, "save_ram_bytes": len(after), "pak_offset": base,
            "note_pages": pages, "unowned_bytes_unchanged": True,
            "meta_bytes": 416, "suspend_bytes": 972,
        }, indent=2) + "\n")
        # Version/CRC failure fixtures go through the runtime API, not only the codec.
        data = bytearray(after)
        first = base + pages[0] * 256
        data[first + 4:first + 8] = struct.pack("<I", 2)
        data[first + 28:first + 32] = struct.pack("<I", zlib.crc32(data[first:first + 28]))
        ct.memmove(pointer, bytes(data), len(data))
        for command in (1, 2):
            result = native_command(emu, command, create=1, runs=99)
            if result[1] != 8 or save_ram(emu)[1] != bytes(data):
                raise HarnessError("Unknown version not preserved with BAD_VERSION")
        data = bytearray(after)
        for logical in (32, 1280 + 32):
            physical = base + pages[logical // 256] * 256 + logical % 256
            data[physical] ^= 1
        ct.memmove(pointer, bytes(data), len(data))
        for command in (1, 2):
            result = native_command(emu, command, create=1, runs=99)
            if result[1] != 7 or save_ram(emu)[1] != bytes(data):
                raise HarnessError("Corrupted record not preserved with BAD_CRC")
        print("native meta/save update OK; unrelated note, all cartridge/other Pak bytes unchanged")
        print("runtime CRC/version failures: explicit errors, corrupted bytes preserved")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core", default=find_core())
    parser.add_argument("--rom", default=str(ROOT / "build_rl/pokestadium-rl.z64"))
    parser.add_argument("--map", default=str(ROOT / "build_rl/pokestadium-rl.map"))
    parser.add_argument("--out")
    parser.add_argument("--phase", choices=("write", "reload", "missing", "full"))
    args = parser.parse_args()
    if not args.core:
        raise HarnessError("Native storage unverified: no supported installed core")
    if args.phase:
        phase(args)
        return
    root = ROOT / ".local"
    root.mkdir(exist_ok=True)
    # New isolated directory every run; saved bytes are never a repository fixture.
    with tempfile.TemporaryDirectory(prefix="rl-save-", dir=root) as out:
        for step in ("write", "reload", "missing", "full"):
            subprocess.run([sys.executable, str(Path(__file__).resolve()),
                            "--core", args.core, "--rom", args.rom, "--map", args.map,
                            "--out", out, "--phase", step], check=True)
    print("native Controller Pak roundtrip/restart/preservation: OK")


if __name__ == "__main__":
    main()
