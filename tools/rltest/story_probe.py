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
from tools.rltest.test_custom_battle import Mon

FIELDS = (
    "command status controller create seed active ticks ko_count pause_ticks resume force_ko "
    "phase exp level hp max_hp dvs live_hp live_max_hp live_attack live_base_attack "
    "attack_stage state_address state_size completed_crc interrupted post_pool_crc "
    "logic_steps presentation_steps native_max_hp native_dvs live_dvs stage "
    "pool_free arena_archive arena_count arena_index arena_required "
    "cold_before cold_after cold_address cold_size cold_busy cold_calls "
    "combat_address combat_size combat_crc "
    "map_before map_before_size map_loaded map_loaded_size map_after map_after_size"
).split()

POOL_COUNTERS = ("gScPoolMinimum", "gScPoolAllocations", "gScPoolFailures", "gScPoolLargest")
BRIDGE_VIEW_FIELDS = (
    "species level exp dvs hp max_hp status moves0 moves1 moves2 moves3 "
    "pp0 pp1 pp2 pp3 stats0 stats1 stats2 stats3 stats4 base0 base1 base2 base3 "
    "stage0 stage1 stage2 stage3 stage4 stage5"
).split()


def read_bridge(emu):
    size = 16 + 24 * len(BRIDGE_VIEW_FIELDS)
    require(emu.read_u32("gScBridgeProbeSize") == size, "Story bridge ABI mismatch")
    values = struct.unpack(f">{size // 4}I", emu.read("gScBridgeProbe", size))
    evidence = dict(zip(("enabled", "action", "boundary", "error"), values[:4]))
    for slot, name in enumerate(("initial_party", "initial_live", "carry_party", "carry_live",
                                 "current_party", "current_live")):
        start = 4 + slot * len(BRIDGE_VIEW_FIELDS)
        evidence[name] = dict(zip(BRIDGE_VIEW_FIELDS, values[start:start + len(BRIDGE_VIEW_FIELDS)]))
    return evidence


def require_bridge(evidence, party, carry=False):
    require(evidence["enabled"] == 1 and not evidence["error"] and
            evidence["boundary"] == (2 if carry else 1), f"Bridge boundary failed: {evidence}")
    prefix = "carry" if carry else "initial"
    require(all(evidence[f"current_{kind}"] == evidence[f"{prefix}_{kind}"]
                for kind in ("party", "live")), f"Fresh native bridge drift: {evidence}")
    require_fixture_party(party, hp=16 if carry else 17, pp=6 if carry else 7,
                          status=0x48 if carry else 0x40)
    mon = party[0]
    require(mon["current_stats"] == [22, 11, 11, 11, 13] and mon["stat_exp"] == [0] * 5 and
            mon["base_stats"] == [45, 49, 49, 45, 65] and mon["growth"] == 3,
            f"Bridge resident canonical stats mismatch: {party}")
    for prefix in ("initial", "carry") if carry else ("initial",):
        for kind in ("party", "live"):
            view = evidence[f"{prefix}_{kind}"]
            require(all(view[key] == value for key, value in dict(
                species=1, level=6, dvs=0x8889, hp=16 if prefix == "carry" else 17,
                max_hp=22, status=0x48 if prefix == "carry" else 0x40).items()),
                f"Bridge {prefix} {kind} scalar mismatch: {view}")
            require([view[f"moves{i}"] for i in range(4)] == [33, 45, 0, 0] and
                    [view[f"pp{i}"] for i in range(4)] ==
                    ([6, 40, 0, 0] if prefix == "carry" else [7, 40, 0, 0]),
                    f"Bridge {prefix} {kind} moves/PP mismatch: {view}")
            require([view[f"stats{i}"] for i in range(5)] ==
                    ([22, 11, 11, 2, 13] if kind == "live" else [22, 11, 11, 11, 13]),
                    f"Bridge {prefix} {kind} canonical stats mismatch: {view}")
            if kind == "live":
                require([view[f"base{i}"] for i in range(4)] == [11, 11, 11, 13] and
                        all(view[f"stage{i}"] == 7 for i in range(6)),
                        f"Bridge {prefix} live stages/base mismatch: {view}")
            else:
                require(view["exp"] == 235, "Bridge original native XP mismatch")


def wait_bridge_boundary(emu, directory, label, boundary, frames):
    try:
        emu.wait(lambda e: read_bridge(e)["boundary"] == boundary or read_bridge(e)["error"],
                 frames=frames, description=f"native bridge boundary {boundary}")
    except HarnessError:
        result = read_probe(emu)
        memory = memory_evidence(emu, result)
        for name, data in (("result", result), ("memory", memory)):
            (directory / f"{label}-failure-{name}.json").write_text(json.dumps(data, indent=2) + "\n")
        (directory / f"{label}-failure-bridge.json").write_text(json.dumps(read_bridge(emu), indent=2) + "\n")
        raw, party = decode_party(emu, result)
        (directory / f"{label}-failure-state.bin").write_bytes(raw)
        (directory / f"{label}-failure-party.json").write_text(json.dumps(party, indent=2) + "\n")
        raise


def decode_party(emu, result):
    require(emu.read_u32("gScProbeMonSize") == ct.sizeof(Mon), "Story party ABI mismatch")
    offset = emu.read_u32("gScProbePartyOffset")
    count_offset = emu.read_u32("gScProbePartyCountOffset")
    require(0 <= count_offset < offset < result["state_size"], "Story resident offsets mismatch")
    raw = emu.read(result["state_address"], result["state_size"])
    count = raw[count_offset]
    require(1 <= count <= 6 and offset + count * ct.sizeof(Mon) <= result["state_size"],
            "Story resident party bounds mismatch")
    party = []
    for slot in range(count):
        start = offset + slot * ct.sizeof(Mon)
        mon_raw = raw[start:start + ct.sizeof(Mon)]
        party.append(decode_mon(mon_raw))
    return raw, party


def decode_mon(raw):
    mon = Mon.from_buffer_copy(raw)
    return dict(species=mon.species, level=mon.level, exp=mon.exp, dvs=mon.dvs,
                hp=mon.cur_hp, max_hp=mon.max_hp, fainted=mon.fainted,
                status=mon.status, moves=list(mon.moves), pp=list(mon.pp),
                max_pp=list(mon.max_pp), growth=mon.growth,
                stat_exp=list(mon.stat_exp), base_stats=list(mon.base_stats),
                current_stats=list(mon.current_stats), held_relic=mon.held_relic,
                nickname=raw[Mon.nickname.offset:Mon.nickname.offset + 11].hex())


def read_progress(emu):
    offset = 19 * 4
    size = offset + 5 * ct.sizeof(Mon)
    require(emu.read_u32("gScProgressProbeSize") == size and
            emu.read_u32("gScProgressMonOffset") == offset, "Story progression ABI mismatch")
    raw = emu.read("gScProgressProbe", size)
    words = struct.unpack(">19I", raw[:offset])
    evidence = dict(zip(("ko_captures", "resumes", "terminal", "error", "injected",
                         "award", "learn_move"), words[:7]))
    evidence["ko_stages"], evidence["resume_stages"] = list(words[7:13]), list(words[13:19])
    for i, name in enumerate(("before_ko", "after_ko", "after_resume",
                              "terminal_resident", "terminal_native")):
        start = offset + i * ct.sizeof(Mon)
        evidence[name] = decode_mon(raw[start:start + ct.sizeof(Mon)])
    return evidence


def require_ko_progress(evidence, synthetic, terminal=False):
    require(not evidence["error"] and evidence["ko_captures"] == 1 and
            evidence["injected"] == int(synthetic) and evidence["award"] == 69 and
            evidence["learn_move"] == 73, f"KO attribution mismatch: {evidence}")
    before, after = evidence["before_ko"], evidence["after_ko"]
    require(before["exp"] == 235 and before["level"] == 6 and
            before["stat_exp"] == [0] * 5 and
            after["exp"] == 304 and after["level"] == 7 and after["dvs"] == 0x8889 and
            after["stat_exp"] == [39, 52, 43, 65, 50] and
            after["current_stats"] == [24, 13, 13, 12, 15] and
            after["hp"] == before["hp"] + 2 and after["max_hp"] == 24 and
            after["moves"] == before["moves"] and after["pp"] == before["pp"] and
            after["status"] == before["status"], f"Exactly-once canonical KO mismatch: {evidence}")
    if synthetic:
        require(evidence["ko_stages"][0] == 8, "Injected +1 Attack stage was not retained")
    if terminal:
        require(evidence["resumes"] == 1 and evidence["terminal"] == 1 and
                evidence["resume_stages"] == evidence["ko_stages"],
                "Learning resume/stage preservation mismatch")
        resumed = evidence["after_resume"]
        require(resumed == {**after, "moves": [33, 45, 0, 73],
                            "pp": after["pp"][:3] + [10], "max_pp": [35, 40, 0, 10]},
                "Learning changed unrelated state or failed move/PP publication")
        native, resident = evidence["terminal_native"], evidence["terminal_resident"]
        keys = ("species", "level", "exp", "dvs", "hp", "max_hp", "status", "fainted",
                "stat_exp", "current_stats", "moves", "pp", "max_pp")
        require(all(native[key] == resident[key] for key in keys),
                "Terminal native/result resident carry mismatch")
        require(resident["exp"] == 304 and resident["stat_exp"] == after["stat_exp"],
                "Duplicate KO replay altered XP/stat experience")


def require_resume_native(progress, native):
    mon = progress["after_resume"]
    party, live = native["carry_party"], native["carry_live"]
    require(all(party[key] == mon[key] for key in
                ("species", "level", "exp", "dvs", "hp", "max_hp", "status")) and
            [party[f"stats{i}"] for i in range(5)] == mon["current_stats"] and
            [party[f"moves{i}"] for i in range(4)] == mon["moves"] and
            [party[f"pp{i}"] for i in range(4)] == mon["pp"],
            "Resumed original native party publication mismatch")
    require(all(live[key] == mon[key] for key in
                ("species", "level", "dvs", "hp", "max_hp", "status")) and
            [live[f"moves{i}"] for i in range(4)] == mon["moves"] and
            [live[f"pp{i}"] for i in range(4)] == mon["pp"] and
            [live[f"stage{i}"] for i in range(6)] == progress["resume_stages"],
            "Resumed live moves/PP/stages publication mismatch")
    scale = (25, 28, 33, 40, 50, 66, 100, 150, 200, 250, 300, 350, 400)
    for i in range(4):
        stage = progress["resume_stages"][i]
        require(1 <= stage <= 13, "Invalid resumed stage")
        value = max(1, min(999, mon["current_stats"][i + 1] * scale[stage - 1] // 100))
        if i == 0 and mon["status"] & 0x10:
            value //= 2
        if i == 2 and mon["status"] & 0x40:
            value //= 4
        require(live[f"base{i}"] == mon["current_stats"][i + 1] and
                live[f"stats{i + 1}"] == max(1, value),
                "Resumed live canonical stats/status/stage mismatch")


def capture_progress(emu, directory, label):
    result = read_probe(emu)
    memory = memory_evidence(emu, result)
    for name, data in (("result", result), ("memory", memory)):
        (directory / f"{label}-{name}.json").write_text(json.dumps(data, indent=2) + "\n")
    progress = read_progress(emu)
    (directory / f"{label}-progress.json").write_text(json.dumps(progress, indent=2) + "\n")
    (directory / f"{label}-native.json").write_text(json.dumps(read_bridge(emu), indent=2) + "\n")
    raw, party = decode_party(emu, result)
    (directory / f"{label}-state.bin").write_bytes(raw)
    (directory / f"{label}-party.json").write_text(json.dumps(party, indent=2) + "\n")
    return result, progress, raw, party, memory


def require_fixture_party(party, hp=17, pp=7, status=4):
    require(len(party) == 1, "Checkpoint fixture party count mismatch")
    mon = party[0]
    require(mon["species"] == 1 and mon["hp"] == hp and mon["max_hp"] == 22 and
            mon["level"] == 6 and mon["exp"] == 235 and mon["dvs"] == 0x8889 and
            mon["fainted"] == 0 and mon["status"] == status and mon["pp"][0] == pp and
            mon["moves"] == [33, 45, 0, 0] and mon["max_pp"] == [35, 40, 0, 0] and
            mon["pp"][1:] == [40, 0, 0],
            f"Decoded discriminating checkpoint mismatch: {party}")


def require_checkpoint_party(party):
    require_fixture_party(party)


def stack_written_margin(data):
    for offset, value in enumerate(data):
        if value != 0xA5:
            return offset
    return len(data)


def memory_evidence(emu, result):
    base = emu.symbols.address("D_80080000")
    top = emu.symbols.address("D_800818E0")
    require(top - base == 0x18E0, "Game stack boundary changed")
    stack = emu.read(base, top - base)
    margin = stack_written_margin(stack)
    counters = {name: emu.read_u32(name) for name in POOL_COUNTERS}
    fragment = struct.unpack(">II", emu.read(emu.symbols.address("gFragments") + 0xE9 * 8, 8))
    evidence = {
        "stack_base": base, "stack_size": len(stack), "stack_written_margin": margin,
        "stack_written_high_water": len(stack) - margin,
        "stack_scope": "since Game thread creation; writes, not exhaustive reserved-SP bound",
        "fragment_vaddr": fragment[0], "fragment_size": fragment[1],
        "pool_scope": "raw availability after every main-pool allocation attempt since command",
        **counters,
    }
    before = emu.story_stack_before
    require(len(before) == len(stack), "Missing precommand Game stack snapshot")
    evidence["stack_precommand_margin"] = stack_written_margin(before)
    evidence["stack_changed_bytes"] = sum(a != b for a, b in zip(before, stack))
    evidence["stack_new_bottom_writes"] = sum(
        before[i] == 0xA5 and stack[i] != 0xA5
        for i in range(evidence["stack_precommand_margin"]))
    for label, symbol in (("first_zero", "gScPoolZero"), ("first_failure", "gScPoolFailure")):
        values = struct.unpack(">10I", emu.read(emu.symbols.address(symbol), 40))
        evidence[label] = dict(zip(
            ("requested", "charged", "side", "before", "after", "caller", "stage", "cold_busy",
             "address", "active"), values))
    return evidence


def validate_memory(evidence, result):
    margin = evidence["stack_written_margin"]
    fragment = (evidence["fragment_vaddr"], evidence["fragment_size"])
    require(margin >= 64, f"Game stack bottom sentinel exhausted: {evidence}")
    require(fragment == (0, 0), f"Cold fragment map survived pool pop: {evidence}")
    require(not result["map_before"] and not result["map_before_size"] and
            result["map_loaded"] and result["map_loaded_size"] == result["cold_size"] and
            not result["map_after"] and not result["map_after_size"],
            f"Cold map load/pop ownership mismatch: {result}")
    require(evidence["gScPoolAllocations"] > 0 and evidence["gScPoolFailures"] == 0 and
            evidence["gScPoolMinimum"] != 0xFFFFFFFF,
            f"Allocation diagnostics failed: {evidence}")


def verify_mailbox_abi(emu):
    compiled = emu.read_u32("gScProbeCompiledSize")
    require(compiled == len(FIELDS) * 4,
            f"Stale compiled mailbox ABI: {compiled} bytes, expected {len(FIELDS) * 4}")
    base = emu.symbols.address("gScProbe")
    active = emu.symbols.address("gScProbeActive")
    require(active - base == len(FIELDS) * 4,
            f"Stale target mailbox ABI: {active - base} bytes, expected {len(FIELDS) * 4}")


def read_probe(emu):
    data = emu.read(emu.symbols.address("gScProbe"), len(FIELDS) * 4)
    return dict(zip(FIELDS, struct.unpack(f">{len(FIELDS)}I", data)))


def set_field(emu, name, value):
    emu.write_u32(emu.symbols.address("gScProbe") + FIELDS.index(name) * 4, value)


def start_command(emu, command, create=0):
    verify_mailbox_abi(emu)
    base = emu.symbols.address("D_80080000")
    top = emu.symbols.address("D_800818E0")
    emu.story_stack_before = emu.read(base, top - base)
    for name in POOL_COUNTERS:
        emu.write_u32(emu.symbols.address(name), 0xFFFFFFFF if name == "gScPoolMinimum" else 0)
    for name in ("gScPoolZero", "gScPoolFailure"):
        emu.write(emu.symbols.address(name), bytes(40))
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
                          "allocation", "checkpoint", "checkpoint_reload", "bridge_init", "bridge_carry",
                          "ko_synthetic", "ko_natural", "ko_reload"):
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
        elif args.phase in ("reload", "interrupted_reload", "checkpoint_reload", "ko_reload"):
            result = command(emu, 1)
            interrupted = args.phase != "reload"
            if args.phase not in ("checkpoint_reload", "ko_reload"):
                require(result["status"] == 0 and bool(result["interrupted"]) == interrupted and
                        result["level"] == (6 if interrupted else 7) and
                        result["phase"] == (0 if interrupted else 3),
                        f"Fresh-process checkpoint did not reload: {result}")
                require(save_ram(emu)[1] == before, "Cold load modified storage")
        elif args.phase == "checkpoint":
            base = find_pak(before)
            result = command(emu, 5)
            after = save_ram(emu)[1]
            (directory / "save-ram.bin").write_bytes(after)
        elif args.phase in ("bridge_init", "bridge_carry"):
            start_command(emu, 6)
            wait_bridge_boundary(emu, directory, args.phase, 1, 1800)
            initial_bridge = read_bridge(emu)
            initial_result = read_probe(emu)
            initial_memory = memory_evidence(emu, initial_result)
            raw, initial_party = decode_party(emu, initial_result)
            (directory / f"{args.phase}-initial-state.bin").write_bytes(raw)
            for label, data in (("bridge", initial_bridge), ("result", initial_result),
                                ("memory", initial_memory), ("party", initial_party)):
                (directory / f"{args.phase}-initial-{label}.json").write_text(json.dumps(data, indent=2) + "\n")
            (directory / f"{args.phase}-prebattle-save.bin").write_bytes(save_ram(emu)[1])
            require_bridge(initial_bridge, initial_party)
            story_owned_changes(before, save_ram(emu)[1], find_pak(before))
            validate_memory(initial_memory, initial_result)
            if args.phase == "bridge_carry":
                emu.write_u32(emu.symbols.address("gScBridgeProbe") + 4, 1)
                wait_bridge_boundary(emu, directory, args.phase, 2, 120)
            emu.step(60)
            result = read_probe(emu)
            bridge_evidence = read_bridge(emu)
            (directory / f"{args.phase}-bridge.json").write_text(json.dumps(bridge_evidence, indent=2) + "\n")
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
            require(result["hp"] == result["live_hp"] == result["live_max_hp"] ==
                    result["native_max_hp"] == 22 and
                    result["dvs"] == result["native_dvs"] == result["live_dvs"] == 0x8889,
                    f"Canonical first-tick record mismatch: {result}")
            require(not result["cold_busy"] and not result["cold_address"] and
                    result["cold_calls"] == 1 and result["cold_before"] == result["cold_after"],
                    f"Cold preparation is still owned during battle: {result}")
            after = save_ram(emu)[1]
            story_owned_changes(before, after, base)
            (directory / "save-ram.bin").write_bytes(after)
        elif args.phase in ("battle", "natural", "ko_synthetic", "ko_natural"):
            synthetic = args.phase in ("battle", "ko_synthetic")
            base = find_pak(before)
            start_command(emu, 3)
            try:
                wait_probe(emu, lambda result: result["active"] and result["ticks"],
                           1800, "resident fragment-62 probe")
            except HarnessError:
                capture_progress(emu, directory, f"{args.phase}-failure")
                raise
            initial_result, _, _, _, initial_memory = capture_progress(
                emu, directory, f"{args.phase}-entry")
            (directory / f"{args.phase}-prebattle-save.bin").write_bytes(save_ram(emu)[1])
            validate_memory(initial_memory, initial_result)
            if synthetic:
                require(initial_result["max_hp"] == 22, "GB HP-DV fixture did not start at HP22")
                set_field(emu, "force_ko", 1)
            try:
                wait_probe(emu, lambda result: result["phase"] == 2, 6000, "per-KO learning pause")
            except HarnessError:
                capture_progress(emu, directory, f"{args.phase}-failure")
                raise
            paused, progress, resident, paused_party, paused_memory = capture_progress(
                emu, directory, f"{args.phase}-learning")
            require_ko_progress(progress, synthetic)
            validate_memory(paused_memory, paused)
            paused = read_probe(emu)
            require(paused["ko_count"] == 1 and paused["level"] == 7 and paused["max_hp"] == 24,
                    f"Expected exactly one canonical level-up at the KO: {paused}")
            require(paused["native_max_hp"] == paused["live_max_hp"] == paused["max_hp"] and
                    paused["native_dvs"] == paused["live_dvs"] == paused["dvs"] == 0x8889,
                    f"GB stats/DVs were replaced by native recalculation: {paused}")
            if synthetic:
                require(paused["attack_stage"] == 8 and
                        paused["live_attack"] == paused["live_base_attack"] * 150 // 100,
                        f"KO refresh lost the synthetic +1 stage: {paused}")
            address, size = paused["state_address"], paused["state_size"]
            require(0x80000000 <= address < address + size <= map_value(args.map, "heap_BSS_START"),
                    "Story state is not in fixed resident memory before the main pool")
            combat_start, combat_size = paused["combat_address"], paused["combat_size"]
            require(0x80000000 <= combat_start < combat_start + combat_size <=
                    map_value(args.map, "heap_BSS_START") and combat_size < 1024,
                    "Combat view is not small fixed resident value data")
            combat = emu.read(combat_start, combat_size)
            paused_native = read_bridge(emu)
            require(not paused["cold_busy"] and not paused["cold_address"] and
                    paused["cold_calls"] == 1 and paused["cold_before"] == paused["cold_after"],
                    f"Cold preparation did not return/unload before battle: {paused}")
            emu.step(60)
            held, held_progress, held_state, held_party, held_memory = capture_progress(
                emu, directory, f"{args.phase}-held")
            held_native = read_bridge(emu)
            require(held["pause_ticks"] > paused["pause_ticks"], "Learning pause did not pump frames")
            require(held_state == resident and held_party == paused_party and held_progress == progress and
                    held_native == paused_native and
                    all(held_native[f"current_{kind}"] == paused_native[f"initial_{kind}"]
                        for kind in ("party", "live")) and
                    emu.read(combat_start, combat_size) == combat and
                    all(held[key] == paused[key] for key in (
                        "exp", "ko_count", "logic_steps", "presentation_steps", "cold_calls")),
                    "Learning advanced state, XP, presentation or battle actions")
            validate_memory(held_memory, held)
            set_field(emu, "resume", 1)
            try:
                result = finish_command(emu)
            except HarnessError:
                capture_progress(emu, directory, f"{args.phase}-failure")
                raise
            result, final_progress, _, final_party, final_memory = capture_progress(
                emu, directory, f"{args.phase}-terminal")
            after = save_ram(emu)[1]
            (directory / f"{args.phase}-terminal-save.bin").write_bytes(after)
            require_ko_progress(final_progress, synthetic, terminal=True)
            require_resume_native(final_progress, read_bridge(emu))
            require(final_party == [final_progress["terminal_resident"]],
                    "Cold commit changed terminal party persistence")
            validate_memory(final_memory, result)
            require(result["status"] == 0 and result["ko_count"] == 1 and result["phase"] == 3 and
                    result["post_pool_crc"] == result["completed_crc"],
                    f"Battle result was not durably committed after pool return: {result}")
            require(result["cold_calls"] == 2 and not result["cold_busy"] and
                    not result["cold_address"] and result["cold_before"] == result["cold_after"],
                    f"Postbattle cold reload/commit ownership failed: {result}")
            story_owned_changes(before, after, base)
            (directory / "save-ram.bin").write_bytes(after)
            (directory / "completed-party.json").write_text(json.dumps(final_party, indent=2) + "\n")
            (directory / "completed-result.json").write_text(json.dumps(result, indent=2) + "\n")
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
        if args.phase in ("create", "interrupt", "interrupted_reload", "battle", "natural", "reload",
                          "checkpoint", "checkpoint_reload", "bridge_init", "bridge_carry",
                          "ko_synthetic", "ko_natural", "ko_reload"):
            evidence = memory_evidence(emu, result)
            (directory / f"{args.phase}-memory.json").write_text(json.dumps(evidence, indent=2) + "\n")
            raw, party = decode_party(emu, result)
            (directory / f"{args.phase}-state.bin").write_bytes(raw)
            (directory / f"{args.phase}-party.json").write_text(json.dumps(party, indent=2) + "\n")
            if args.phase in ("checkpoint", "checkpoint_reload"):
                require(result["status"] == 0 and result["phase"] == 0 and
                        not any(result[key] for key in (
                            "active", "ticks", "ko_count", "logic_steps", "presentation_steps")),
                        f"Checkpoint-only command failed or advanced battle: {result}")
                require_checkpoint_party(party)
            if args.phase == "checkpoint":
                story_owned_changes(before, after, base)
            if args.phase == "checkpoint_reload":
                require(result["interrupted"] == 1, "Discriminating checkpoint flag not restored")
                require(save_ram(emu)[1] == before, "Checkpoint load modified storage")
                expected = json.loads((directory / "checkpoint-party.json").read_text())
                require(party == expected, "Decoded checkpoint party changed across fresh reload")
                checkpoint = json.loads((directory / "checkpoint-result.json").read_text())
                require(result["completed_crc"] == checkpoint["completed_crc"],
                        "Checkpoint load changed completed encounter CRC")
            if args.phase == "ko_reload":
                expected_party = json.loads((directory / "completed-party.json").read_text())
                expected_result = json.loads((directory / "completed-result.json").read_text())
                require(result["status"] == 0 and result["phase"] == 3 and not result["interrupted"] and
                        party == expected_party and result["completed_crc"] == expected_result["completed_crc"] and
                        not any(result[key] for key in ("ko_count", "ticks", "logic_steps", "presentation_steps")),
                        f"Decoded completed checkpoint/replay mismatch: {result}")
                require(save_ram(emu)[1] == before, "Completed reload modified storage")
            if args.phase in ("bridge_init", "bridge_carry"):
                require_bridge(bridge_evidence, party, carry=args.phase == "bridge_carry")
                require(result["command"] == 6 and result["active"] == 1 and result["phase"] == 1 and
                        result["status"] == 0xFFFFFFFF and
                        not any(result[key] for key in ("ticks", "ko_count", "logic_steps", "presentation_steps")) and
                        result["completed_crc"] == initial_result["completed_crc"],
                        f"Bridge diagnostic hold advanced battle/accounting: {result}")
                if args.phase == "bridge_init":
                    require(party == initial_party and bridge_evidence == initial_bridge,
                            "Initializer hold changed native/resident boundary")
                else:
                    expected = [{**initial_party[0], "hp": 16, "pp": [6, 40, 0, 0], "status": 0x48}]
                    require(party == expected, "Injected carry changed unrelated resident fields")
                require(save_ram(emu)[1] == (directory / f"{args.phase}-prebattle-save.bin").read_bytes(),
                        "Bridge diagnostic hold changed storage")
            validate_memory(evidence, result)
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
                                          "allocation", "checkpoint", "checkpoint_reload",
                                          "bridge_init", "bridge_carry",
                                          "ko_synthetic", "ko_natural", "ko_reload"))
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
