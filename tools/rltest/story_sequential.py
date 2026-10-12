"""Read-only sequential campaign observer. Importing this module starts no core.

The supplied 32-bit layout must be bound to the reviewed native ELF before a
caller uses capture(). HOST layout/source fixtures alone are not that binding.
The old six-party observer and its frozen evidence are intentionally unchanged.
"""

import copy
import hashlib
import struct
import time

from tools.rltest.frontend import HarnessError
from tools.rltest.story_six_natural import xp

ENTRY, ACTIVE, CLOSING, READY, BATTLE, REOPEN, FAILED = range(7)
PROGRESSION = ("species", "level", "exp", "stat_exp", "moves", "max_pp",
               "base_stats", "current_stats", "max_hp", "growth", "dvs")


def require(condition, message):
    if not condition:
        raise HarnessError(message)


class SequentialBudget:
    """One shared frame/deadline limit, with no excluded learning frames."""

    def __init__(self, frame, clock=time.monotonic):
        self.start = self.previous = frame
        self.clock = clock
        self.deadline = clock() + 180

    def check(self, frame):
        require(self.previous <= frame <= self.start + 18000,
                "Sequential shared frame budget/reload frame continuity refused")
        require(self.clock() < self.deadline, "Sequential 180-second watchdog expired")
        self.previous = frame
        return frame - self.start


def field(raw, layout, key, size=1):
    offset = layout[key]
    require(isinstance(offset, int) and 0 <= offset <= len(raw) - size,
            f"Sequential {key} layout outside captured object")
    return int.from_bytes(raw[offset:offset + size], "big")


def identity(raw, layout):
    return dict(operation=field(raw, layout, "operation_id", 4),
                encounter=field(raw, layout, "encounter", 2),
                variant=field(raw, layout, "variant"),
                party_count=field(raw, layout, "party_count"),
                enemy_count=field(raw, layout, "enemy_count"))


def decode_ledger(raw, layout, event_size):
    value = identity(raw, layout)
    value.update(count=field(raw, layout, "count"), next=field(raw, layout, "next"),
                 bound=field(raw, layout, "bound"))
    require(event_size == 3 and 0 <= value["next"] <= value["count"] <= 6,
            "Sequential ledger count/event ABI refused")
    offset = layout["party_ids"]
    require(0 <= offset <= len(raw) - 6, "Sequential party identity outside ledger")
    value["party_ids"] = list(raw[offset:offset + 6])
    offset = layout["events"]
    require(0 <= offset <= len(raw) - 6 * event_size, "Sequential events outside ledger")
    value["events"] = [dict(zip(("enemy", "participants", "recipients"),
                               raw[offset + i * 3:offset + i * 3 + 3]))
                       for i in range(value["count"])]
    seen = set()
    for event in value["events"]:
        require(event["enemy"] < value["enemy_count"] and event["enemy"] not in seen
                and 0 < event["participants"] < 1 << value["party_count"]
                and not event["recipients"] & ~event["participants"],
                "Sequential duplicate/out-of-range KO or recipient mask")
        seen.add(event["enemy"])
    return value


def read_object(emu, address, size):
    require(isinstance(address, int) and 0x80000000 <= address < 0x80800000
            and 0 < size <= 0x80800000 - address,
            "Sequential captured span outside 8 MiB RDRAM")
    raw = emu.read(address, size)
    require(len(raw) == size, "Short sequential capture")
    return raw


def capture(emu, abi, decode_mon):
    """Only read UI storage in RUN_ACTIVE; battle reads resident values only."""
    require(abi["pointer_size"] == 4 and abi["mon_size"] == 68,
            "Sequential observer needs reviewed 32-bit mon ABI")
    control_address = emu.read_u32("gScCampaignObserverControl")
    control = read_object(emu, control_address, abi["control"]["size"])
    run = emu.read("gScCampaignRun", abi["run"]["size"])
    stage = field(run, abi["run"], "stage", 4)
    require(stage in range(7) and not field(run, abi["run"], "error", 4)
            and stage != FAILED, "Sequential runner failed")
    state = control[abi["control"]["sequential_state"]:
                    abi["control"]["sequential_state"] + abi["state"]["size"]]
    request = control[abi["control"]["sequential_request"]:
                      abi["control"]["sequential_request"] + abi["request"]["size"]]
    ledger = control[abi["control"]["sequential_ledger"]:
                     abi["control"]["sequential_ledger"] + abi["ledger"]["size"]]
    row = dict(frame=emu.frame, stage=stage, control_raw=control.hex(),
               run_raw=run.hex(), request=identity(request, abi["request"]),
               ledger=decode_ledger(ledger, abi["ledger"], abi["event_size"]),
               result_ready=field(control, abi["control"], "sequential_result_ready", 4),
               outcome=field(control, abi["control"], "sequential_outcome", 4),
               ui=None)
    if stage == REOPEN:
        result = control[abi["control"]["sequential_result"]:
                         abi["control"]["sequential_result"] + abi["result"]["size"]]
        count = field(result, abi["result"], "player_count")
        require(1 <= count <= 6, "Sequential result player count refused")
        offset = abi["result"]["player"]
        mapping = abi["result"]["player_index"]
        require(offset + count * 68 <= len(result) and mapping + count <= len(result),
                "Sequential result party outside object")
        row["carry"] = [(result[mapping + i],
                         decode_mon(result[offset + i * 68:offset + (i + 1) * 68]))
                        for i in range(count)]
        require(field(result, abi["result"], "outcome") == row["outcome"],
                "Sequential result/outcome mismatch")
    if stage == ACTIVE:
        ui_address = field(control, abi["control"], "ui_pointer", 4)
        require(ui_address != 0, "Sequential active shell has no UI")
        state = read_object(emu, ui_address + abi["ui"]["state"], abi["state"]["size"])
        ui_ledger = read_object(emu, ui_address + abi["ui"]["postbattle_ledger"],
                                abi["ledger"]["size"])
        row["ui"] = dict(screen=int.from_bytes(read_object(
            emu, ui_address + abi["ui"]["screen"], 4), "big"),
            pending_save=int.from_bytes(read_object(
                emu, ui_address + abi["ui"]["pending_save"], 4), "big"),
            ledger=decode_ledger(ui_ledger, abi["ledger"], abi["event_size"]))
    count = field(state, abi["state"], "party_count")
    require(0 <= count <= 6, "Sequential party count refused")
    offset = abi["state"]["party"]
    require(0 <= offset <= len(state) - count * abi["mon_size"],
            "Sequential party outside state")
    row["party"] = [decode_mon(state[offset + i * 68:offset + (i + 1) * 68])
                    for i in range(count)]
    row["state_raw"] = state.hex()
    row["phase"] = field(state, abi["state"], "phase")
    return row


class SequentialLedger:
    """Validates captured facts and cumulative postbattle XP/stat experience.

    It does not claim native fences, canonical battle-menu input mapping, save
    codec acceptance or visual Transform fidelity; those are separate checks.
    """

    def __init__(self, initial_party, canonical_enemy, kind="trainer"):
        require(1 <= len(initial_party) <= 6 and 1 <= len(canonical_enemy) <= 6,
                "Sequential canonical team bounds")
        self.before = copy.deepcopy(initial_party)
        self.enemy = canonical_enemy
        self.kind = kind
        self.binding = None
        self.events = []
        self.applied = 0
        self.result = None
        self.carry = {}

    def expected(self, count):
        expected = copy.deepcopy(self.before)
        for event in self.events[:count]:
            foe = self.enemy[event["enemy"]]
            n = event["participants"].bit_count()
            award = (foe["yield"] // n) * foe["level"] // 7
            if self.kind in ("trainer", "rival"):
                award += award // 2
            for slot, mon in enumerate(expected):
                if not event["recipients"] & (1 << slot):
                    continue
                mon["exp"] = min(xp(100, mon["growth"]), mon["exp"] + award)
                while mon["level"] < 100 and xp(mon["level"] + 1, mon["growth"]) <= mon["exp"]:
                    mon["level"] += 1
                mon["stat_exp"] = [min(65535, value + foe["base"][i] // n)
                                   for i, value in enumerate(mon["stat_exp"])]
        return expected

    def accept(self, row):
        require(row["stage"] != FAILED, "Sequential failed runner row")
        ledger = row["ledger"]
        if ledger["bound"]:
            binding = {key: ledger[key] for key in
                       ("operation", "encounter", "variant", "party_count", "enemy_count")}
            require(binding == row["request"] and binding["operation"] > 0
                    and binding["party_count"] == len(self.before)
                    and binding["enemy_count"] == len(self.enemy)
                    and ledger["party_ids"] == [m["species"] for m in self.before]
                    + [0] * (6 - len(self.before)), "Sequential battle/party identity mismatch")
            if self.binding is None:
                self.binding = binding
            require(binding == self.binding, "Sequential coherent operation retarget")
            require(ledger["events"][:len(self.events)] == self.events
                    and len(ledger["events"]) >= len(self.events),
                    "Sequential event order/replay changed")
            self.events = copy.deepcopy(ledger["events"])
        if row["stage"] in (CLOSING, READY, BATTLE, REOPEN) and self.binding:
            require(all(all(a[key] == b[key] for key in PROGRESSION)
                        for a, b in zip(row["party"], self.before))
                    and len(row["party"]) == len(self.before),
                    "Sequential progression mutated while shell absent/battle active")
            require(ledger["next"] == 0, "Sequential ledger replayed during battle")
        if row["stage"] == REOPEN:
            require(row["result_ready"] == 1 and row["outcome"] in (0, 1, 2),
                    "Sequential reopen lacks completed result")
            require(self.result is None or self.result == row["outcome"],
                    "Sequential completed result changed")
            self.result = row["outcome"]
            captured = {}
            for slot, mon in row["carry"]:
                require(slot < len(self.before) and slot not in captured
                        and mon["species"] == self.before[slot]["species"],
                        "Sequential result carry mapping refused")
                captured[slot] = mon
            require(not self.carry or self.carry == captured, "Sequential carry result changed")
            self.carry = captured
        if row["stage"] == ACTIVE and self.result is not None:
            require(row["ui"] is not None, "Sequential replay has no shell")
            if self.result != 1:
                require(row["phase"] == 4 and row["ui"]["ledger"]["bound"] == 0
                        and row["party"] == self.before,
                        "Sequential loss applied progression or retained earned ledger")
                return
            require(len(self.events) == len(self.enemy), "Sequential win omitted a KO")
            replay = row["ui"]["ledger"]
            count = replay["next"] if replay["bound"] else len(self.events)
            require(self.applied <= count <= len(self.events), "Sequential replay cursor reversed")
            if replay["bound"]:
                require(replay["events"] == self.events and
                        {k: replay[k] for k in self.binding} == self.binding,
                        "Sequential replay changed captured identity/events")
            expected = self.expected(count)
            require(len(row["party"]) == len(expected) and
                    all(a["exp"] == b["exp"] and a["stat_exp"] == b["stat_exp"]
                        and a["level"] == b["level"] and a["growth"] == b["growth"]
                        for a, b in zip(row["party"], expected)),
                    "Sequential KO-time XP/stat experience mismatch")
            self.applied = count
            for slot, carry in self.carry.items():
                mon = row["party"][slot]
                require(mon["status"] == carry["status"] and
                        (carry["hp"] != 0 or (mon["hp"] == 0 and mon["fainted"] == 1)),
                        "Sequential replay revived or cleared carry status")


def input_digest(plan):
    """Caller hashes exact bytes, not a reserialized semantic approximation."""
    return hashlib.sha256(plan).hexdigest()
