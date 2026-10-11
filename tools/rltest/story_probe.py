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


def decode_party(emu, result, allow_empty=False):
    require(emu.read_u32("gScProbeMonSize") == ct.sizeof(Mon), "Story party ABI mismatch")
    offset = emu.read_u32("gScProbePartyOffset")
    count_offset = emu.read_u32("gScProbePartyCountOffset")
    require(0 <= count_offset < offset < result["state_size"], "Story resident offsets mismatch")
    raw = emu.read(result["state_address"], result["state_size"])
    count = raw[count_offset]
    require((0 if allow_empty else 1) <= count <= 6 and
            offset + count * ct.sizeof(Mon) <= result["state_size"],
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


EMBER_SCOPE = "pure-host-prehistory-not-native-wins; target-natural-native-battle"
EMBER_PHASES = ("ko_ember", "ko_ember_e2e", "ember_entry", "ember_reload")


class EmberCombatBudget:
    def __init__(self, frame):
        self.start = frame
        self.hold = 0

    def evidence(self, frame):
        used = frame - self.start - self.hold
        require(0 <= used <= 18000, "Ember shared combat budget exceeded")
        return dict(start_frame=self.start, frame=frame, combat_used=used,
                    combat_remaining=18000 - used, combat_limit=18000,
                    render_hold_used=self.hold, render_hold_limit=60)

    def exclude_hold(self, start, end):
        require(not self.hold and end - start == 60,
                "Ember budget excludes only one exact 60-frame render hold")
        self.hold = 60
        self.evidence(end)


def finish_ember_budget_command(emu, budget):
    budget.evidence(emu.frame)
    result = read_probe(emu)
    require(result["command"] == 0, "Ember end-to-end command still pending")
    emu.release("L")
    return result


def ember_reload_expected(source, directory):
    hashes = {
        "save-ram.bin": "8c8b2b3c868b7cf593e03b0bb2c54c6fbc704dc2b2caea42fbb37a6f60ce7822",
        "completed-party.json": "68d17c72dd64415e47fd83b1fbcc17706bef92fb7490c11ed8d6f854d4ffa11e",
        "completed-result.json": "4ca728e8310a383f67d230caa355dc9e840026b73023ab9f4466aff0099faf5b",
        "ko_ember_e2e-terminal-state.bin": "9f4667ed7ecd181a6644900158299cb98adf552e658292f2051ad9463eaf1ed2",
        "evidence.sha256": "6ebf07c4c2e7a93c3ba9ecb08398f75ca4737a572ecf842d82f3c3e350ee2875",
        "grant-provenance.json": "8c569459dc01ded5f99884a76219a8f2f5f0bb8b21b6ef1b2772ddd0c98d2e6f",
    }
    require(source.resolve() != directory.resolve() and not any(directory.iterdir()),
            "Ember reload requires a new empty output separate from immutable source")
    data = {}
    for name, digest in hashes.items():
        data[name] = (source / name).read_bytes()
        require(hashlib.sha256(data[name]).hexdigest() == digest,
                f"Ember reload accepted source hash mismatch: {name}")
    manifest = {}
    for line in data["evidence.sha256"].decode().splitlines():
        digest, name = line.split("  ", 1)
        manifest[name] = digest
    for name in hashes.keys() - {"evidence.sha256"}:
        require(hashlib.sha256(data[name]).hexdigest() == manifest.get(name),
                f"Ember reload expected hash mismatch: {name}")
    raw = data["ko_ember_e2e-terminal-state.bin"]
    party = json.loads(data["completed-party.json"])
    result = json.loads(data["completed-result.json"])
    require(len(raw) == 644 and raw[20] == 3 and raw[21] == 1 and
            result["status"] == 0 and result["phase"] == 3 and not result["interrupted"] and
            result["completed_crc"] == 3785588613,
            "Ember reload source is not a completed reward checkpoint")
    require(party == [decode_mon(raw[168:236])], "Ember reload named/raw party mismatch")
    require(party[0]["exp"] == 437 and party[0]["level"] == 9 and
            party[0]["dvs"] == 0x4528 and party[0]["hp"] == 16 and
            party[0]["status"] == 0 and party[0]["moves"] == [10, 45, 0, 52] and
            party[0]["pp"] == [30, 35, 0, 25] and
            party[0]["stat_exp"] == [169, 148, 160, 188, 110] and
            party[0]["current_stats"] == [26, 16, 13, 17, 14],
            "Ember reload source canonical party mismatch")
    completed = bytearray(64)
    for index in (57, 349, 351):
        completed[index // 8] |= 1 << (index % 8)
    require(raw[38:102] == bytes(completed), "Ember reload source target bits mismatch")
    base = find_pak(data["save-ram.bin"])
    pages = story_owned_changes(data["save-ram.bin"], data["save-ram.bin"], base)
    note = b"".join(data["save-ram.bin"][base + page * 256:base + (page + 1) * 256]
                    for page in pages)
    (directory / "ember_reload-source-note.bin").write_bytes(note)
    (directory / "ember_reload-expected-state.bin").write_bytes(raw)
    codec = ROOT / "build/story/test_story_ember"
    require(codec.is_file(), "Build host story tests before Ember reload codec preflight")
    checked = subprocess.run([str(codec), "--validate-reload",
                              str(directory / "ember_reload-source-note.bin"),
                              str(directory / "ember_reload-expected-state.bin")],
                             capture_output=True, text=True)
    (directory / "ember_reload-codec.log").write_text(checked.stdout + checked.stderr)
    require(checked.returncode == 0, f"Ember reload codec preflight failed: {checked.stderr}")
    for name in hashes:
        (directory / ("source-" + name if name in ("evidence.sha256", "grant-provenance.json")
                      else name)).write_bytes(data[name])
    hashes["host_codec"] = hashlib.sha256(codec.read_bytes()).hexdigest()
    hashes["source_note"] = hashlib.sha256(note).hexdigest()
    return dict(raw=raw, party=party, result=result, save=data["save-ram.bin"],
                hashes=hashes)


def require_ember_reload_idle(result, progress):
    require(not any(result[key] for key in (
                "active", "ticks", "ko_count", "logic_steps", "presentation_steps",
                "pause_ticks", "resume", "force_ko")) and
            not any(progress[key] for key in (
                "ko_captures", "resumes", "terminal", "error", "injected", "award", "learn_move")),
            "Ember completed reload replayed battle/progression")


def require_ember_reload(result, progress, raw, party, before, after, expected):
    require(result["command"] == 0 and result["status"] == 0 and result["phase"] == 3 and
            not result["interrupted"] and result["completed_crc"] == expected["result"]["completed_crc"],
            "Ember completed reload status/phase/identity mismatch")
    require(raw == expected["raw"] and party == expected["party"],
            "Ember completed reload changed full state/reward/RNG/party")
    require_ember_reload_idle(result, progress)
    require(before == after == expected["save"], "Ember completed reload modified storage")
    require(result["cold_calls"] == 1 and not result["cold_busy"] and
            not result["cold_address"] and result["cold_before"] == result["cold_after"],
            "Ember completed reload cold ownership/pool mismatch")


def ember_reload_only(emu, directory, before, expected):
    start = emu.frame
    battle_count = emu.read_u32("gRlBattleCount")
    frames = dict(setup_end_frame=start, command_launch_end_frame=None, load_end_frame=None,
                  load_wait_limit=6000, setup_scope="boot/native query excluded from probe accounting",
                  operation_scope="launch transition frames included; no combat or cleanup steps")
    base = emu.symbols.address("D_80080000")
    top = emu.symbols.address("D_800818E0")
    emu.story_stack_before = emu.read(base, top - base)
    try:
        require(before == expected["save"], "Boot stabilization changed completed reload source")
        (directory / "ember_reload-preload-save.bin").write_bytes(before)
        start_command(emu, 1)
        frames["command_launch_end_frame"] = emu.frame
        def ready(instance):
            result = read_probe(instance)
            progress = read_progress(instance)
            with (directory / "ember_reload-load-history.jsonl").open("a") as history:
                history.write(json.dumps(dict(frame=instance.frame, result=result, progress=progress)) + "\n")
            require_ember_reload_idle(result, progress)
            require(not instance.read_u32("gScProbeActive") and
                    not instance.read_u32("gRlBattleActive") and
                    instance.read_u32("gRlBattleCount") == battle_count,
                    "Ember completed reload launched/replayed a native battle")
            require(result["status"] == 0xFFFFFFFF or result["status"] == 0,
                    f"Ember completed load failed: {result}")
            return result["command"] == 0
        emu.wait(ready, frames=6000, description="Ember completed read-only load")
        result, progress, raw, party, memory = capture_progress(emu, directory, "ember_reload-loaded")
        after = save_ram(emu)[1]
        require_ember_reload(result, progress, raw, party, before, after, expected)
        validate_memory(memory, result)
        emu.release("L")
        return result
    except HarnessError:
        capture_progress(emu, directory, "ember_reload-failure", allow_empty=True)
        raise
    finally:
        frames["load_end_frame"] = emu.frame
        frames["operation_frames"] = emu.frame - start
        frames["native_battle_count_before"] = battle_count
        frames["native_battle_count_after"] = emu.read_u32("gRlBattleCount")
        (directory / "ember_reload-frames.json").write_text(json.dumps(frames, indent=2) + "\n")

def require_ember_progress(evidence, terminal=False):
    require(not evidence["error"] and evidence["ko_captures"] == 1 and
            evidence["injected"] == 0 and evidence["award"] == 99 and
            evidence["learn_move"] == 52, f"Ember KO attribution mismatch: {evidence}")
    before, after = evidence["before_ko"], evidence["after_ko"]
    require(before["species"] == after["species"] == 4 and
            before["dvs"] == after["dvs"] == 0x4528 and
            before["exp"] == 338 and before["level"] == 8 and
            before["max_hp"] == 24 and before["hp"] > 0 and
            before["stat_exp"] == [129, 113, 130, 138, 90] and
            before["current_stats"] == [24, 14, 12, 16, 13] and
            before["moves"] == [10, 45, 0, 0] and
            after["exp"] == 437 and after["level"] == 9 and
            after["stat_exp"] == [169, 148, 160, 188, 110] and
            after["current_stats"] == [26, 16, 13, 17, 14] and
            after["max_hp"] == 26 and after["hp"] == before["hp"] + 2 and
            after["moves"] == before["moves"] and after["pp"] == before["pp"] and
            after["status"] == before["status"],
            f"Ember canonical KO/state preservation mismatch: {evidence}")
    require(after == {**before, "exp": 437, "level": 9, "max_hp": 26,
                      "hp": before["hp"] + 2, "stat_exp": [169, 148, 160, 188, 110],
                      "current_stats": [26, 16, 13, 17, 14]},
            "Ember KO changed unrelated mon fields")
    if terminal:
        require(evidence["resumes"] == 1 and evidence["terminal"] == 1 and
                evidence["resume_stages"] == evidence["ko_stages"],
                "Ember learning resume/stage mismatch")
        require(evidence["after_resume"] ==
                {**after, "moves": [10, 45, 0, 52], "pp": after["pp"][:3] + [25],
                 "max_pp": [35, 40, 0, 25]},
                "Ember publication changed unrelated state or moves/PP")
        native, resident = evidence["terminal_native"], evidence["terminal_resident"]
        keys = ("species", "level", "exp", "dvs", "hp", "max_hp", "status", "fainted",
                "stat_exp", "current_stats", "moves", "pp", "max_pp")
        require(all(native[key] == resident[key] for key in keys) and
                resident["exp"] == 437 and resident["stat_exp"] == after["stat_exp"] and
                resident["moves"] == [10, 45, 0, 52],
                "Ember terminal native/resident carry or duplicate KO mismatch")


def require_ember_entry(result, raw, party):
    require(len(party) == 1 and result["phase"] == 1 and result["ko_count"] == 0,
            "Ember target did not start as a fresh singleton battle")
    mon = party[0]
    require(mon["species"] == 4 and mon["dvs"] == 0x4528 and mon["level"] == 8 and
            mon["exp"] == 338 and mon["hp"] == mon["max_hp"] == 24 and
            not mon["status"] and not mon["fainted"] and
            mon["moves"] == [10, 45, 0, 0] and mon["pp"] == mon["max_pp"] == [35, 40, 0, 0] and
            mon["stat_exp"] == [129, 113, 130, 138, 90] and
            mon["current_stats"] == [24, 14, 12, 16, 13],
            f"Ember pure checkpoint identity mismatch: {party}")
    completed = bytearray(64)
    for index in (57, 349):
        completed[index // 8] |= 1 << (index % 8)
    require(raw[38:102] == bytes(completed) and
            raw[102:166] == bytes(64) and
            struct.unpack_from(">4I", raw)[0:2] == (42, 476557059) and
            struct.unpack_from(">4I", raw)[3] == 1 and
            raw[16:20] == bytes([0, 0, 0, 4]) and raw[22] == 0 and
            struct.unpack_from(">H", raw, 36)[0] == 351,
            "Ember graph prehistory or target encounter mismatch")


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


def capture_progress(emu, directory, label, allow_empty=False):
    result = read_probe(emu)
    (directory / f"{label}-save.bin").write_bytes(save_ram(emu)[1])
    memory = memory_evidence(emu, result)
    for name, data in (("result", result), ("memory", memory)):
        if name == "result" and label.startswith(("ko_ember-", "ko_ember_e2e-", "ember_entry-", "ember_reload-")):
            data = {**data, "coverage_scope": EMBER_SCOPE}
        (directory / f"{label}-{name}.json").write_text(json.dumps(data, indent=2) + "\n")
    progress = read_progress(emu)
    (directory / f"{label}-progress.json").write_text(json.dumps(progress, indent=2) + "\n")
    (directory / f"{label}-native.json").write_text(json.dumps(read_bridge(emu), indent=2) + "\n")
    raw, party = decode_party(emu, result, allow_empty=allow_empty)
    (directory / f"{label}-state.bin").write_bytes(raw)
    (directory / f"{label}-party.json").write_text(json.dumps(party, indent=2) + "\n")
    if result["active"]:
        (directory / f"{label}-machine.json").write_text(
            json.dumps(read_native_machine(emu), indent=2) + "\n")
    return result, progress, raw, party, memory


def read_native_machine(emu):
    def read(address, size):
        require(0x80000000 <= address < address + size <= 0x80400000,
                f"Native machine read outside granted cap4: {address:#x}+{size:#x}")
        return emu.read(address, size)

    machine_address = emu.read_u32(emu.resolve("D_84390240"))
    machine = read(machine_address, 0xE0)
    word = lambda data, offset: struct.unpack_from(">I", data, offset)[0]
    half = lambda data, offset: struct.unpack_from(">H", data, offset)[0]
    # Exact compiled 2b78aa5 object layout: timer/choice/pending relative to D_84390124.
    ai = read(emu.resolve("D_84390124") - 4, 0x20)
    port_address = emu.resolve("gControllers")
    port = read(port_address, 0x24)
    pad_address = word(port, 0x20)
    pad = read(pad_address, 6)
    thread = read(emu.resolve("pThreads"), 0x128)
    result = {
        "address": machine_address, "raw": machine.hex(),
        "mode": word(machine, 0x1C), "step": word(machine, 0x20),
        "actor": half(machine, 0x2C), "sequence": word(machine, 0x30),
        "action": word(machine, 0x38), "result": word(machine, 0x40),
        "substep": word(machine, 0x3C), "secondary": word(machine, 0x34),
        "effect": word(machine, 0x48), "hit_state": machine[0x1A],
        "continuation": word(machine, 0x4C),
        "native_hold": emu.read_u32(emu.resolve("D_84390288")),
        "actor_side": emu.read_u32(emu.resolve("D_84390204")),
        "target_side": emu.read_u32(emu.resolve("D_84390200")),
        "saved_thread_context": {"state": half(thread, 16), "sp": word(thread, 244),
                                 "ra": word(thread, 260), "pc": word(thread, 284),
                                 "cause": word(thread, 288), "badvaddr": word(thread, 292)},
        "ai_timer_bits": word(ai, 0), "ai_choice": word(ai, 0x18),
        "ai_pending": word(ai, 0x1C),
        "actor_mon": emu.read_u32(emu.resolve("D_843C5238")),
        "target_mon": emu.read_u32(emu.resolve("D_843C523C")),
        "port0": {"address": port_address, "raw": port.hex(),
                  "down": half(port, 6), "pressed": half(port, 8),
                  "pad_address": pad_address, "pad_raw": pad.hex(),
                  "pad_button": half(pad, 0)},
        "sides": [],
    }
    sides_address = emu.resolve("D_84390010")
    for index in range(2):
        address = emu.read_u32(sides_address + 4 * index)
        control = read(address + 0x654, 0xCC)
        team_address = emu.read_u32(address + 0x724)
        team = read(team_address, 0x21C)
        count, active = team[2], word(control, 8)
        require(0 < count <= 6 and active < count, "Native machine party bounds mismatch")
        hp = half(control, 0x44)
        party_hp = [half(team, 0x1E + i * 0x54) for i in range(count)]
        controller_address = word(control, 0xC8)
        controller = read(controller_address, 0x0C)
        result["sides"].append({
            "address": address, "control_raw": control.hex(),
            "team_address": team_address, "team_raw": team.hex(),
            "flags": team[0], "port": team[1],
            "active": active, "count": count, "hp": hp, "party_hp": party_hp,
            "living": sum(value != 0 for value in party_hp),
            "choice": word(control, 4), "menu": word(control, 0x10),
            "move_slot": word(control, 0x14), "state": control[0x2D],
            "queued_move": control[0x92],
            "controller": controller_address,
            "buttons_down": half(controller, 6), "buttons_pressed": half(controller, 8),
            "moves": list(control[0x57:0x5B]),
            "pp": [value & 63 for value in control[0x6A:0x6E]],
            "status": control[0x4D], "volatile": list(control[0x84:0x8C]),
            "presentation_flags": half(control, 0x34),
            "stages": list(control[0x94:0x9A]),
            "party_pp": [value & 63 for value in
                         team[0x3C + active * 0x54:0x40 + active * 0x54]],
            "pending_hp": [half(control, 0x46), half(control, 0x48)],
        })
    return result


def synthetic_selector_input(probe, machine):
    if (not probe["active"] or probe["phase"] != 1 or probe["resume"] or
            machine["result"] or machine["mode"] != 1 or machine["sequence"] != 0):
        return None
    player = machine["sides"][0]
    if player["flags"] & 2 or player["port"] != 0 or not player["hp"]:
        return None
    if player["menu"] == 0 and player["choice"] == 0:
        return "fight"
    if player["menu"] == 1 and player["choice"] == 1:
        require(0 < player["moves"][0] <= 165 and player["pp"][0] > 0,
                "Synthetic selector requires usable native move slot 0")
        return "move0"
    return None


def finish_synthetic_command(emu, directory):
    end = emu.frame + 6000
    previous = None
    remaining = 0
    held = None
    with (directory / "ko_synthetic-machine-history.jsonl").open("w") as history:
        try:
            while True:
                probe = read_probe(emu)
                if not probe["command"]:
                    break
                require(probe["status"] == 0xFFFFFFFF or probe["status"] < 0x100,
                        f"Synthetic native command failed: {probe}")
                require(emu.frame < end, "Timed out waiting for story probe native command")
                require(probe["active"] and
                        (probe["phase"] == 1 or (probe["phase"] == 2 and probe["resume"] == 1)),
                        f"Unexpected synthetic post-resume phase: {probe}")
                machine = read_native_machine(emu)
                controls = (machine, {key: probe[key] for key in
                                     ("ticks", "logic_steps", "presentation_steps", "pause_ticks")})
                # Keep transition history durable before attempting any controller input.
                if controls != previous or (emu.frame % 120 == 0):
                    history.write(json.dumps({"frame": emu.frame, "probe": probe,
                                              "machine": machine}) + "\n")
                    history.flush()
                    previous = controls
                eligible = synthetic_selector_input(probe, machine)
                if held and eligible != held:
                    if held == "fight":
                        emu.release("N64_A")
                    else:
                        emu.stick(right=True)
                    held = None
                    remaining = 12
                if remaining:
                    remaining -= 1
                    if not remaining and held:
                        if held == "fight":
                            emu.release("N64_A")
                        else:
                            emu.stick(right=True)
                        held = None
                        remaining = 12
                else:
                    held = eligible
                    if held:
                        history.write(json.dumps({"frame": emu.frame, "input": held}) + "\n")
                        history.flush()
                        if held == "fight":
                            emu.hold("N64_A")
                        else:
                            emu.stick(0, -32767, right=True)
                        remaining = 12
                emu.step()
        finally:
            emu.release("N64_A")
            emu.stick(right=True)
    return finish_command(emu)


def wait_natural_probe(emu, directory, learning, phase_name="ko_natural", budget=None):
    label = "prelearning" if learning else "postresume"
    description = "per-KO learning pause" if learning else "story probe native command"
    with (directory / f"{phase_name}-{label}-machine-history.jsonl").open("w") as history:
        def ready(instance):
            probe = read_probe(instance)
            progress = read_progress(instance)
            machine = read_native_machine(instance) if probe["active"] else None
            history_probe = ({**probe, "coverage_scope": EMBER_SCOPE}
                             if phase_name in EMBER_PHASES else probe)
            row = {"frame": instance.frame, "probe": history_probe,
                   "progress": progress, "machine": machine}
            if budget is not None:
                row["budget"] = budget.evidence(instance.frame)
            history.write(json.dumps(row) + "\n")
            history.flush()
            require(not probe["force_ko"] and not progress["injected"],
                    "Natural KO observed a synthetic force request or injection")
            require(not progress["error"] and
                    (probe["status"] == 0xFFFFFFFF or probe["status"] < 0x100),
                    f"Natural {label} failed: {probe}")
            if learning:
                require(probe["command"] != 0,
                        f"Natural command ended without learning: {probe}")
                return probe["phase"] == 2
            return probe["command"] == 0

        frames = budget.evidence(emu.frame)["combat_remaining"] if budget is not None else 6000
        try:
            emu.wait(ready, frames=frames, description=description)
        finally:
            if budget is not None:
                (directory / f"{phase_name}-{label}-budget.json").write_text(
                    json.dumps(budget.evidence(emu.frame), indent=2) + "\n")


def require_learning_storage(prebattle, paused, held):
    require(paused == prebattle, "Learning pause changed durable prebattle storage")
    require(held == prebattle, "Learning hold changed durable prebattle storage")


def require_probe_profile(map_path, phase):
    contents = Path(map_path).read_text()
    require(("gScProbeEmberProfile" in contents) == (phase in EMBER_PHASES),
            "Ember scenario requires its separate profile; original scenarios refuse it")
    if phase in ("bridge_init", "bridge_carry"):
        require("gScProbeKoProfile" not in contents,
                "Bridge phases unavailable in STORY_KO_PROBE profile")


def require_ember_native_entry(machine):
    require(len(machine["sides"]) == 2, "Ember entry native side count mismatch")
    for side, expected in zip(machine["sides"], (
            (4, 8, 0x4528, 24, [24, 14, 12, 16, 13], [10, 45, 0, 0], [35, 40, 0, 0]),
            (13, 9, 0x8889, 27, [27, 12, 11, 15, 10], [40, 81, 0, 0], [35, 40, 0, 0]))):
        species, level, dvs, hp, stats, moves, pp = expected
        party = bytes.fromhex(side["team_raw"])[0x1C:0x70]
        live = bytes.fromhex(side["control_raw"])[0x38:]
        half = lambda data, offset: struct.unpack_from(">H", data, offset)[0]
        require(side["flags"] & 2 and side["count"] == 1 and side["active"] == 0 and
                side["hp"] == hp and side["party_hp"] == [hp] and
                side["moves"] == moves and side["pp"] == side["party_pp"] == pp and
                not side["status"] and side["stages"] == [7] * 6,
                "Ember entry native CPU/HP/PP/status/stage mismatch")
        require(party[0] == live[0x0B] == species and
                party[0x24] == live[0x26] == level and
                half(party, 0x1E) == half(live, 0x24) == dvs and
                half(party, 0x26) == half(live, 0x28) == hp and
                [half(party, 0x26 + 2 * i) for i in range(5)] == stats and
                [half(live, 0x28 + 2 * i) for i in range(5)] == stats,
                "Ember entry original/live canonical fields mismatch")


def ember_entry_only(emu, directory, before):
    start_command(emu, 3)
    try:
        wait_probe(emu, lambda result: result["active"] and result["ticks"],
                   1800, "Ember initialized entry only")
        result, progress, raw, party, memory = capture_progress(emu, directory, "ember_entry-entry")
        after = save_ram(emu)[1]
        (directory / "ember_entry-prebattle-save.bin").write_bytes(after)
        require_ember_entry(result, raw, party)
        require(not result["force_ko"] and not result["resume"] and
                not any(progress[key] for key in ("error", "injected", "ko_captures", "resumes", "terminal")),
                "Ember entry unexpectedly advanced KO/learning/force state")
        require(result["cold_calls"] == 1 and not result["cold_busy"] and
                not result["cold_address"] and result["cold_before"] == result["cold_after"],
                "Ember entry cold ownership/pool imbalance")
        validate_memory(memory, result)
        require_ember_native_entry(read_native_machine(emu))
        story_owned_changes(before, after, find_pak(before))
        return result
    except HarnessError:
        capture_progress(emu, directory, "ember_entry-failure")
        raise


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
    require_probe_profile(args.map, args.phase)
    expected_reload = None
    if args.phase == "ember_reload":
        require(args.reload_source is not None, "Ember reload requires immutable --reload-source")
        expected_reload = ember_reload_expected(Path(args.reload_source), directory)
    provenance = {
        "head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "diff_sha256": hashlib.sha256(subprocess.check_output(
            ["git", "diff", "HEAD", "--binary"], cwd=ROOT)).hexdigest(),
        "command": sys.argv,
        "rom_sha256": hashlib.sha256(Path(args.rom).read_bytes()).hexdigest(),
        "map_sha256": hashlib.sha256(Path(args.map).read_bytes()).hexdigest(),
        "core_sha256": hashlib.sha256(Path(args.core).read_bytes()).hexdigest(),
    }
    if args.phase in EMBER_PHASES:
        provenance["coverage_scope"] = EMBER_SCOPE
        provenance["fixture"] = {
            "seed": 42, "pure_wins": [57, 349], "declined_rewards": [57, 349],
            "graph_heals": [0, 0], "skipped_optional": 350, "native_target": 351,
            "prelearning_budget": 6000, "postresume_budget": 6000, "hold_frames": 60,
            "expected_win_is_assertion_not_prediction": True,
        }
        if args.phase == "ko_ember_e2e":
            provenance["fixture"].pop("prelearning_budget")
            provenance["fixture"].pop("postresume_budget")
            provenance["fixture"]["shared_combat_budget"] = 18000
            provenance["fixture"]["budget_scope"] = "entry capture through terminal, excluding exact render hold"
        if expected_reload is not None:
            provenance["fixture"] = {"seed": 42, "completed_target": 351, "load_wait_limit": 6000}
            provenance["reload_expected_hashes"] = expected_reload["hashes"]
            provenance["operation_scope"] = "fresh-process completed LOAD only; boot/setup not battle accounting"
        provenance["save_source_sha256"] = hashlib.sha256(
            (directory / "save-ram.bin").read_bytes()).hexdigest()
        provenance["core_options"] = {"mupen64plus-pak1": "memory",
                                      "mupen64plus-angrylion-multithread": "4"}
        for name in ("ember-host-proof.jsonl", "ember-checkpoint.sc01"):
            source = ROOT / "build/story" / name
            data = source.read_bytes()
            (directory / name).write_bytes(data)
            provenance[name + "_sha256"] = hashlib.sha256(data).hexdigest()
    (directory / f"{args.phase}-provenance.json").write_text(
        json.dumps(provenance, indent=2) + "\n")
    options = {"mupen64plus-pak1": "none" if args.phase == "missing" else "memory",
               "mupen64plus-angrylion-multithread": "4"}
    with Frontend(args.core, args.rom, args.map, directory / args.phase,
                  options=options, timeout=900) as emu, probe_deadline(emu, directory):
        pointer, initial = save_ram(emu)
        if args.phase in ("reload", "interrupted_reload", "interrupt", "battle", "natural",
                          "allocation", "checkpoint", "checkpoint_reload", "bridge_init", "bridge_carry",
                          "ko_synthetic", "ko_natural", "ko_reload", "ko_ember", "ko_ember_e2e", "ember_entry", "ember_reload"):
            restored = (directory / "save-ram.bin").read_bytes()
            require(len(restored) == len(initial), "Save RAM size changed between processes")
            ct.memmove(pointer, restored, len(restored))
        boot(emu)
        # Stabilize vanilla FlashRAM initialization, without invoking any story writer.
        native_command(emu, 1)
        pointer, before = save_ram(emu)
        if args.phase == "ember_reload":
            (directory / "ember_reload-setup.json").write_text(json.dumps(dict(
                frame=emu.frame, save_sha256=hashlib.sha256(before).hexdigest(),
                probe=read_probe(emu), progress=read_progress(emu),
                scope="post boot/native query, before command1 LOAD"), indent=2) + "\n")
            result = ember_reload_only(emu, directory, before, expected_reload)
            (directory / "ember_reload-result.json").write_text(
                json.dumps({**result, "coverage_scope": EMBER_SCOPE,
                            "acceptance_scope": "completed read-only reload only"}, indent=2) + "\n")
            print("story native ember_reload: OK")
            return
        if args.phase == "ember_entry":
            result = ember_entry_only(emu, directory, before)
            (directory / "ember_entry-result.json").write_text(
                json.dumps({**result, "coverage_scope": EMBER_SCOPE,
                            "acceptance_scope": "initialized entry/resources only; no combat continuation"},
                           indent=2) + "\n")
            return
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
        elif args.phase in ("battle", "natural", "ko_synthetic", "ko_natural", "ko_ember", "ko_ember_e2e"):
            synthetic = args.phase in ("battle", "ko_synthetic")
            ember = args.phase in ("ko_ember", "ko_ember_e2e")
            base = find_pak(before)
            start_command(emu, 3)
            try:
                wait_probe(emu, lambda result: result["active"] and result["ticks"],
                           1800, "resident fragment-62 probe")
            except HarnessError:
                capture_progress(emu, directory, f"{args.phase}-failure")
                raise
            initial_result, _, entry_state, entry_party, initial_memory = capture_progress(
                emu, directory, f"{args.phase}-entry")
            (directory / f"{args.phase}-prebattle-save.bin").write_bytes(save_ram(emu)[1])
            validate_memory(initial_memory, initial_result)
            if ember:
                require_ember_entry(initial_result, entry_state, entry_party)
            budget = EmberCombatBudget(emu.frame) if args.phase == "ko_ember_e2e" else None
            if synthetic:
                require(initial_result["max_hp"] == 22, "GB HP-DV fixture did not start at HP22")
                set_field(emu, "force_ko", 1)
            try:
                if args.phase in ("ko_natural", "ko_ember", "ko_ember_e2e"):
                    wait_natural_probe(emu, directory, learning=True, phase_name=args.phase, budget=budget)
                else:
                    wait_probe(emu, lambda result: result["phase"] == 2, 6000, "per-KO learning pause")
            except HarnessError:
                capture_progress(emu, directory, f"{args.phase}-failure")
                raise
            paused, progress, resident, paused_party, paused_memory = capture_progress(
                emu, directory, f"{args.phase}-learning")
            paused_save = save_ram(emu)[1]
            (directory / f"{args.phase}-learning-save.bin").write_bytes(paused_save)
            prebattle_save = (directory / f"{args.phase}-prebattle-save.bin").read_bytes()
            require_learning_storage(prebattle_save, paused_save, paused_save)
            if ember:
                require_ember_progress(progress)
            else:
                require_ko_progress(progress, synthetic)
            validate_memory(paused_memory, paused)
            paused = read_probe(emu)
            require(paused["ko_count"] == 1 and paused["level"] == (9 if ember else 7) and
                    paused["max_hp"] == (26 if ember else 24),
                    f"Expected exactly one canonical level-up at the KO: {paused}")
            require(paused["native_max_hp"] == paused["live_max_hp"] == paused["max_hp"] and
                    paused["native_dvs"] == paused["live_dvs"] == paused["dvs"] ==
                    (0x4528 if ember else 0x8889),
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
            hold_start = emu.frame
            emu.step(60)
            if budget is not None:
                budget.exclude_hold(hold_start, emu.frame)
                (directory / f"{args.phase}-hold-budget.json").write_text(
                    json.dumps(budget.evidence(emu.frame), indent=2) + "\n")
            held, held_progress, held_state, held_party, held_memory = capture_progress(
                emu, directory, f"{args.phase}-held")
            held_save = save_ram(emu)[1]
            (directory / f"{args.phase}-held-save.bin").write_bytes(held_save)
            require_learning_storage(prebattle_save, paused_save, held_save)
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
                if args.phase == "ko_synthetic":
                    result = finish_synthetic_command(emu, directory)
                else:
                    if args.phase in ("ko_natural", "ko_ember", "ko_ember_e2e"):
                        wait_natural_probe(emu, directory, learning=False, phase_name=args.phase, budget=budget)
                    if budget is not None:
                        result = finish_ember_budget_command(emu, budget)
                    else:
                        result = finish_command(emu)
            except HarnessError:
                capture_progress(emu, directory, f"{args.phase}-failure")
                raise
            result, final_progress, terminal_state, final_party, final_memory = capture_progress(
                emu, directory, f"{args.phase}-terminal")
            after = save_ram(emu)[1]
            (directory / f"{args.phase}-terminal-save.bin").write_bytes(after)
            if ember:
                require_ember_progress(final_progress, terminal=True)
                completed = bytearray(64)
                for index in (57, 349, 351):
                    completed[index // 8] |= 1 << (index % 8)
                require(terminal_state[38:102] == bytes(completed),
                        "Ember terminal commit changed unexpected graph completion bits")
            else:
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
            recorded_result = {**result, "coverage_scope": EMBER_SCOPE} if ember else result
            (directory / "completed-result.json").write_text(json.dumps(recorded_result, indent=2) + "\n")
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
        recorded_result = {**result, "coverage_scope": EMBER_SCOPE} if args.phase in EMBER_PHASES else result
        (directory / f"{args.phase}-result.json").write_text(json.dumps(recorded_result, indent=2) + "\n")
        if args.phase in ("create", "interrupt", "interrupted_reload", "battle", "natural", "reload",
                          "checkpoint", "checkpoint_reload", "bridge_init", "bridge_carry",
                          "ko_synthetic", "ko_natural", "ko_reload", "ko_ember", "ko_ember_e2e"):
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
    parser.add_argument("--reload-source", help="Immutable accepted Ember end-to-end evidence directory")
    parser.add_argument("--phase", choices=("create", "interrupt", "interrupted_reload",
                                          "battle", "natural", "reload", "missing", "full",
                                          "allocation", "checkpoint", "checkpoint_reload",
                                          "bridge_init", "bridge_carry",
                                          "ko_synthetic", "ko_natural", "ko_reload", "ko_ember", "ko_ember_e2e",
                                          "ember_entry", "ember_reload"))
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
