"""Reproducible native v3 Pak migration, adapted from the PR16 scratch probe.

The default creates a real suspended run via UI, then converts its two framed
records to explicit v3 wire fixtures. No ROM or save fixture is committed.
"""

import argparse
import ctypes
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib

from tools.rltest.frontend import Frontend
from tools.rltest.run import default_core
from tools.rltest.save_roundtrip import boot, find_pak, native_command, save_ram
from tools.rltest.test_permanent_progression import select
from tools.rltest.test_play_run import (
    enter_roguelike_from_menu, read_run, require, start_new_run, wait_scene,
)


def note_pages(data):
    base = find_pak(data)
    directory = data[base + 768:base + 1280]
    matches = [i for i in range(16) if directory[i * 32:i * 32 + 6] == b"RL01RL"]
    require(len(matches) == 1, "expected exactly one owned note")
    page = int.from_bytes(directory[matches[0] * 32 + 6:matches[0] * 32 + 8], "big")
    pages = []
    while page != 1:
        require(5 <= page < 128 and page not in pages, "bad note page chain")
        pages.append(page)
        page = int.from_bytes(data[base + 256 + page * 2:base + 258 + page * 2], "big")
    require(len(pages) == 10, "note allocation changed")
    return base, pages


def read_note(data):
    base, pages = note_pages(data)
    return b"".join(data[base + page * 256:base + (page + 1) * 256] for page in pages)


def as_v3(data):
    note = bytearray(read_note(data))
    for start in (0, 1280):
        header = note[start:start + 32]
        if header[:4] != b"RLPK":
            continue
        length = struct.unpack_from("<I", header, 12)[0]
        payload = note[start + 32:start + 32 + length]
        require(payload[:5] == b"RLSV\x04", "source is not native v4")
        # v4 extends the 357-byte v3 profile body by 55 bytes before suspend.
        payload = payload[:357] + payload[412:-4]
        payload[4] = 3
        payload += struct.pack("<I", zlib.crc32(payload))
        struct.pack_into("<II", header, 12, len(payload), zlib.crc32(payload))
        struct.pack_into("<I", header, 28, zlib.crc32(header[:28]))
        note[start:start + 1280] = bytes(1280)
        note[start:start + 32] = header
        note[start + 32:start + 32 + len(payload)] = payload
    result = bytearray(data)
    base, pages = note_pages(data)
    for index, page in enumerate(pages):
        result[base + page * 256:base + (page + 1) * 256] = note[index * 256:(index + 1) * 256]
    return bytes(result)


def phase(directory, part):
    with Frontend(default_core(), Path("build_rl/pokestadium-rl.z64"),
                  Path("build_rl/pokestadium-rl.map"), output=directory,
                  options={"mupen64plus-pak1": "memory"}, timeout=900) as emu:
        if part != "write":
            pointer, initial = save_ram(emu)
            data = (directory / ("v3-save.bin" if part == "upgrade" else "v4-save.bin")).read_bytes()
            require(len(data) == len(initial), "aggregate save size changed")
            ctypes.memmove(pointer, data, len(data))
        boot(emu)
        if part == "write":
            enter_roguelike_from_menu(emu)
            start_new_run(emu, 8, 0)
            (directory / "suspended-run.bin").write_bytes(bytes(read_run(emu)))
            select(emu, read_run(emu).map.lanes[0])
            emu.wait_state(4, frames=600)
            (directory / "v3-save.bin").write_bytes(as_v3(save_ram(emu)[1]))
            print("MIGRATION FIXTURE: native UI suspension converted to explicit v3 wire")
            return
        native_command(emu, 1)
        before = save_ram(emu)[1]
        result = native_command(emu, 1)
        require(result[1] == 0 and result[7:9] == (1, 8), "native migrated load failed")
        require(save_ram(emu)[1] == before, "read-only migration load wrote bytes")
        enter_roguelike_from_menu(emu)
        select(emu, 1)
        wait_scene(emu, 2)
        require(bytes(read_run(emu)) == (directory / "suspended-run.bin").read_bytes(),
                "migration changed active run/RNG/map/HP/PP/status")
        select(emu, read_run(emu).map.lanes[0])
        emu.wait_state(4, frames=600)
        after = save_ram(emu)[1]
        note = read_note(after)
        require(4 in (note[36], note[1316]), "UI save did not upgrade to v4")
        base, pages = note_pages(before)
        owned = {base + page * 256 + offset for page in pages for offset in range(256)}
        require(all(a == b for i, (a, b) in enumerate(zip(before, after)) if i not in owned),
                "migration modified unowned save bytes")
        (directory / "v4-save.bin").write_bytes(after)
        print("NATIVE V3->V4 MIGRATION PASSED: full suspend identical, owned pages only")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--phase", choices=("write", "upgrade", "reload"))
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.phase:
        phase(args.output, args.phase)
        return
    root = Path(".local/rltest")
    root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="migration-", dir=root) as temporary:
        for part in ("write", "upgrade", "reload"):
            subprocess.run([sys.executable, "-m", "tools.rltest.test_save_migration",
                            "--phase", part, "--output", temporary], check=True)


if __name__ == "__main__":
    main()
