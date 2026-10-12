"""Sequential scenario executor. Importing/--help starts no core.

Only the supervisor may launch this worker under a separate execution grant.
Fresh scenario processes inherit its counted-step streams, never its budget.
"""

import argparse
import ctypes
import hashlib
import json
import os
import pathlib
import subprocess
import sys
import tempfile

from tools.rltest.frontend import Frontend, HarnessError, Variable, ANGRYLION_THREADS_OPTION, _png
from tools.rltest.save_roundtrip import find_pak, save_ram
from tools.rltest.story_probe import decode_mon, story_owned_changes
from tools.rltest.story_sequential import ACTIVE, BATTLE, SequentialLedger, capture, field, require
from tools.rltest.story_sequential_buttons import (
    MASKS, accepted, action_menu, choose_move, choose_party, press_verified, read_menu,
)
from tools.rltest.story_sequential_pak import convert
from tools.rltest.story_sequential_supervisor import reserve_frames

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCENARIOS = ("win", "carry-reload", "loss-terminal-reload", "transform")
EXTRA_MASKS = dict(UP=0x800, DOWN=0x400, R=0x10)
STORY_STATE = 0x32


class OptionValue(ctypes.Structure):
    _fields_ = [("value", ctypes.c_char_p), ("label", ctypes.c_char_p)]


class OptionV1(ctypes.Structure):
    _fields_ = [("key", ctypes.c_char_p), ("desc", ctypes.c_char_p),
                ("info", ctypes.c_char_p), ("values", OptionValue * 128),
                ("default", ctypes.c_char_p)]


class OptionV2(ctypes.Structure):
    _fields_ = [("key", ctypes.c_char_p), ("desc", ctypes.c_char_p),
                ("desc_categorized", ctypes.c_char_p), ("info", ctypes.c_char_p),
                ("info_categorized", ctypes.c_char_p), ("category_key", ctypes.c_char_p),
                ("values", OptionValue * 128), ("default", ctypes.c_char_p)]


class OptionsV2(ctypes.Structure):
    _fields_ = [("categories", ctypes.c_void_p), ("definitions", ctypes.POINTER(OptionV2))]


class IntlOptions(ctypes.Structure):
    _fields_ = [("us", ctypes.c_void_p), ("local", ctypes.c_void_p)]


def atomic_bytes(path, data):
    with tempfile.NamedTemporaryFile(dir=path.parent, prefix=path.name + ".", delete=False) as stream:
        temporary = pathlib.Path(stream.name)
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


def boot_thread(emu):
    data = emu.read("pThreads", 0x130)
    word = lambda offset: int.from_bytes(data[offset:offset + 4], "big")
    row = dict(state=int.from_bytes(data[0x10:0x12], "big"), id=word(0x14),
               queue=word(8), pc=word(0x11C), cause=word(0x120),
               badvaddr=word(0x124), sp=word(0xF4), ra=word(0x104))
    if 0x80000000 <= row["sp"] <= 0x807FFFC0:
        row["stack_words"] = [int.from_bytes(value, "big") for value in
                              [emu.read(row["sp"] + i * 4, 4) for i in range(16)]]
    return row


class Progress:
    def __init__(self, directory):
        self.directory = directory
        self.boundary = "core-construction"
        self.witness = None
        self.transitions = []
        self.snapshots = 0
        self.last_snapshot = -500
        self.last_boundary = None
        self.snapshot_boundaries = set()
        self.completed = 0
        self.details = {}
        self.boot_witness = {}
        self.persist()

    def persist(self, emu=None):
        state = None
        error = None
        if emu is not None and emu.frame:
            try:
                state = emu.read_u32("gCurrentGameState")
                if isinstance(emu, Frontend):
                    for key, symbol in (("game_state", "gCurrentGameState"),
                                        ("os_mem_size", "osMemSize"),
                                        ("runner_stage", "gScCampaignRun"),
                                        ("menu_ready", "gScCampaignMenuReady")):
                        try:
                            self.boot_witness[key] = emu.read_u32(symbol)
                        except HarnessError as exc:
                            self.boot_witness[key] = {"error": str(exc)}
                    fragments = emu.read("gFragments", 0xEA * 8)
                    self.boot_witness["mapped_fragments"] = [
                        i for i in range(0xEA) if any(fragments[i * 8:(i + 1) * 8])]
                    self.boot_witness["fragment_maps"] = {
                        str(i): dict(base=int.from_bytes(fragments[i * 8:i * 8 + 4], "big"),
                                     size=int.from_bytes(fragments[i * 8 + 4:(i + 1) * 8], "big"))
                        for i in self.boot_witness["mapped_fragments"]}
                    try:
                        self.boot_witness["game_thread"] = boot_thread(emu)
                    except HarnessError as exc:
                        self.boot_witness["game_thread"] = {"error": str(exc)}
                    for symbol, size in (("sMemPool", 0x34), ("gScPoolFailure", 40),
                                         ("gScPoolZero", 40)):
                        try:
                            self.boot_witness[symbol] = emu.read(symbol, size).hex()
                        except HarnessError as exc:
                            self.boot_witness[symbol] = {"error": str(exc)}
            except HarnessError as exc:
                error = str(exc)
        changed = self.boundary not in self.snapshot_boundaries
        if emu is not None and (changed or emu.frame - self.last_snapshot >= 500):
            framebuffer = getattr(emu, "framebuffer", None)
            if framebuffer is not None:
                data, width, height, pitch = framebuffer
                require(len(data) <= 4 * 1024 * 1024, "Diagnostic framebuffer exceeds bound")
                slot = self.snapshots if self.snapshots < 2 else 2 + (self.snapshots - 2) % 6
                atomic_bytes(self.directory / f"video-{slot}.png",
                             _png(data, width, height, pitch, emu.pixel_format))
                self.snapshots += 1
                self.snapshot_boundaries.add(self.boundary)
                self.last_snapshot = emu.frame
                self.details["last_video"] = dict(slot=slot, frame=emu.frame,
                                                 boundary=self.boundary)
                self.details.setdefault("videos", {})[str(slot)] = self.details["last_video"]
        self.last_boundary = self.boundary
        row = dict(boundary=self.boundary, completed_frames=self.completed,
                   game_state=state, read_error=error, witness=self.witness,
                   transitions=self.transitions[-32:], details=self.details,
                   boot_witness=self.boot_witness,
                   snapshots_written=self.snapshots, snapshot_slots=8)
        atomic_bytes(self.directory / "progress.json",
                     (json.dumps(row, sort_keys=True) + "\n").encode())

    def mark(self, boundary, emu=None, **details):
        self.boundary = boundary
        self.details.update(details)
        self.persist(emu)


class LeasedFrontend(Frontend):
    """Frontend construction itself runs frame one; lease it before dispatch."""

    def __init__(self, *args, frame_lease, prepare_save, progress=None, boot_symbols=None,
                 require_expansion=False, **kwargs):
        self.frame_lease = frame_lease
        self.prepare_save = prepare_save
        self.progress = progress
        self.boot_symbols = boot_symbols or {}
        self.require_expansion = require_expansion
        self.registered_options = {}
        self.expansion_choice = None
        self.requested_options = set((kwargs.get("options") or {}).keys())
        self._sc_budget_bound = True
        super().__init__(*args, **kwargs)

    def register_option(self, key, values, default, labels, description):
        require(values and default in values, f"Invalid registered core option: {key}")
        self.registered_options[key] = dict(values=values, default=default,
                                            labels=labels, description=description)
        if key == ANGRYLION_THREADS_OPTION:
            self._angrylion_threads_supported = True
        if "disable" in (key + " " + description).lower() and (
                "extra" in key.lower() or "expansion pak" in description.lower()):
            enabled = [value for value, label in zip(values, labels)
                       if value.lower() in ("false", "disabled", "off") or
                       label.lower() in ("false", "disabled", "off")]
            require(len(enabled) == 1, "Ambiguous registered extra-memory enable value")
            self.options[key] = enabled[0]
            self.expansion_choice = dict(key=key, value=enabled[0])
        self.options.setdefault(key, default)
        require(self.options[key] in values, f"Unsupported requested core value: {key}")

    def _environment(self, command, data):
        if command not in (16, 53, 54, 67, 68):
            return super()._environment(command, data)
        try:
            if command == 16:
                definitions = ctypes.cast(data, ctypes.POINTER(Variable))
                for i in range(1024):
                    entry = definitions[i]
                    if not entry.key:
                        break
                    description, choices = entry.value.decode().split(";", 1)
                    values = choices.strip().split("|")
                    self.register_option(entry.key.decode(), values, values[0], values, description)
                else:
                    raise HarnessError("Too many legacy option definitions")
                return super()._environment(command, data)
            pointer = data
            if command in (54, 68):
                pointer = ctypes.cast(data, ctypes.POINTER(IntlOptions)).contents.us
            if command in (67, 68):
                definitions = ctypes.cast(pointer, ctypes.POINTER(OptionsV2)).contents.definitions
            else:
                definitions = ctypes.cast(pointer, ctypes.POINTER(OptionV1))
            for i in range(1024):
                entry = definitions[i]
                if not entry.key:
                    break
                values, labels = [], []
                for item in entry.values:
                    if not item.value:
                        break
                    values.append(item.value.decode())
                    labels.append((item.label or item.value).decode())
                self.register_option(entry.key.decode(), values,
                                     (entry.default or entry.values[0].value).decode(),
                                     labels, (entry.desc or b"").decode())
            else:
                raise HarnessError("Too many core option definitions")
            return True
        except (HarnessError, ValueError) as exc:
            self.callback_error = exc
            return False

    def verify_options(self):
        requested = self.requested_options | {key for key in self.options
                                             if key.startswith("mupen64plus-")}
        require(requested <= self.registered_options.keys(),
                f"Unregistered requested core keys: {requested - self.registered_options.keys()}")
        if self.progress:
            self.progress.mark("core-options-negotiated", negotiated_options={
                key: self.options[key] for key in self.registered_options},
                expansion_choice=self.expansion_choice,
                expansion_admission="actual bound osMemSize must equal 8 MiB")

    def resolve(self, address):
        if isinstance(address, str) and address in self.boot_symbols:
            return self.boot_symbols[address]
        return super().resolve(address)

    def step(self, frames=1):
        if self.frame == 0:
            self.verify_options()
            if self.progress:
                self.progress.mark("preboot-save-setup")
            self.prepare_save(self)
        for _ in range(frames):
            if self.progress:
                self.progress.persist(self)
            self.frame_lease(1)
            super().step(1)
            if self.progress:
                self.progress.completed = self.frame
                self.progress.persist(self)
            if self.require_expansion and self.frame >= 120:
                require(self.read_u32("osMemSize") == 0x800000,
                        "Boot did not establish required 8 MiB Expansion Pak")


def digest(data):
    return hashlib.sha256(data).hexdigest()


def install_setup(emu, container, *, reload=False):
    require(not reload and emu.frame == 0, "Save-RAM write refused outside pre-boot setup")
    pointer, initial = save_ram(emu)
    require(len(initial) == len(container), "Fresh core save container size changed")
    ctypes.memmove(pointer, container, len(container))


def load_persisted_save(emu, container, game_written_sha256):
    require(emu.frame == 0, "Reload persistence must precede first frame")
    require(digest(container) == game_written_sha256,
            "Reload substituted bytes differ from game-written save")
    pointer, initial = save_ram(emu)
    require(len(initial) == len(container), "Persisted save container size changed")
    ctypes.memmove(pointer, container, len(container))
    require(save_ram(emu)[1] == container, "Core did not retain exact persisted save")


def write_json(path, value):
    with path.open("x") as stream:
        json.dump(value, stream, indent=2, sort_keys=True)
        stream.write("\n")


def verify_candidate(seal_path, package, binding_path, expected_seal_sha):
    raw_seal = seal_path.read_bytes()
    require(len(expected_seal_sha) == 64 and digest(raw_seal) == expected_seal_sha,
            "Native seal differs from approved external pin")
    seal = json.loads(raw_seal)
    require(seal["scope"] ==
            "build-fixed-point-and-linked-layout-only; not execution or gameplay acceptance",
            "Wrong native seal scope")
    require(seal["fixed_point"]["all_three_headers_byte_identical"],
            "Candidate has no authority fixed point")
    for group in ("source_sha256", "artifacts_sha256"):
        for name, expected in seal[group].items():
            path = (ROOT / name).resolve()
            require(path.is_relative_to(ROOT) and digest(path.read_bytes()) == expected,
                    f"Sealed input changed: {name}")
    for path in (binding_path, package / "manifest.json"):
        relative = str(path.resolve().relative_to(ROOT))
        require(seal["artifacts_sha256"].get(relative) == digest(path.read_bytes()),
                "Binding/package is not part of approved seal")
    raw = (package / "manifest.json").read_bytes()
    manifest = json.loads(raw)
    require(digest(raw) == (package / "manifest.sha256").read_text().strip(),
            "Package manifest changed")
    binding = json.loads(binding_path.read_text())
    require(binding["package_sha256"] == digest(raw), "Binding/package mismatch")
    for name, expected in manifest["files"].items():
        require(digest((package / name).read_bytes()) == expected,
                f"Package checkpoint changed: {name}")
    require(binding["abi"]["control"]["size"] == 4104 and
            "list" in binding["abi"] and "labels" in binding["abi"]["ui"] and
            "native_lifecycle" in binding,
            "Worker requires final bound menu witnesses")
    require(binding.get("boot_symbols", {}).get("osMemSize") == 0x80000318,
            "Worker requires ELF-bound boot symbols")
    config = seal["configuration"]
    for name, key in (("pokestadium-rl.elf", "elf_sha256"),
                      ("pokestadium-rl.map", "map_sha256"),
                      ("pokestadium-rl.z64", "rom_sha256")):
        require(digest((ROOT / config["BUILD_DIR"] / name).read_bytes()) == binding[key],
                f"Bound candidate changed: {name}")
    return seal, manifest, binding


def story_note(container):
    base = find_pak(container)
    pages = story_owned_changes(container, container, base)
    return b"".join(container[base + page * 256:base + (page + 1) * 256]
                    for page in pages)


def isolated_container(initial, note, expected):
    base = find_pak(initial)
    pak = convert(initial[base:base + 32768], note, expected)
    return initial[:base] + pak + initial[base + 32768:]


def menu_view(emu, abi):
    require(emu.read_u32("gScCampaignRun") == ACTIVE,
            "UI read outside active shell")
    control = emu.read_u32("gScCampaignObserverControl")
    ui = emu.read_u32(control + abi["control"]["ui_pointer"])
    require(0x80000000 <= ui <= 0x80800000 - abi["ui"]["size"],
            "UI pointer outside bound RDRAM")
    raw = emu.read(ui, abi["ui"]["size"])
    count = field(raw, {"count": abi["ui"]["list"] + abi["list"]["count"]}, "count", 4)
    cursor = field(raw, {"cursor": abi["ui"]["list"] + abi["list"]["cursor"]}, "cursor", 4)
    require(0 < count <= 416 and cursor < count, "UI list bounds refused")
    ids = [int.from_bytes(raw[abi["ui"]["ids"] + i * 2:
                              abi["ui"]["ids"] + i * 2 + 2], "big") for i in range(count)]
    labels = []
    for i in range(count):
        label = raw[abi["ui"]["labels"] + i * 72:abi["ui"]["labels"] + (i + 1) * 72]
        require(b"\0" in label, "UI label missing terminator")
        labels.append(label.split(b"\0", 1)[0].decode("ascii"))
    return dict(screen=field(raw, abi["ui"], "screen", 4), cursor=cursor,
                ids=ids, labels=labels, operation=field(raw, abi["ui"], "operation", 4),
                pending_save=field(raw, abi["ui"], "pending_save", 4))


def retirement_evidence(emu, raw, layout):
    values = {key: field(raw, layout["lifecycle"], key, 4) for key in (
        "memory_live", "memory_storage", "lifetime_live", "display_acquired",
        "display_scope", "retirement_acquired", "retirement_pending",
        "backing_retired", "snapshot_valid", "fallback_completed")}
    require(values == dict(memory_live=0, memory_storage=0, lifetime_live=0,
                           display_acquired=0, display_scope=0, retirement_acquired=0,
                           retirement_pending=0, backing_retired=1, snapshot_valid=1,
                           fallback_completed=1), "Battle began before joined UI retirement")
    start = layout["lifecycle"]["snapshot"]
    snapshot = raw[start:start + layout["snapshot"]["size"]]
    scalar = {key: field(snapshot, layout["snapshot"], key, 4) for key in (
        "fallback", "vi_current", "vi_next", "crash_frame", "waiting", "requests",
        "completions", "events", "rsp_state", "rdp_state", "task_completions")}
    require(scalar["waiting"] == 1 and
            all(scalar[key] == 0 for key in ("requests", "completions", "events", "task_completions"))
            and scalar["rsp_state"] in (0, 3) and scalar["rdp_state"] in (0, 2),
            "Retained atomic DP snapshot is not quiescent")
    fallback = scalar["fallback"]
    require(0x80000000 <= fallback <= 0x80400000 - 16,
            "Retirement fallback descriptor outside resident memory")
    image = emu.read(fallback, 16)
    require(image[:8] == b"\0\0\0\2\2\x80\0\1" and image[12:] == bytes(4),
            "Retirement fallback descriptor differs")
    pixels = int.from_bytes(image[8:12], "big")
    require(0x80000000 <= pixels <= 0x80400000 - 1280 and pixels % 64 == 0 and
            all((scalar[key] & 0x1FFFFFFF) == (pixels & 0x1FFFFFFF)
                for key in ("vi_current", "vi_next")), "VI did not retire onto fallback")
    spans = []
    for i in range(2):
        offset = layout["lifecycle"]["spans"] + i * layout["span"]["size"]
        span = raw[offset:offset + layout["span"]["size"]]
        pointer = field(span, layout["span"], "pixels", 4)
        size = field(span, layout["span"], "bytes", 4)
        require(size == 614400 and pointer % 64 == 0 and
                0x80400000 <= pointer <= 0x807DA7E0 - size,
                "Retired framebuffer span differs")
        spans.append((pointer, size))
    require(spans[0][0] + spans[0][1] <= spans[1][0] or
            spans[1][0] + spans[1][1] <= spans[0][0], "Retired framebuffer spans overlap")
    crash = scalar["crash_frame"] & 0x1FFFFFFF
    require(crash and not any((pointer & 0x1FFFFFFF) <= crash <
                             (pointer & 0x1FFFFFFF) + size for pointer, size in spans),
            "Crash framebuffer still aliases retired UI")
    for i in range(3):
        offset = layout["snapshot"]["frames"] + i * layout["frame"]["size"]
        frame = snapshot[offset:offset + layout["frame"]["size"]]
        require(all(field(frame, layout["frame"], key) <= 1 for key in ("rsp", "rdp"))
                and all(field(frame, layout["frame"], key, 4) == 0
                        for key in ("work", "task", "image")),
                "Retired snapshot still references display tasks")
    require(emu.read("gFragments", 0xEA * 8)[0xE9 * 8:0xEA * 8] == bytes(8),
            "Story shell still mapped during ordinary battle")
    return dict(flags=values, snapshot=scalar, spans=spans)


def candidate_bindings():
    # These are hypotheses only. Actual N64 pressed bits decide every mapping.
    return ([dict(kind="right-stick", xy=xy) for xy in
             ([0, 32767], [32767, 0], [0, -32768], [-32768, 0])] +
            [dict(kind="joypad", id=i) for i in (8, 9, 12, 13, 14, 15, 0, 1, 10, 11, 4, 5, 3)])


def apply_candidate(emu, binding, down):
    if binding["kind"] == "joypad":
        if down:
            emu.held[0].add(binding["id"])
        else:
            emu.held[0].clear()
    else:
        emu.stick(*(binding["xy"] if down else [0, 0]), right=True)


def calibrate(emu):
    result = {}
    masks = {**MASKS, **EXTRA_MASKS}
    for binding in candidate_bindings():
        if getattr(emu, "progress", None):
            emu.progress.mark("button-calibration", emu, calibration_candidate=binding,
                              calibrated=result)
        require(not any(emu.held) and not any(v for p in emu.analog for v in p),
                "Calibration began with held input")
        apply_candidate(emu, binding, True)
        observed = set()
        try:
            for _ in range(4):
                emu.step(1)
                pointer = emu.read_u32("gPlayer1Controller")
                require(0x80000000 <= pointer <= 0x80800000 - 0x24,
                        "Calibration controller not initialized")
                pressed = emu.read_u16(pointer + 8)
                if pressed:
                    observed.add(pressed)
        finally:
            apply_candidate(emu, binding, False)
        emu.step(2)
        for name, mask in masks.items():
            if observed == {mask} and name not in result:
                result[name] = binding
    required = {"N64_A", "N64_B", "C_UP", "C_RIGHT", "C_DOWN", "C_LEFT",
                "UP", "DOWN", "L", "R", "START"}
    require(required <= result.keys(), f"Button calibration incomplete: {required - result.keys()}")
    return result


class Scenario:
    def __init__(self, emu, abi, manifest, directory, scenario, calibration):
        self.emu, self.abi, self.manifest = emu, abi, manifest
        self.directory, self.scenario, self.calibration = directory, scenario, calibration
        self.ledger = None
        self.last = None
        self.boundary = "core-created-before-boot"
        self.transitions = []
        self.initial_state = None
        self.transform = None
        self.interrupted = None
        self.acknowledgements = []
        self.expected_native = None
        self.native_ack = None
        self.mapping = None
        self.retirement = None
        self.progress = getattr(emu, "progress", None)
        original = emu.step

        def observed_step(frames=1):
            for _ in range(frames):
                original(1)
                self.observe()
        emu.step = observed_step

    def observe(self):
        if self.emu.read_u32("gCurrentGameState") != STORY_STATE:
            return
        row = capture(self.emu, self.abi, decode_mon)
        if self.ledger:
            self.ledger.accept(row)
        if row["stage"] == BATTLE:
            proof = retirement_evidence(self.emu, bytes.fromhex(row["control_raw"]),
                                        self.abi["native_lifecycle"])
            require(self.retirement is None or self.retirement == proof,
                    "Retained retirement proof changed during battle")
            self.retirement = proof
        key = (row["stage"], row["ledger"]["count"],
               row["ui"]["screen"] if row["ui"] else None,
               row["ui"]["ledger"]["next"] if row["ui"] else None)
        if self.last != key:
            self.transitions.append(row)
            self.transitions = self.transitions[-32:]
            self.last = key
        self.boundary = f"stage-{row['stage']}-screen-{key[2]}"
        if self.progress:
            self.progress.witness = row
            self.progress.transitions = self.transitions
            self.progress.mark(self.boundary, self.emu)
        if row["stage"] == BATTLE and self.interrupted is None:
            self.interrupted = story_note(save_ram(self.emu)[1])
            (self.directory / "interrupted.note").write_bytes(self.interrupted)
        if row["stage"] == BATTLE and self.scenario == "transform":
            mons = self.emu.battle_mons()
            if mons and mons[0]["moves"] != [144, 0, 0, 0] and (
                    mons[0]["moves"] == mons[1]["moves"]):
                self.transform = dict(frame=self.emu.frame, player=mons[0], enemy=mons[1])
        if row["stage"] == BATTLE and self.expected_native:
            menu = read_menu(self.emu, self.abi["native_run_address"])
            kind, slot = self.expected_native
            if accepted(menu, kind, slot):
                self.native_ack = self.emu.frame

    def wait(self, predicate, description):
        # The supervisor alone controls the aggregate and scenario ceilings.
        self.boundary = "waiting-" + description
        if self.progress:
            self.progress.mark(self.boundary, self.emu)
        while not predicate():
            self.emu.step(1)
        self.boundary = description
        if self.progress:
            self.progress.mark(description, self.emu)

    def boot_to_title(self):
        self.boundary = "waiting-title-with-intro-Start"
        while self.emu.read_u32("gCurrentGameState") != 2:
            state = self.emu.read_u32("gCurrentGameState")
            if self.progress:
                self.progress.mark(self.boundary, self.emu, boot_state=state)
            # Fragment17 intro accepts Start; logo initialization itself does not.
            if state == 1 and self.emu.frame >= 120 and self.emu.frame % 120 == 0:
                self.emu.hold("START")
                try:
                    self.emu.step(2)
                    pointer = self.emu.read_u32("gPlayer1Controller")
                    require(0x80000000 <= pointer <= 0x80800000 - 0x24,
                            "Intro Start controller unavailable")
                    if self.progress:
                        down = self.emu.read_u16(pointer + 6)
                        self.progress.mark("intro-Start-observed", self.emu,
                                           intro_start_button_down=down,
                                           intro_start_verified=bool(down & 0x1000))
                finally:
                    self.emu.release("START")
            self.emu.step(1)

    def press(self, name):
        if self.progress:
            self.progress.mark("press-" + name, self.emu)
        press_verified(self.emu, name, self.calibration, self.emu.step)

    def ui(self):
        return menu_view(self.emu, self.abi)

    def select(self, screen, *, id_=None, label=None):
        self.wait(lambda: self.emu.read_u32("gScCampaignRun") == ACTIVE,
                  "active-shell")
        menu = self.ui()
        require(menu["screen"] == screen, f"Unexpected UI screen {menu['screen']}, wanted {screen}")
        choices = menu["ids"] if id_ is not None else menu["labels"]
        wanted = id_ if id_ is not None else label
        require(choices.count(wanted) == 1, f"Missing/ambiguous UI choice: {wanted}")
        target = choices.index(wanted)
        while self.ui()["cursor"] != target:
            before = self.ui()["cursor"]
            self.press("DOWN" if before < target else "UP")
            self.wait(lambda: self.ui()["cursor"] != before, "UI cursor acknowledged")
            require(self.ui()["screen"] == screen, "UI screen changed while selecting")
        self.press("N64_A")

    def enter_continue(self):
        while self.emu.read_u32("gCurrentGameState") == 2:
            self.press("START")
            self.emu.step(30)
        self.wait(lambda: self.emu.read_u32("gCurrentGameState") == 4 and
                  self.emu.read_u32("gScCampaignMenuReady") == 1, "Menu Select input-ready top page")
        self.press("R")
        self.wait(lambda: self.emu.read_u32("gCurrentGameState") == STORY_STATE and
                  self.emu.read_u32("gScCampaignRun") == ACTIVE, "campaign-shell")
        self.select(0, id_=0)
        self.wait(lambda: self.ui()["screen"] != 0, "Continue loaded")
        require(self.ui()["screen"] != 18, "Continue storage error")
        row = capture(self.emu, self.abi, decode_mon)
        return row

    def launch(self):
        initial = capture(self.emu, self.abi, decode_mon)
        self.initial_state = initial["state_raw"]
        target = self.manifest["target"]
        area = target["id"].split("/")[0]
        self.select(2, label=area)
        self.wait(lambda: self.ui()["screen"] == 3, "encounter-list")
        self.select(3, id_=self.abi["target"]["index"])
        self.wait(lambda: self.ui()["screen"] == 8, "start-confirmation")
        require(self.ui()["operation"] == 1, "Encounter is not a trainer launch")
        species = self.manifest["catalog_species"]
        enemies = target["teams"][0]
        enemy = [dict(level=mon["level"], base=species[str(mon["species"])]["base"],
                      yield_=species[str(mon["species"])]["yield"]) for mon in enemies]
        for mon in enemy:
            mon["yield"] = mon.pop("yield_")
        self.ledger = SequentialLedger(initial["party"], enemy)
        self.select(8, id_=1)

    def battle_action(self):
        menu = read_menu(self.emu, self.abi["native_run_address"])
        if menu["locked"] or menu["mode"] == 3:
            return
        desired = self.mapping.index(0) if 0 in self.mapping else None
        switching = (desired is not None and menu["hp"][desired] and
                     menu["active"] != desired)
        if menu["mode"] == 0 and menu["action"] == 0:
            self.press(action_menu("switch" if switching else "fight"))
            self.wait(lambda: read_menu(self.emu, self.abi["native_run_address"])["mode"] != 0,
                      "native action menu acknowledged")
            return
        if menu["mode"] == 2:
            slots = [i for i, hp in enumerate(menu["hp"]) if hp and i != menu["active"]]
            require(slots, "Party menu has no living replacement")
            slot, kind = (desired if desired in slots else slots[0]), "party"
            button = choose_party(menu, slot)
        elif menu["mode"] == 1 and menu["action"] == 1:
            if switching:
                self.press("L")
                self.wait(lambda: read_menu(self.emu, self.abi["native_run_address"])["mode"] == 0,
                          "native action menu reopened")
                return
            preferred = {"win": 52, "loss-terminal-reload": 45, "transform": 144}[self.scenario]
            legal = [i for i in range(4) if menu["moves"][i] and
                     menu["pp"][i] & 63 and menu["disabled"] != i]
            require(legal, "No legal move under reviewed direct-input policy")
            wanted = [i for i in legal if menu["moves"][i] == preferred]
            if self.scenario == "loss-terminal-reload":
                nondamaging = {28, 45, 39, 43, 81, 150}
                wanted = [i for i in legal if menu["moves"][i] in nondamaging]
            slot, kind = (wanted or legal)[0], "move"
            button = choose_move(menu, slot)
        else:
            return
        self.expected_native = (kind, slot)
        self.native_ack = None
        self.press(button)
        self.wait(lambda: self.native_ack is not None, f"native {kind} acknowledged")
        self.acknowledgements.append(dict(frame=self.native_ack, kind=kind, slot=slot,
                                         button=button))
        self.expected_native = None

    def finish_battle(self):
        self.wait(lambda: self.emu.read_u32("gScCampaignRun") == BATTLE, "battle-entry")
        self.wait(lambda: self.emu.battle_mons() is not None, "battle-mons-initialized")
        launch = self.emu.read("gRlLastBattleResult", self.abi["result"]["size"])
        count = read_menu(self.emu, self.abi["native_run_address"])["count"]
        offset = self.abi["result"]["player_index"]
        mapping = list(launch[offset:offset + count])
        require(len(set(mapping)) == count and all(i < len(self.ledger.before) for i in mapping),
                "Native-to-story party mapping refused")
        self.mapping = mapping
        write_json(self.directory / "native-party-mapping.json", mapping)
        while self.emu.read_u32("gScCampaignRun") == BATTLE:
            self.battle_action()
            self.emu.step(1)
        self.wait(lambda: self.emu.read_u32("gScCampaignRun") == ACTIVE, "postbattle-shell")
        require(self.ledger.result is not None, "Result boundary was not observed")
        if self.scenario == "win":
            require(self.ledger.result == 1, "Win scenario lost/drew")
        elif self.scenario == "loss-terminal-reload":
            require(self.ledger.result == 0, "Loss scenario won/drew")
            require(self.ui()["screen"] == 17, "Loss is not terminal")
            return
        while self.ui()["screen"] in (8, 9, 14, 15, 16):
            screen = self.ui()["screen"]
            if screen == 14:
                self.select(screen, id_=4)
            elif screen == 15:
                self.select(screen, id_=255)
            elif screen == 9:
                self.select(screen, id_=2)
            elif screen == 8:
                require(self.ui()["operation"] in (5, 6, 12),
                        "Unreviewed earned confirmation")
                self.select(screen, id_=1)
            self.emu.step(1)
        require(not self.ui()["pending_save"], "Unsettled postbattle save")
        if self.scenario == "transform":
            require(self.transform is not None, "Actual Transform not observed")


def execute_scenario(name, args, factory=LeasedFrontend, stream_in=None, stream_out=None):
    stream_in = sys.stdin if stream_in is None else stream_in
    stream_out = sys.stdout if stream_out is None else stream_out
    seal, manifest, binding = verify_candidate(args.seal, args.package, args.binding,
                                              args.seal_sha256)
    catalog = json.loads((ROOT / seal["configuration"]["BUILD_DIR"] / "story/data.json").read_text())
    manifest["catalog_species"] = {str(record["id"]): record for record in catalog["species"]}
    terminal_reload = getattr(args, "terminal_reload", False)
    directory = args.out / name
    if terminal_reload:
        directory /= "terminal-reload"
    directory.mkdir()
    progress = Progress(directory)
    abi = binding["abi"]
    abi["native_run_address"] = binding["binding"]["run"]
    abi["native_lifecycle"] = binding.get("native_lifecycle")
    config = seal["configuration"]
    require(digest(args.core.read_bytes()) == args.core_sha256,
            "Core differs from approved external pin")
    cold_reload = terminal_reload or name == "carry-reload"
    expected_container = ((args.out / ("loss-terminal-reload" if terminal_reload else "win")
                           / "save-ram.bin").read_bytes() if cold_reload else None)
    game_written_sha = (json.loads((args.out /
                        ("loss-terminal-reload" if terminal_reload else "win") /
                        "report.json").read_text())["game_written_save_sha256"]
                        if cold_reload else None)
    prepared = {}

    def prepare_save(emu):
        require(emu.frame == 0, "Save preparation must precede first frame")
        initial = save_ram(emu)[1]
        if cold_reload:
            load_persisted_save(emu, expected_container, game_written_sha)
            prepared["container"] = save_ram(emu)[1]
        else:
            note_name = "transform-route.note" if name == "transform" else "normal-route.note"
            prepared["container"] = isolated_container(
                initial, (args.package / note_name).read_bytes(), manifest["files"][note_name])
            install_setup(emu, prepared["container"])

    emu = factory(args.core, ROOT / config["ROM"],
                  ROOT / config["BUILD_DIR"] / "pokestadium-rl.map", directory,
                  options={"mupen64plus-pak1": "memory"},
                  timeout=180, prepare_save=prepare_save, progress=progress,
                  boot_symbols=binding.get("boot_symbols"),
                  require_expansion=True,
                  frame_lease=lambda n: reserve_frames(
                      stream_in, stream_out, name, n))
    require(getattr(emu, "_sc_budget_bound", False) and emu.frame == 1,
            "Frontend construction must lease initial boot frame")
    evidence = dict(scenario=name, status="fail", last_verified_boundary="core-created",
                    scope="scenario observables; not full heap/fence/ACE acceptance")
    scenario = None
    try:
        restored = prepared["container"]
        if cold_reload:
            evidence["game_written_save_before_reload_sha256"] = game_written_sha
            evidence["loaded_save_sha256"] = digest(restored)
            require(save_ram(emu)[1] == expected_container, "Boot changed game-written save")
        calibration_path = args.out / "win" / "calibration.json"
        calibration = {} if name == "win" else json.loads(calibration_path.read_text())
        scenario = Scenario(emu, abi, manifest, directory, name, calibration)
        scenario.boot_to_title()
        require(emu.read_u32("osMemSize") == 0x800000, "Core is not using 8 MiB")
        if name == "win":
            scenario.calibration = calibrate(emu)
            write_json(calibration_path, scenario.calibration)
        if emu.read_u32("gCurrentGameState") == 2:
            scenario.press("START")
        loaded = scenario.enter_continue()
        require(story_note(save_ram(emu)[1]) == story_note(restored),
                "Continue changed isolated journal")
        campaign_container = save_ram(emu)[1]
        if terminal_reload:
            expected = json.loads((args.out / name / "initial-state.json").read_text())
            raw = bytearray.fromhex(expected["state_raw"])
            raw[abi["state"]["phase"]] = 4
            require(loaded["phase"] == 4 and loaded["state_raw"] == raw.hex() and
                    loaded["party"] == expected["party"],
                    "Cold terminal reload does not match interrupted prebattle checkpoint")
            require(story_note(restored) == (args.out / name / "interrupted.note").read_bytes(),
                    "Loss wrote a separate checkpoint instead of retaining interruption")
            require(scenario.ui()["screen"] == 17, "Cold reload offers battle retry")
        elif name == "carry-reload":
            expected = json.loads((args.out / "win" / "committed-state.json").read_text())
            require(loaded["state_raw"] == expected["state_raw"] and
                    loaded["party"] == expected["party"], "Cold carry reload mismatch")
            evidence["actual_carry_sha256"] = digest(story_note(restored))
        else:
            write_json(directory / "initial-state.json",
                       {key: loaded[key] for key in ("state_raw", "party", "phase")})
            scenario.launch()
            scenario.finish_battle()
            if name == "loss-terminal-reload":
                evidence["terminal_reload_pending"] = True
            committed = capture(emu, abi, decode_mon)
            write_json(directory / "committed-state.json",
                       {key: committed[key] for key in ("state_raw", "party", "phase")})
            if name == "win":
                require(committed["phase"] == 0, "Win did not return to route")
                scenario.select(2, id_=3)
                scenario.wait(lambda: scenario.ui()["screen"] == 0 and
                              not scenario.ui()["pending_save"], "win-save-committed")
                evidence["actual_carry_sha256"] = digest(story_note(save_ram(emu)[1]))
            evidence["outcome"] = scenario.ledger.result
            evidence["events"] = scenario.ledger.events
            evidence["transform"] = scenario.transform
            evidence["retirement"] = scenario.retirement
        final_container = save_ram(emu)[1]
        if cold_reload:
            evidence["game_written_save_after_reload_sha256"] = digest(final_container)
            require(final_container == expected_container, "Cold reload changed game-written save")
        story_owned_changes(campaign_container, final_container, find_pak(campaign_container))
        (directory / "save-ram.bin").write_bytes(final_container)
        evidence["game_written_save_sha256"] = digest(final_container)
        evidence["core_sha256"] = digest(args.core.read_bytes())
        evidence["options"] = emu.options
        evidence["isolated_journal_sha256"] = digest(story_note(final_container))
        evidence["status"] = ("awaiting-terminal-reload" if
                              name == "loss-terminal-reload" and not terminal_reload else "pass")
    except HarnessError as error:
        evidence["error"] = str(error)
        raise
    finally:
        if scenario:
            evidence["last_verified_boundary"] = scenario.boundary
            evidence["local_frames"] = emu.frame
            evidence["acknowledgements"] = scenario.acknowledgements
            write_json(directory / "transitions.json", scenario.transitions)
        write_json(directory / "report.json", evidence)
        emu.close()


def control_inputs(seal, path, core):
    require(path is not None, "Control manifest required")
    relative = str(path.resolve().relative_to(ROOT))
    require(seal["artifacts_sha256"].get(relative) == digest(path.read_bytes()),
            "Control manifest is not sealed")
    control = json.loads(path.read_text())
    require(control["core_sha256"] == digest(core.read_bytes()), "Control core pin differs")
    for name, expected in control["files"].items():
        target = (path.parent / name).resolve()
        require(target.is_relative_to(ROOT) and digest(target.read_bytes()) == expected,
                "Control artifact changed")
    return control


def execute_control(args, stream_out, factory=LeasedFrontend):
    seal, _, _ = verify_candidate(args.seal, args.package, args.binding, args.seal_sha256)
    control = control_inputs(seal, args.control, args.core)
    directory = args.out / "control"
    directory.mkdir()
    progress = Progress(directory)
    emu = factory(args.core, args.control.parent / control["rom"],
                  args.control.parent / control["map"], directory,
                  options={"mupen64plus-pak1": "memory"},
                  timeout=180, prepare_save=lambda emu: None, progress=progress,
                  boot_symbols=control["boot_symbols"],
                  frame_lease=lambda n: reserve_frames(sys.stdin, stream_out, "control", n))
    try:
        while emu.frame < 1500:
            progress.mark("control-boot", emu)
            if emu.frame >= 120 and emu.frame % 120 == 0:
                emu.hold("START")
            else:
                emu.release("START")
            emu.step(1)
        state = emu.read_u32("gCurrentGameState")
        write_json(directory / "report.json", dict(
            status="pass" if state in (2, 4) else "fail",
            scope="diagnostic normal-ROM boot only; not normal gameplay revalidation",
            last_verified_boundary="control-1500-frame-boundary", local_frames=emu.frame,
            game_state=state, options=emu.options, core_sha256=digest(args.core.read_bytes())))
    finally:
        emu.close()


def run_children(args, command_factory=None):
    require(not args.out.exists(), "Use a fresh private run directory")
    args.out.mkdir(parents=True)
    # Each child loads exactly one core; reloads are fresh OS processes.
    names = ("control",) + SCENARIOS if getattr(args, "control", None) else SCENARIOS
    for name in names:
        write_json(args.out / (name + ".attempted.json"), dict(scenario=name))
        if command_factory is None:
            command = [sys.executable, "-m", "tools.rltest.story_sequential_worker"]
            for key in ("seal", "package", "binding", "core", "out", "seal_sha256", "core_sha256"):
                command += ["--" + key.replace("_", "-"), str(getattr(args, key))]
            if getattr(args, "control", None):
                command += ["--control", str(args.control)]
        else:
            command = command_factory(args)
        result = subprocess.run(command + ["--scenario", name], stdin=sys.stdin,
                                stdout=sys.stdout, stderr=sys.stderr, cwd=ROOT)
        require(result.returncode == 0, f"Scenario worker refused: {name}")
        if name == "loss-terminal-reload":
            write_json(args.out / (name + ".terminal-attempted.json"), dict(scenario=name))
            result = subprocess.run(command + ["--scenario", name, "--terminal-reload"],
                                    stdin=sys.stdin, stdout=sys.stdout, stderr=sys.stderr, cwd=ROOT)
            require(result.returncode == 0, "Terminal fresh process refused")
    print('{"done":true}', flush=True)
    require(sys.stdin.readline() == "OK\n", "Supervisor refused completion")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("seal", "package", "binding", "core", "out"):
        parser.add_argument("--" + name, type=pathlib.Path, required=True)
    parser.add_argument("--scenario", choices=("control",) + SCENARIOS)
    parser.add_argument("--seal-sha256", required=True)
    parser.add_argument("--core-sha256", required=True)
    parser.add_argument("--terminal-reload", action="store_true")
    parser.add_argument("--control", type=pathlib.Path)
    args = parser.parse_args()
    if args.scenario:
        require(not args.terminal_reload or args.scenario == "loss-terminal-reload",
                "Terminal reload only belongs to the loss scenario")
        # Core C stdout must not share the supervisor's JSON protocol.
        protocol = os.fdopen(os.dup(sys.stdout.fileno()), "w", buffering=1)
        os.dup2(sys.stderr.fileno(), sys.stdout.fileno())
        if args.scenario == "control":
            execute_control(args, stream_out=protocol)
        else:
            execute_scenario(args.scenario, args, stream_out=protocol)
    else:
        run_children(args)


if __name__ == "__main__":
    main()
