"""Explicitly opt-in story native milestone probe; fresh processes, never savestates."""

import argparse
import ctypes as ct
from contextlib import contextmanager
import hashlib
import json
import os
import re
import signal
import struct
import subprocess
import sys
import threading
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.rltest.frontend import Frontend, HarnessError
from tools.rltest.save_roundtrip import (
    boot, checksum_inode, find_pak, native_command, save_ram,
)
from tools.rl.boot_test import find_core

FIELDS = (
    "command status controller create seed active ticks ko_count pause_ticks resume force_ko "
    "phase exp level hp max_hp dvs live_hp live_max_hp live_attack live_base_attack "
    "attack_stage state_address state_size completed_crc interrupted post_pool_crc "
    "logic_steps presentation_steps native_max_hp native_dvs live_dvs stage "
    "pool_free arena_archive arena_count arena_index arena_required "
    "cold_before cold_after cold_address cold_size cold_busy cold_calls "
    "combat_address combat_size combat_crc"
).split()


def read_probe(emu):
    data = emu.read(emu.symbols.address("gScProbe"), len(FIELDS) * 4)
    return dict(zip(FIELDS, struct.unpack(f">{len(FIELDS)}I", data)))


def set_field(emu, name, value):
    emu.write_u32(emu.symbols.address("gScProbe") + FIELDS.index(name) * 4, value)


def start_command(emu, command, create=0):
    values = dict.fromkeys(FIELDS, 0)
    values.update(status=0xFFFFFFFF, create=create, seed=42)
    emu.write(emu.symbols.address("gScProbe"),
              struct.pack(f">{len(FIELDS)}I", *(values[name] for name in FIELDS)))
    set_field(emu, "command", command)
    emu.hold("L")
    emu.step(4)
    emu.press("N64_A", frames=4, after=4)


def finish_command(emu):
    try:
        emu.wait(lambda e: read_probe(e)["command"] == 0, frames=6000,
                 description="story probe native command")
    except HarnessError as error:
        raise HarnessError(f"{error}; state={emu.read_u32('gCurrentGameState')}; "
                           f"probe={read_probe(emu)}") from error
    result = read_probe(emu)
    emu.release("L")
    emu.wait_state(4, frames=900)
    emu.step(180)
    return result


def command(emu, value, create=0):
    start_command(emu, value, create)
    return finish_command(emu)

def wait_probe(emu, predicate, frames, description):
    def ready(instance):
        result = read_probe(instance)
        require(result["command"] != 0 and
                (result["status"] == 0xFFFFFFFF or result["status"] < 0x100),
                f"{description} failed before its boundary: {result}")
        return bool(predicate(result))
    try:
        emu.wait(ready, frames=frames, description=description)
    except HarnessError as error:
        thread = emu.symbols.address("pThreads")
        raise HarnessError(f"{error}; probe={read_probe(emu)}; "
                           f"launch_stage={emu.read_u32('gRlBattleLaunchStage')}; "
                           f"game_thread_state={emu.read_u16(thread + 16)}; "
                           f"pc={emu.read_u32(thread + 284):08x}; "
                           f"ra={emu.read_u32(thread + 260):08x}; "
                           f"sp={emu.read_u32(thread + 244):08x}; "
                           f"cause={emu.read_u32(thread + 288):08x}; "
                           f"badvaddr={emu.read_u32(thread + 292):08x}") from error

def stalled_probe(emu, directory):
    evidence = {"frame": emu.frame, "diagnostic": "watchdog timeout; no concurrent core reads"}
    (directory / "stall.json").write_text(json.dumps(evidence, indent=2) + "\n")
    print(f"Native probe watchdog: {evidence}", file=sys.stderr, flush=True)
    os.kill(os.getpid(), signal.SIGTERM)

@contextmanager
def probe_deadline(emu, directory):
    watchdog = threading.Timer(180, stalled_probe, (emu, directory))
    watchdog.daemon = True
    watchdog.start()
    try:
        yield
    finally:
        watchdog.cancel()


def require(condition, message):
    if not condition:
        raise HarnessError(message)

def map_value(map_path, name):
    pattern = re.compile(rf"^\s*0x([0-9a-fA-F]+)\s+{re.escape(name)}(?:\s*=\s*.*)?\s*$")
    for line in Path(map_path).read_text().splitlines():
        match = pattern.fullmatch(line)
        if match:
            return int(match[1], 16)
    raise HarnessError(f"Missing linker layout symbol: {name}")


def story_owned_changes(before, after, base):
    require(len(before) == len(after), "Save RAM length changed")
    directory = after[base + 768:base + 1280]
    notes = [i for i in range(16) if directory[i * 32:i * 32 + 6] == b"SC01RL"]
    require(len(notes) == 1, "Expected exactly one isolated SC01 note")
    note = notes[0]
    inode = after[base + 256:base + 512]
    page = int.from_bytes(directory[note * 32 + 6:note * 32 + 8], "big")
    pages = []
    while page != 1:
        require(5 <= page < 128 and page not in pages, "Invalid story Pak chain")
        pages.append(page)
        page = int.from_bytes(inode[page * 2:page * 2 + 2], "big")
    require(len(pages) == 6, "SC01 must occupy exactly six pages")
    allowed = set(range(base + 768 + note * 32, base + 800 + note * 32))
    for page in pages:
        allowed.update(range(base + page * 256, base + (page + 1) * 256))
        for start in (256, 512):
            allowed.update((base + start + page * 2, base + start + page * 2 + 1))
    allowed.update((base + 257, base + 513))
    changes = [i for i, (a, b) in enumerate(zip(before, after)) if a != b and i not in allowed]
    require(not changes, f"Story API changed unowned/RL01 bytes: {changes[:16]}")
    return pages


def foreign_note(data, base, full=False):
    result = bytearray(data)
    directory = result[base + 768:base + 1280]
    slots = [i for i in range(16) if not any(directory[i * 32:i * 32 + 32])]
    free = [i for i in range(5, 128)
            if result[base + 256 + i * 2:base + 258 + i * 2] == b"\x00\x03"]
    require(slots and free, "Fixture needs an empty note slot and free page")
    pages = free if full else free[:1]
    offset = base + 768 + slots[0] * 32
    result[offset:offset + 6] = b"NPSE01"
    result[offset + 6:offset + 8] = struct.pack(">H", pages[0])
    result[offset + 8] = 2
    for index, page in enumerate(pages):
        following = pages[index + 1] if index + 1 < len(pages) else 1
        result[base + 256 + page * 2:base + 258 + page * 2] = struct.pack(">H", following)
        result[base + page * 256:base + (page + 1) * 256] = bytes(
            (i * 37 + 19) & 255 for i in range(256))
    checksum_inode(result, base)
    return bytes(result)


def phase(args):
    directory = Path(args.out)
    provenance = {
        "head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "diff_sha256": hashlib.sha256(subprocess.check_output(
            ["git", "diff", "HEAD", "--binary"], cwd=ROOT)).hexdigest(),
        "command": sys.argv,
        "rom_sha256": hashlib.sha256(Path(args.rom).read_bytes()).hexdigest(),
        "map_sha256": hashlib.sha256(Path(args.map).read_bytes()).hexdigest(),
        "core_sha256": hashlib.sha256(Path(args.core).read_bytes()).hexdigest(),
    }
    (directory / f"{args.phase}-provenance.json").write_text(
        json.dumps(provenance, indent=2) + "\n")
    options = {"mupen64plus-pak1": "none" if args.phase == "missing" else "memory",
               "mupen64plus-angrylion-multithread": "4"}
    with Frontend(args.core, args.rom, args.map, directory / args.phase,
                  options=options, timeout=900) as emu, probe_deadline(emu, directory):
        pointer, initial = save_ram(emu)
        if args.phase in ("reload", "interrupted_reload", "interrupt", "battle", "natural",
                          "allocation"):
            restored = (directory / "save-ram.bin").read_bytes()
            require(len(restored) == len(initial), "Save RAM size changed between processes")
            ct.memmove(pointer, restored, len(restored))
        boot(emu)
        # Stabilize vanilla FlashRAM initialization, without invoking any story writer.
        native_command(emu, 1)
        pointer, before = save_ram(emu)
        if args.phase == "missing":
            result = command(emu, 2, 1)
            require(result["status"] == 2 and save_ram(emu)[1] == before,
                    f"Missing Pak did not fail read-only: {result}")
        elif args.phase in ("reload", "interrupted_reload"):
            result = command(emu, 1)
            interrupted = args.phase == "interrupted_reload"
            require(result["status"] == 0 and bool(result["interrupted"]) == interrupted and
                    result["level"] == (6 if interrupted else 7) and
                    result["phase"] == (0 if interrupted else 3),
                    f"Fresh-process checkpoint did not reload: {result}")
            require(save_ram(emu)[1] == before, "Cold load modified storage")
        elif args.phase == "allocation":
            base = find_pak(before)
            result = command(emu, 3)
            require(result["status"] == 0x100 + 1 and result["stage"] == 13 and
                    result["arena_required"] > result["pool_free"] and
                    result["ticks"] == 0 and result["ko_count"] == 0 and not result["active"],
                    f"Expected explicit prebattle allocation refusal: {result}")
            after = save_ram(emu)[1]
            story_owned_changes(before, after, base)
            (directory / "save-ram.bin").write_bytes(after)
        elif args.phase == "interrupt":
            base = find_pak(before)
            start_command(emu, 3)
            wait_probe(emu, lambda result: result["active"] and result["ticks"],
                       1800, "durable prebattle checkpoint")
            result = read_probe(emu)
            require(result["phase"] == 1 and result["level"] == 6,
                    f"Battle already advanced before interruption fixture: {result}")
            require(not result["cold_busy"] and not result["cold_address"] and
                    result["cold_calls"] == 1 and result["cold_before"] == result["cold_after"],
                    f"Cold preparation is still owned during battle: {result}")
            after = save_ram(emu)[1]
            story_owned_changes(before, after, base)
            (directory / "save-ram.bin").write_bytes(after)
        elif args.phase in ("battle", "natural"):
            base = find_pak(before)
            start_command(emu, 3)
            wait_probe(emu, lambda result: result["active"] and result["ticks"],
                       1800, "resident fragment-62 probe")
            if args.phase == "battle":
                require(read_probe(emu)["max_hp"] == 22, "GB HP-DV fixture did not start at HP22")
                set_field(emu, "force_ko", 1)
            wait_probe(emu, lambda result: result["phase"] == 2, 6000, "per-KO learning pause")
            paused = read_probe(emu)
            require(paused["ko_count"] == 1 and paused["level"] == 7 and paused["max_hp"] == 24,
                    f"Expected exactly one canonical level-up at the KO: {paused}")
            require(paused["native_max_hp"] == paused["live_max_hp"] == paused["max_hp"] and
                    paused["native_dvs"] == paused["live_dvs"] == paused["dvs"] == 0x8889,
                    f"GB stats/DVs were replaced by native recalculation: {paused}")
            if args.phase == "battle":
                require(paused["attack_stage"] == 8 and
                        paused["live_attack"] == paused["live_base_attack"] * 150 // 100,
                        f"KO refresh lost the synthetic +1 stage: {paused}")
            address, size = paused["state_address"], paused["state_size"]
            require(0x80000000 <= address < address + size <= map_value(args.map, "heap_BSS_START"),
                    "Story state is not in fixed resident memory before the main pool")
            resident = emu.read(address, size)
            combat_start, combat_size = paused["combat_address"], paused["combat_size"]
            require(0x80000000 <= combat_start < combat_start + combat_size <=
                    map_value(args.map, "heap_BSS_START") and combat_size < 1024,
                    "Combat view is not small fixed resident value data")
            combat = emu.read(combat_start, combat_size)
            require(not paused["cold_busy"] and not paused["cold_address"] and
                    paused["cold_calls"] == 1 and paused["cold_before"] == paused["cold_after"],
                    f"Cold preparation did not return/unload before battle: {paused}")
            emu.step(60)
            held = read_probe(emu)
            require(held["pause_ticks"] > paused["pause_ticks"], "Learning pause did not pump frames")
            require(emu.read(address, size) == resident and
                    emu.read(combat_start, combat_size) == combat and
                    all(held[key] == paused[key] for key in (
                        "exp", "ko_count", "logic_steps", "presentation_steps", "cold_calls")),
                    "Learning advanced state, XP, presentation or battle actions")
            set_field(emu, "resume", 1)
            result = finish_command(emu)
            require(result["status"] == 0 and result["ko_count"] == 1 and result["phase"] == 3 and
                    result["post_pool_crc"] == result["completed_crc"],
                    f"Battle result was not durably committed after pool return: {result}")
            require(result["cold_calls"] == 2 and not result["cold_busy"] and
                    not result["cold_address"] and result["cold_before"] == result["cold_after"],
                    f"Postbattle cold reload/commit ownership failed: {result}")
            after = save_ram(emu)[1]
            story_owned_changes(before, after, base)
            (directory / "save-ram.bin").write_bytes(after)
        else:
            require(native_command(emu, 2, create=1, runs=42)[1] == 0, "RL01 baseline failed")
            pointer, baseline = save_ram(emu)
            base = find_pak(baseline)
            fixture = foreign_note(baseline, base, full=args.phase == "full")
            ct.memmove(pointer, fixture, len(fixture))
            if args.phase == "full":
                result = command(emu, 2, 1)
                require(result["status"] == 3 and save_ram(emu)[1] == fixture,
                        f"Full Pak did not fail unchanged: {result}")
                damaged = bytearray(fixture)
                damaged[base + 513] ^= 1
                ct.memmove(pointer, bytes(damaged), len(damaged))
                result = command(emu, 2, 1)
                require(result["status"] == 5 and save_ram(emu)[1] == bytes(damaged),
                        f"Damaged Pak did not fail unchanged: {result}")
            else:
                result = command(emu, 2, 1)
                require(result["status"] == 0, f"SC01 creation failed: {result}")
                after = save_ram(emu)[1]
                story_owned_changes(fixture, after, base)
                require(native_command(emu, 1)[4] == 42, "SC01 changed RL01 meta")
                (directory / "save-ram.bin").write_bytes(save_ram(emu)[1])
        (directory / f"{args.phase}-result.json").write_text(json.dumps(result, indent=2) + "\n")
        print(f"story native {args.phase}: OK")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--execute", action="store_true",
                        help="Run emulator; use only after the exclusive validation slot is granted")
    parser.add_argument("--core")
    parser.add_argument("--rom", default="build_story_probe/pokestadium-rl.z64")
    parser.add_argument("--map", default="build_story_probe/pokestadium-rl.map")
    parser.add_argument("--out", default=".local/rltest/story-probe")
    parser.add_argument("--phase", choices=("create", "interrupt", "interrupted_reload",
                                          "battle", "natural", "reload", "missing", "full",
                                          "allocation"))
    args = parser.parse_args()
    if not args.execute:
        parser.error("No emulator launched: --execute is required after slot authorization")
    args.core = args.core or find_core()
    require(args.core is not None, "No installed software-rendering N64 core found")
    Path(args.out).mkdir(parents=True, exist_ok=True)
    if args.phase:
        phase(args)
    else:
        for name in ("create", "interrupt", "interrupted_reload", "battle", "reload",
                     "natural", "reload", "missing", "full"):
            subprocess.run([sys.executable, __file__, "--execute", "--phase", name,
                            "--core", args.core, "--rom", args.rom, "--map", args.map,
                            "--out", args.out], check=True)


if __name__ == "__main__":
    main()
