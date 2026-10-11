"""Source-reviewed six-party observer; execution requires a separate reservation."""

import copy
import hashlib
import json
import math
import os
import struct
import subprocess

from tools.rltest.frontend import HarnessError


def require(condition, message):
    if not condition:
        raise HarnessError(message)


class SixBudget:
    limit = 18000

    def __init__(self, start):
        self.start = start
        self.holds = {}
        self.resumed = set()

    def evidence(self, frame):
        used = frame - self.start - sum(end - start for start, end in self.holds.values())
        require(0 <= used <= self.limit, "Six-party shared combat budget exceeded")
        return dict(combat_used=used, combat_remaining=self.limit - used,
                    holds=self.holds, resumed=sorted(self.resumed))

    def exclude(self, generation, start, end):
        require(generation > 0 and generation not in self.holds and end - start == 60,
                "Learning exclusion requires a unique generation and exactly 60 frames")
        require(start >= self.start and all(start >= old_end for _, old_end in self.holds.values()),
                "Learning exclusions overlap or precede initialized entry")
        self.holds[generation] = (start, end)
        self.evidence(end)

    def resume(self, generation):
        require(generation in self.holds and generation not in self.resumed,
                "Learning resume requires one verified, not-yet-resumed hold")
        self.resumed.add(generation)


def xp(level, growth):
    cube = level ** 3
    return (cube, 4 * cube // 5, 5 * cube // 4,
            max(0, 6 * cube // 5 - 15 * level ** 2 + 100 * level - 140))[growth]


def stats(mon):
    dvs = mon["dvs"]
    hp_dv = ((dvs & 1) << 3) | (((dvs >> 4) & 1) << 2) | (
        ((dvs >> 8) & 1) << 1) | ((dvs >> 12) & 1)
    values = []
    for index, base in enumerate(mon["base_stats"]):
        root = max(1, min(255, math.isqrt(mon["stat_exp"][index])))
        if root < 255 and root * root < mon["stat_exp"][index]:
            root += 1
        dv = hp_dv if index == 0 else (dvs >> ((index - 1) * 4)) & 15
        values.append(min(999, (2 * (base + dv) + root // 4) * mon["level"] // 100 +
                          (mon["level"] + 10 if index == 0 else 5)))
    return values


def species(raw):
    require(len(raw) == 41 and raw[11] <= 8, "Six combat species ABI mismatch")
    return dict(base=list(raw[:5]), yield_=raw[5], growth=raw[6],
                learn=[tuple(raw[13 + i * 2:15 + i * 2]) for i in range(raw[11])])


def combat_data(raw):
    require(len(raw) == 732 and raw[730] == 1 and raw[729] == 0 and raw[664] == 2,
            "Six combat layout/ready/trainer/team mismatch")
    return dict(party=[species(raw[i * 41:(i + 1) * 41]) for i in range(6)],
                enemy=[species(raw[(6 + i) * 41:(7 + i) * 41]) for i in range(2)],
                levels=[raw[666 + i * 10 + 1] for i in range(2)], pp=list(raw[498:664]))


def require_canonical_combat(combat, party, target, manifest):
    records = {record["id"]: record for record in manifest["species"]}
    for actual, mon in zip(combat["party"] + combat["enemy"], party + target):
        expected = records[mon["species"]]
        require(actual == dict(base=expected["base"], yield_=expected["yield"],
                               growth=expected["growth"], learn=[tuple(item) for item in expected["learn"]]),
                "Compiled combat values differ from frozen audited species data")
    require(combat["pp"] == manifest["pp"] and
            combat["levels"] == [mon["level"] for mon in target],
            "Compiled combat PP or canonical trainer levels differ")


def awarded(before, enemy, level, participants, party_species):
    n = participants.bit_count()
    require(0 < n <= 6, "Invalid six-party award denominator")
    base = (enemy["yield_"] // n) * level // 7
    award = base + base // 2
    result, learning = copy.deepcopy(before), {}
    for index, mon in enumerate(result):
        if not participants & (1 << index) or not mon["hp"]:
            continue
        old = mon["level"]
        mon["stat_exp"] = [min(65535, value + enemy["base"][i] // n)
                           for i, value in enumerate(mon["stat_exp"])]
        mon["exp"] = min(xp(100, mon["growth"]), mon["exp"] + award)
        while mon["level"] < 100 and xp(mon["level"] + 1, mon["growth"]) <= mon["exp"]:
            mon["level"] += 1
        if mon["level"] != old:
            missing = mon["max_hp"] - mon["hp"]
            mon["current_stats"] = stats(mon)
            mon["max_hp"] = mon["current_stats"][0]
            mon["hp"] = max(0, mon["max_hp"] - missing)
            mon["fainted"] = int(mon["hp"] == 0)
            for learn_level, move in party_species[index]["learn"]:
                if learn_level == mon["level"]:
                    if move not in mon["moves"]:
                        learning[index] = move
                    break
    return result, learning, award


class SixLedger:
    def __init__(self, entry, combat):
        self.previous = entry
        self.combat = combat
        self.events = []
        self.observed_slots = set()

    def accept(self, row):
        control, previous = row["control"], self.previous
        generation = control["events"]
        delta = generation - previous["control"]["events"]
        mask, old_mask = row["state"][23], previous["state"][23]
        require(delta in (0, 1) and not old_mask & ~mask and
                (mask ^ old_mask).bit_count() == delta,
                "Unobserved multiple KO/counter jump or defeated-mask replay; refusing reconstruction")
        if row["machine"]:
            active = row["machine"]["sides"][0]["active"]
            self.observed_slots.add(row["mapping"][active])
        if delta:
            event = row["event"]
            enemy = event["enemy"]
            require(event["generation"] == generation and enemy < 2 and
                    mask ^ old_mask == 1 << enemy and event["count"] == 6 and
                    event["participants"] == row["participants"][enemy] and
                    event["participants"] & (1 << event["active"]),
                    "KO event identity/participant proof mismatch")
            before = event["before"]
            for a, b in zip(before, previous["party"]):
                require(all(a[key] == b[key] for key in
                            ("species", "exp", "stat_exp", "level", "moves", "max_pp",
                             "base_stats", "growth", "dvs", "nickname", "held_relic")),
                        "Pre-award snapshot is not from the previous progression generation")
                require(a["fainted"] == int(a["hp"] == 0), "Pre-award fainting mismatch")
            after, learning, award = awarded(before, self.combat["enemy"][enemy],
                                            self.combat["levels"][enemy],
                                            event["participants"], self.combat["party"])
            require(after == row["party"], "Independent all-party XP/statEXP/rounding/bench award mismatch")
            require(row["state"][26] == sum(1 << slot for slot in learning) and
                    all(row["state"][27 + slot] == learning.get(slot, 0) for slot in range(6)),
                    "All-party learning generation mismatch")
            self.events.append(dict(enemy=enemy, participants=event["participants"],
                                    before=before, after=after, learning=learning, award=award,
                                    frame=row["frame"]))
        else:
            require(all(a["exp"] == b["exp"] and a["stat_exp"] == b["stat_exp"]
                        for a, b in zip(row["party"], previous["party"])),
                    "XP/statEXP changed without an individually observed KO")
        resume_delta = control["resumes"] - previous["control"]["resumes"]
        require(resume_delta in (0, 1), "Unobserved multiple learning resumes")
        if resume_delta:
            require(previous["state"][26] and control["pause_id"] == previous["control"]["pause_id"],
                    "Resume without an actual pending learning generation")
            for slot, (a, b) in enumerate(zip(row["party"], previous["party"])):
                moves, max_pp = list(b["moves"]), list(b["max_pp"])
                if previous["state"][26] & (1 << slot):
                    move = previous["state"][27 + slot]
                    moves[3], max_pp[3] = move, self.combat["pp"][move]
                require(a["moves"] == moves and a["max_pp"] == max_pp,
                        "All-party learning publication/replacement mismatch")
        self.previous = row


def native_named(raw):
    half = lambda offset: struct.unpack_from(">H", raw, offset)[0]
    return dict(species=raw[0], hp=half(2), status=raw[5], moves=list(raw[9:13]),
                exp=struct.unpack_from(">I", raw, 16)[0],
                stat_exp=[half(20 + i * 2) for i in range(5)], dvs=half(30),
                pp=[value & 63 for value in raw[32:36]], level=raw[36],
                max_hp=half(38), current_stats=[half(38 + i * 2) for i in range(5)])


def terminal_proof(emu, decode_mon):
    require(emu.read_u32("gScSixTerminalSize") == 696 and
            emu.read_u32("gScSixCombatSize") == 732, "Six terminal ABI mismatch")
    raw = emu.read("gScSixBridgeStorage", 696)
    count, enemies, outcome, valid = struct.unpack_from(">4I", raw)
    require((count, enemies, outcome, valid) == (6, 2, 1, 1),
            "Six terminal raw/count/outcome proof unavailable")
    mapping = list(raw[16:22])
    require(sorted(mapping) == list(range(6)), "Six terminal compact mapping mismatch")
    precommit = emu.read("gScSixCombatStorage", 644)
    party = [decode_mon(precommit[168 + i * 68:236 + i * 68]) for i in range(6)]
    originals = [native_named(raw[24 + i * 84:108 + i * 84]) for i in range(8)]
    for i in range(6):
        mon = party[mapping[i]]
        require(all(originals[i][key] == mon[key] for key in originals[i]),
                "Actual raw native RESULT differs from all-party precommit")
    require(all(not mon["hp"] for mon in originals[6:]), "Terminal foes not exhausted")
    require(emu.read_u32("gScSixLaunchSize") == 828 and
            emu.read_u32("gScSixLaunchPlayerOffset") == 12 and
            emu.read_u32("gScSixLaunchEnemyOffset") == 420, "Six launcher-result compiled ABI mismatch")
    launch = emu.read("gRlLastBattleResult", 828)
    require(list(launch[:3]) == [outcome, count, enemies] and list(launch[3:9]) == mapping,
            "Actual launcher-result outcome/count/index mismatch")
    converted = [decode_mon(launch[12 + i * 68:80 + i * 68]) for i in range(6)]
    converted_enemy = [decode_mon(launch[420 + i * 68:488 + i * 68]) for i in range(2)]
    require(all(mon == party[mapping[i]] for i, mon in enumerate(converted)),
            "Actual converted launcher party differs from precommit")
    for actual, original in zip(converted + converted_enemy, originals):
        require(all(actual[key] == original[key] for key in original),
                "Actual converted launcher-result differs from raw originals")
    return dict(raw=raw.hex(), precommit=precommit.hex(), party=party, mapping=mapping,
                native=originals, launch_raw=launch.hex(), launch_party=converted,
                launch_enemy=converted_enemy)


def run(emu, directory, before, probe):
    """No input, cleanup, retry, continuation after terminal, or prediction of CPU switching."""
    probe.start_command(emu, 3)
    probe.wait_probe(emu, lambda r: r["active"] and r["ticks"], 1800, "six natural initialized entry")
    entry = probe.capture_progress(emu, directory, "six_natural-entry")
    result, progress, raw, party, memory = entry
    mapping = list(emu.read("gScProbeSixMapping", 6))
    machine = probe.read_native_machine(emu)
    probe.require_six_entry(result, progress, raw, party, machine, mapping,
                            emu.read(emu.symbols.address("gRlLastBattleResult") + 3, 6),
                            directory / "six-host-proof.jsonl")
    probe.validate_memory(memory, result)
    prebattle = probe.save_ram(emu)[1]
    (directory / "six_natural-prebattle-save.bin").write_bytes(prebattle)
    require(emu.read_u32("gScSixEventSize") == 416, "Six pre-award event ABI mismatch")
    combat = combat_data(emu.read(result["combat_address"], 732))
    _, _, target = probe.six_expected(directory / "six-host-proof.jsonl")
    require_canonical_combat(combat, party, target, json.loads((directory / "six-data.json").read_text()))
    budget = SixBudget(emu.frame)
    terminal = None

    def capture(history, label):
        nonlocal terminal
        r = probe.read_probe(emu)
        state, mons = probe.decode_party(emu, r)
        control = probe.read_six_control(emu)
        row = dict(frame=emu.frame, label=label, probe=r, control=control,
                   state=state.hex(), party=mons, mapping=list(emu.read("gScProbeSixMapping", 6)),
                   participants=list(emu.read("gScProbeSixBridge", 7)),
                   memory=probe.memory_evidence(emu, r),
                   save_sha256=hashlib.sha256(probe.save_ram(emu)[1]).hexdigest(), machine=None)
        row["bridge_storage_raw"] = emu.read("gScSixBridgeStorage", 736).hex()
        row["combat_storage_raw"] = emu.read("gScSixCombatStorage", 732).hex()
        row["event_storage_raw"] = emu.read("gScSixProgressStorage", 416).hex()
        row["launcher_storage_raw"] = emu.read("gRlLastBattleResult", 828).hex()
        row["budget"] = dict(start=budget.start, frame=emu.frame,
                             accepted_holds=dict(budget.holds), resume_ids=sorted(budget.resumed),
                             tentative_hold=label.startswith("hold-"))
        # Seal owned bytes before transient pointer reads or semantic validators can refuse.
        history.write(json.dumps({**row, "capture": "owned-before-transient"}, separators=(",", ":")) + "\n")
        history.flush()
        os.fsync(history.fileno())
        if control["role"] in (0, 2):
            event = bytes.fromhex(row["event_storage_raw"])
            row["event"] = dict(generation=struct.unpack_from(">I", event)[0],
                                active=event[4], enemy=event[5], participants=event[6], count=event[7],
                                before=[probe.decode_mon(event[8 + i * 68:76 + i * 68]) for i in range(6)])
        if control["role"] == 0:
            row["combat"] = row["combat_storage_raw"]
            row["bridge"] = probe.read_bridge(emu)
            if r["active"]:
                row["machine"] = probe.read_native_machine(emu)
        elif control["role"] == 2:
            terminal = terminal_proof(emu, probe.decode_mon)
            row["terminal"] = terminal
        history.write(json.dumps(row, separators=(",", ":")) + "\n")
        history.flush()
        os.fsync(history.fileno())
        row["state"] = state
        require(not control["error"] and control["role"] in (0, 2) and
                r["ko_count"] == control["events"] and not r["force_ko"],
                "Six natural runtime/event/role refusal")
        require(row["mapping"] == mapping and
                (row["machine"] is None or row["machine"]["sides"][0]["count"] == 6),
                "Six original team count/launcher mapping changed")
        probe.validate_memory(row["memory"], r)
        require(row["memory"]["stack_new_bottom_writes"] == 0, "New writes below precommand stack boundary")
        return row

    with (directory / "six_natural-history.jsonl").open("x") as history:
        row = capture(history, "entry")
        ledger = SixLedger(row, combat)
        while row["probe"]["command"]:
            require(budget.evidence(emu.frame)["combat_remaining"] > 0,
                    "Six-party shared budget exhausted before terminal")
            if row["probe"]["phase"] == 2 and not row["probe"]["resume"]:
                generation = row["control"]["pause_id"]
                require(generation not in budget.holds, "Repeated learning pause generation")
                held = row
                pause_save = probe.save_ram(emu)[1]
                (directory / f"six_natural-learning-{generation}-save.bin").write_bytes(pause_save)
                require(pause_save == prebattle, "Learning changed durable prebattle save")
                start = emu.frame
                for _ in range(60):
                    emu.step(1)
                    row = capture(history, f"hold-{generation}")
                    ledger.accept(row)
                    require(row["state"] == held["state"] and row["party"] == held["party"] and
                            row["control"] == held["control"] and row["bridge"] == held["bridge"] and
                            row["mapping"] == held["mapping"] and
                            row["participants"] == held["participants"] and
                            row["machine"]["sides"] == held["machine"]["sides"] and
                            row["combat"] == held["combat"] and
                            row["save_sha256"] == held["save_sha256"] and
                            all(row["probe"][key] == held["probe"][key] for key in
                                ("logic_steps", "presentation_steps", "ko_count", "cold_calls", "resume")),
                            "Learning hold advanced full party/native/participant/logic state")
                require(row["probe"]["pause_ticks"] > held["probe"]["pause_ticks"],
                        "Verified learning hold did not pump native pause frames")
                budget.exclude(generation, start, emu.frame)
                budget.resume(generation)
                history.write(json.dumps(dict(frame=emu.frame, capture="verified-hold-and-resume-once",
                                               generation=generation, budget=budget.evidence(emu.frame))) + "\n")
                history.flush()
                os.fsync(history.fileno())
                probe.set_field(emu, "resume", 1)
            emu.step(1)
            row = capture(history, "combat")
            budget.evidence(emu.frame)
            ledger.accept(row)
        require(terminal is not None and row["control"]["role"] == 2 and
                not row["probe"]["status"] and row["probe"]["phase"] == 3 and
                len(ledger.events) == 2 and row["control"]["resumes"] == len(budget.resumed) and
                row["control"]["pause_id"] == len(budget.holds), "Incomplete six natural terminal/learning proof")
        require(terminal["party"] == row["party"], "Commit altered native/precommit party carry")
        committed = row["state"]
        expected_completed = bytearray(raw[38:102])
        expected_completed[58 // 8] |= 1 << (58 % 8)
        expected_retired = bytearray(raw[102:166])
        expected_retired[59 // 8] |= 1 << (59 % 8)
        require(committed[38:102] == expected_completed and committed[102:166] == expected_retired,
                "Unexpected completed/retired graph changes")
        after = probe.save_ram(emu)[1]
        base = probe.find_pak(before)
        pages = probe.story_owned_changes(before, after, base)
        (directory / "six_natural-terminal-save.bin").write_bytes(after)
        (directory / "six_natural-terminal-state.bin").write_bytes(committed)
        (directory / "six_natural-launcher-result.bin").write_bytes(bytes.fromhex(terminal["launch_raw"]))
        (directory / "six_natural-precommit-state.bin").write_bytes(bytes.fromhex(terminal["precommit"]))
        note = b"".join(after[base + page * 256:base + (page + 1) * 256] for page in pages)
        note_path = directory / "six_natural-terminal-note.bin"
        note_path.write_bytes(note)
        codec = probe.ROOT / "build/story/test_story_ember"
        checked = subprocess.run([str(codec), "--validate-reload", str(note_path),
                                  str(directory / "six_natural-terminal-state.bin")],
                                 capture_output=True, text=True)
        (directory / "six_natural-codec.log").write_text(checked.stdout + checked.stderr)
        require(checked.returncode == 0, "Terminal SC01 codec/full-state mismatch")
        evidence = dict(budget=budget.evidence(emu.frame), events=ledger.events,
                        observed_slots=sorted(ledger.observed_slots),
                        coverage_scope="only observed CPU slots; not all-slot switching/Transform/defeat/reload")
        (directory / "six_natural-ledger.json").write_text(json.dumps(evidence, indent=2) + "\n")
        return row["probe"]
