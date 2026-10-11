#!/usr/bin/env python3
"""Audit factual Red/Blue data; never download ROMs, graphics, music or dialogue."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import re
import urllib.request

REVISION = "af519899719f0754965776faac0e836a3b906e6d"
TREE_DIGEST = "9a47f4f79fd8746ef3d87f4d3100743c2cbd4e54302b3114c64dc1f6e23913aa"
RAW = f"https://raw.githubusercontent.com/pret/pokered/{REVISION}/"
TREE = f"https://api.github.com/repos/pret/pokered/git/trees/{REVISION}?recursive=1"
GROWTH = {"GROWTH_MEDIUM_FAST": 0, "GROWTH_FAST": 1,
          "GROWTH_SLOW": 2, "GROWTH_MEDIUM_SLOW": 3}
STONES = {"MOON_STONE": 1, "FIRE_STONE": 2, "THUNDER_STONE": 3,
          "WATER_STONE": 4, "LEAF_STONE": 5}
RIVALS = (
    ("OaksLab", "lab", "RIVAL1", 1),
    ("Route22", "early", "RIVAL1", 4),
    ("CeruleanCity", "bridge", "RIVAL1", 7),
    ("SSAnne2F", "ship", "RIVAL2", 1),
    ("PokemonTower2F", "tower", "RIVAL2", 4),
    ("SilphCo7F", "silph", "RIVAL2", 7),
    ("Route22", "late", "RIVAL2", 10),
    ("ChampionsRoom", "champion", "RIVAL3", 1),
)
GIFTS = (
    ("MtMoonPokecenter", "magikarp", (129,), 5),
    ("CeladonMansionRoofHouse", "eevee", (133,), 25),
    ("FightingDojo", "dojo", (106, 107), 30),
    ("SilphCo7F", "lapras", (131,), 15),
    ("CinnabarLabFossilRoom", "fossil", (138, 140), 30),
    ("CinnabarLabFossilRoom", "amber", (142,), 30),
)
SINGLE_MOVES = (
    ("BROCK", 1, "BIDE"), ("MISTY", 1, "BUBBLEBEAM"),
    ("LT_SURGE", 2, "THUNDERBOLT"), ("ERIKA", 2, "MEGA_DRAIN"),
    ("KOGA", 3, "TOXIC"), ("SABRINA", 3, "PSYWAVE"),
    ("BLAINE", 3, "FIRE_BLAST"), ("GIOVANNI", 4, "FISSURE"),
)
TEAM_MOVES = {"LORELEI": "BLIZZARD", "BRUNO": "FISSURE",
              "AGATHA": "TOXIC", "LANCE": "BARRIER"}
RETIREMENT_AREAS = (
    ("PewterGym", "BROCK", 1, "SetEvent"),
    ("CeruleanGym", "MISTY", 2, "SetEvents"),
    ("VermilionGym", "LT_SURGE", 3, "SetEventRange"),
    ("CeladonGym", "ERIKA", 7, "SetEventRange"),
    ("FuchsiaGym", "KOGA", 6, "SetEventRange"),
    ("SaffronGym", "SABRINA", 7, "SetEventRange"),
    ("CinnabarGym", "BLAINE", 7, "SetEventRange"),
    ("ViridianGym", "GIOVANNI", 8, "SetEventRange"),
    ("FightingDojo", "BLACKBELT", 4, "SetEventRange"),
)


class DataError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise DataError(message)


def clean(text, version=None):
    """Handle only the upstream version conditionals needed by factual tables."""
    active = [True]
    for number, original in enumerate(text.splitlines(), 1):
        line = original.split(";", 1)[0].strip()
        match = re.fullmatch(r"IF DEF\((_RED|_BLUE|_DEBUG)\)", line)
        if match:
            require(version is not None, "Version conditional without version")
            active.append(active[-1] and match[1] == "_" + version.upper())
        elif line == "IF DEF(_RED_VC) || DEF(_BLUE_VC)":
            require(version is not None, "Version conditional without version")
            active.append(False)
        elif line == "ELSE":
            require(len(active) > 1, f"Unexpected ELSE at {number}")
            active[-1] = active[-2] and not active[-1]
        elif line == "ENDC":
            require(len(active) > 1, f"Unexpected ENDC at {number}")
            active.pop()
        else:
            require(not re.match(r"(IF|ELIF)\b", line),
                    f"Unsupported conditional at {number}: {line}")
            if active[-1] and line:
                yield number, line
    require(len(active) == 1, "Unterminated version conditional")


def number(token):
    token = token.strip()
    return int(token[1:], 16) if token.startswith("$") else int(token, 10)


def constants(text):
    result = {}
    current = 0
    for _, line in clean(text):
        fields = line.split()
        if fields[0] == "const_def":
            current = number(fields[1]) if len(fields) > 1 else 0
        elif fields[0] == "const":
            require(fields[1] not in result, f"Duplicate constant {fields[1]}")
            result[fields[1]] = current
            current += 1
        elif fields[0] == "const_skip":
            current += number(fields[1]) if len(fields) > 1 else 1
    return result


class Source:
    def __init__(self, cache, fetch):
        self.root = Path(cache) / REVISION
        self.fetch = fetch
        self.provenance = {}
        self.blobs = {}

    def get(self, path):
        allowed = (path in {"constants/pokemon_constants.asm",
                           "constants/pokedex_constants.asm",
                           "constants/battle_constants.asm",
                           "constants/toggle_constants.asm",
                           "data/maps/toggleable_objects.asm",
                           "data/pokemon/evos_moves.asm", "data/pokemon/mew.asm",
                           "data/moves/moves.asm", "data/trainers/parties.asm",
                           "data/trainers/special_moves.asm", "home/move_mon.asm",
                           "engine/pokemon/experience.asm",
                           "engine/pokemon/evos_moves.asm", "engine/pokemon/add_mon.asm",
                           "engine/battle/read_trainer_party.asm", "engine/battle/experience.asm"}
                   or path in {"engine/items/item_effects.asm",
                               "engine/menus/start_sub_menus.asm", "engine/battle/core.asm"}
                   or re.fullmatch(r"data/pokemon/base_stats/\w+\.asm", path)
                   or re.fullmatch(r"data/maps/objects/\w+\.asm", path)
                   or re.fullmatch(r"scripts/\w+\.asm", path))
        require(allowed, f"Source path not allowlisted: {path}")
        limit = 256 * 1024 if path == "engine/battle/core.asm" else 128 * 1024
        destination = self.root / path
        if not destination.exists():
            require(self.fetch, f"Missing cached source {path}; run with --fetch")
            with urllib.request.urlopen(RAW + path, timeout=45) as response:
                data = response.read(limit + 1)
            require(len(data) <= limit, f"Oversized source {path}")
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
        data = destination.read_bytes()
        require(len(data) <= limit, f"Oversized cached source {path}")
        require(path in self.blobs, f"Source absent from pinned tree: {path}")
        blob = hashlib.sha1(f"blob {len(data)}\0".encode() + data).hexdigest()
        require(blob == self.blobs[path], f"Source hash mismatch: {path}")
        self.provenance[path] = hashlib.sha256(data).hexdigest()
        return data.decode("utf-8")

    def paths(self):
        self.blobs = {}
        destination = self.root / "tree.json"
        if not destination.exists():
            require(self.fetch, "Missing tree cache; run with --fetch")
            request = urllib.request.Request(TREE, headers={"User-Agent": "stadium-story-audit"})
            with urllib.request.urlopen(request, timeout=45) as response:
                data = json.load(response)
            self.verify_tree(data)
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_text(json.dumps(data), encoding="utf-8")
        data = json.loads(destination.read_text(encoding="utf-8"))
        self.verify_tree(data)
        self.blobs = {entry["path"]: entry["sha"] for entry in data["tree"]
                      if entry["type"] == "blob"}
        return [entry["path"] for entry in data["tree"] if entry["type"] == "blob"]

    @staticmethod
    def verify_tree(data):
        require(data.get("sha") == REVISION and data.get("truncated") is False,
                "Wrong source tree")
        entries = data.get("tree")
        require(isinstance(entries, list) and all(
            isinstance(e, dict) and all(isinstance(e.get(key), str)
                                       for key in ("path", "mode", "type", "sha"))
            for e in entries), "Invalid source tree entries")
        require(len({e["path"] for e in entries}) == len(entries),
                "Duplicate source tree path")
        normalized = {"sha": data["sha"], "tree": sorted(
            (e["path"], e["mode"], e["type"], e["sha"]) for e in entries)}
        raw = json.dumps(normalized, sort_keys=True, separators=(",", ":")).encode("ascii")
        require(hashlib.sha256(raw).hexdigest() == TREE_DIGEST,
                "Source tree digest mismatch")


def moves(text):
    result = {"NO_MOVE": 0}
    pp = [0]
    for _, line in clean(text):
        if line.startswith("move "):
            fields = [part.strip() for part in line[5:].split(",")]
            require(len(fields) == 6 and fields[0] not in result, f"Invalid move: {line}")
            result[fields[0]] = len(pp)
            pp.append(number(fields[5]))
            require(0 < pp[-1] <= 40, f"Invalid move PP: {line}")
    require(len(pp) == 166, f"Expected 165 moves, got {len(pp) - 1}")
    return result, pp


def base_stats(text, dex, move_ids):
    rows = [line[3:].strip() for _, line in clean(text)
            if line.startswith("db ")]
    require(len(rows) == 8, f"Expected eight base-data db rows, got {len(rows)}")
    name = rows[0]
    require(name in dex, f"Unknown dex entry {name}")
    stats = [number(n) for n in rows[1].split(",")]
    require(len(stats) == 5 and all(0 < n < 256 for n in stats), "Invalid base stats")
    initial = [move_ids[n.strip()] for n in rows[5].split(",")]
    require(len(initial) == 4 and initial[0], "Invalid starting moves")
    require(rows[6] in GROWTH, f"Unsupported growth {rows[6]}")
    return {"id": dex[name], "symbol": name[4:], "base": stats,
            "yield": number(rows[4]), "moves": initial, "growth": GROWTH[rows[6]]}


def evolution_moves(text, internal, dex, move_ids):
    pointers = []
    blocks = {}
    current = None
    for _, line in clean(text):
        if line.startswith("dw "):
            pointers.append(line[3:])
        elif re.fullmatch(r"\w+EvosMoves:", line):
            current = line[:-1]
            blocks[current] = []
        elif current and line.startswith("db "):
            blocks[current].append([part.strip() for part in line[3:].split(",")])
    result = {}
    for symbol, identifier in dex.items():
        mon = symbol[4:]
        require(mon in internal, f"No internal species {mon}")
        index = internal[mon] - 1
        require(0 <= index < len(pointers), f"Bad species pointer {mon}")
        rows = blocks[pointers[index]]
        evos = []
        learn = []
        in_moves = False
        terminated = False
        for row in rows:
            if row == ["0"]:
                if in_moves:
                    terminated = True
                    break
                in_moves = True
            elif not in_moves:
                method = {"EVOLVE_LEVEL": 1, "EVOLVE_ITEM": 2, "EVOLVE_TRADE": 3}[row[0]]
                size = 4 if method == 2 else 3
                require(len(row) == size, f"Bad evolution for {mon}")
                target = dex["DEX_" + row[-1]]
                level = number(row[-2])
                stone = STONES[row[1]] if method == 2 else 0
                evos.append([method, level, target, stone])
            else:
                require(len(row) == 2, f"Bad learnset for {mon}")
                learn.append([number(row[0]), move_ids[row[1]]])
        require(terminated, f"Unterminated learnset for {mon}")
        require(all(1 <= level <= 100 and move for level, move in learn), "Invalid learned move")
        require(learn == sorted(learn, key=lambda entry: entry[0]), f"Unsorted learnset {mon}")
        result[identifier] = (evos, learn)
    return result


def trainer_parties(text, dex):
    pointers = []
    groups = {}
    current = None
    for line_number, line in clean(text):
        if line.startswith("dw "):
            pointers.append(line[3:].strip())
        elif re.fullmatch(r"\w+Data:", line):
            current = line[:-1]
            groups[current] = []
        elif line.startswith("db "):
            require(current is not None, "Party outside trainer group")
            fields = [part.strip() for part in line[3:].split(",")]
            require(fields[-1] == "0", f"Unterminated trainer line {line_number}")
            special = fields[0] == "$FF"
            body = fields[1:-1]
            if special:
                require(len(body) % 2 == 0, "Odd special trainer party")
                team = [(body[i + 1], number(body[i])) for i in range(0, len(body), 2)]
            else:
                team = [(mon, number(fields[0])) for mon in body]
            require(1 <= len(team) <= 6, "Invalid trainer party size")
            records = []
            for symbol, level in team:
                require("DEX_" + symbol in dex and 1 <= level <= 100, "Invalid trainer mon")
                records.append({"species": dex["DEX_" + symbol], "level": level})
            groups[current].append({"mons": records, "special": special, "line": line_number})
    require(len(pointers) == 47 and all(p in groups for p in pointers), "Invalid trainer pointer table")
    result = {re.sub(r"Data$", "", key).replace("_", "").upper(): value
              for key, value in groups.items()}
    require(sum(map(len, result.values())) == 391, "Unexpected stored party count")
    return result


def construct_moves(species, level):
    result = list(species["moves"])
    for learned_level, move in species["learn"]:
        if learned_level > level:
            break
        if move in result:
            continue
        if 0 in result:
            result[result.index(0)] = move
        else:
            result = result[1:] + [move]
    return result


def trainer_key(class_name):
    return "PSYCHIC" if class_name == "PSYCHIC_TR" else class_name.replace("_", "")


def party(groups, class_name, index, species, move_ids, pp, leader=False):
    key = trainer_key(class_name)
    require(key in groups and 1 <= index <= len(groups[key]),
            f"Unresolved trainer {class_name}/{index}")
    entry = groups[key][index - 1]
    result = []
    for mon in entry["mons"]:
        slots = construct_moves(species[mon["species"]], mon["level"])
        result.append(dict(mon, moves=slots, dvs=0x8889, stat_exp=[0] * 5))
    if entry["special"]:
        if leader:
            overrides = [(slot, move) for cls, slot, move in SINGLE_MOVES if cls == class_name]
            require(len(overrides) == 1, f"Unknown leader {class_name}")
            slot, move = overrides[0]
            result[slot]["moves"][2] = move_ids[move]
        elif class_name in TEAM_MOVES:
            result[4]["moves"][2] = move_ids[TEAM_MOVES[class_name]]
        elif class_name == "RIVAL3":
            result[0]["moves"][2] = move_ids["SKY_ATTACK"]
            starter_move = {9: "BLIZZARD", 3: "MEGA_DRAIN", 6: "FIRE_BLAST"}
            result[5]["moves"][2] = move_ids[starter_move[result[5]["species"]]]
    for mon in result:
        mon["pp"] = [pp[move] for move in mon["moves"]]
    return result


def object_battles(text, version):
    for line_number, line in clean(text, version):
        if not line.startswith("object_event "):
            continue
        fields = [field.strip() for field in line[13:].split(",")]
        require(6 <= len(fields) <= 8, f"Unsupported object syntax {line}")
        if len(fields) == 8:
            yield line_number, fields[5], fields[6], number(fields[7])


def silph_retirement(encounters, scripts, toggle_constants, toggle_objects, version):
    mapping = {}
    for floor in range(2, 12):
        area = f"SilphCo{floor}F"
        map_id = f"SILPH_CO_{floor}F"
        def block(text, directive):
            match = re.search(rf"^\s*{directive} {map_id}\s*(?:;[^\n]*)?\n"
                              rf"(.*?)(?=^\s*{directive} |\Z)", text, re.M | re.S)
            require(match is not None, f"Missing toggle map {map_id}")
            return list(clean(match[1], version))
        constants_rows = block(toggle_constants, "toggle_consts_for")
        object_rows = block(toggle_objects, "toggleable_objects_for")
        keys = [code.split()[1] for _, code in constants_rows if code.startswith("const ")]
        objects = []
        for _, code in object_rows:
            match = re.fullmatch(r"toggle_object_state (\w+),\s*(ON|OFF)", code)
            require(match is not None, f"Unsupported Silph toggle row: {code}")
            objects.append(match[1])
        require(len(keys) == len(objects) and keys and len(set(keys)) == len(keys),
                f"Changed toggle mapping in {area}")
        for key, obj in zip(keys, objects):
            mapping[key] = f"{area}/TEXT_{obj}"
    path = "scripts/SilphCo11F.asm"
    text = scripts[path]
    match = re.search(r"^\.HideToggleableObjectIDs:\n(.*?)^\s*db -1", text, re.M | re.S)
    require(match is not None, "Missing Silph retirement list")
    tokens = [(line, code[3:]) for line, code in clean(match[1], version)
              if code.startswith("db ")]
    require(len(tokens) == 40 and len(set(token for _, token in tokens)) == 40,
            "Changed Silph hide list")
    by_id = {e["id"]: e for e in encounters}
    trigger = "SilphCo11F/TEXT_SILPHCO11F_GIOVANNI"
    retired = []
    for _, token in tokens:
        if token.startswith("TOGGLE_SILPH_"):
            require(token in mapping, f"Unknown Silph toggle {token}")
            identifier = mapping[token]
            require(identifier in by_id and by_id[identifier]["kind"] == "trainer",
                    f"Silph hide list is not a trainer: {identifier}")
            if identifier != trigger:
                retired.append(identifier)
    require(len(retired) == 30, "Changed Silph retired trainer count")
    rows = list(clean(text, version))
    call_lines = [line for line, code in rows if code == "call SilphCo11FTeamRocketLeavesScript"]
    require(len(call_lines) == 1 and trigger in by_id, "Changed Silph retirement call")
    return {"after": trigger, "retires": sorted(retired), "source": path,
            "line": call_lines[0], "operation": "HideObject",
            "toggles": [token for _, token in tokens],
            "mapping_sources": ["constants/toggle_constants.asm",
                                "data/maps/toggleable_objects.asm"]}


def rival_branch_audit(scripts, version):
    result = []
    for area, stage, class_name, first in RIVALS:
        path = f"scripts/{area}.asm"
        rows = list(clean(scripts[path], version))
        if area == "Route22":
            helper = [code for _, code in rows]
            begin = helper.index("Route22GetRivalTrainerNoByStarterScript:")
            require(helper[begin + 1:begin + 14] == [
                "ld a, [wRivalStarter]", "ld b, a", ".next_trainer_no",
                "ld a, [hli]", "cp b", "jr z, .got_trainer_no", "inc hl",
                "jr .next_trainer_no", ".got_trainer_no", "ld a, [hl]",
                "ld [wTrainerNo], a", "ret", "Route22MoveRivalRightScript:"],
                "Changed Route22 rival lookup semantics")
            start = next(i for i, (_, code) in enumerate(rows)
                         if code == "ld a, OPP_" + class_name)
            tables = [i for i in range(start, len(rows)) if rows[i][1] == ".StarterTable:"]
            require(tables, f"Missing rival starter table: {stage}")
            table = rows[tables[0] + 1:tables[0] + 4]
            require([code for _, code in table] ==
                    [f"db STARTER{starter}, {first + branch}"
                     for branch, starter in enumerate((2, 3, 1))],
                    f"Changed rival starter table: {stage}")
            require("call Route22GetRivalTrainerNoByStarterScript" in
                    [code for _, code in rows[start:tables[0]]],
                    f"Unreferenced rival starter table: {stage}")
            line = table[0][0]
        else:
            start = next(i for i, (_, code) in enumerate(rows)
                         if code == "ld a, OPP_" + class_name)
            load = next(i for i in range(start, len(rows))
                        if rows[i][1] == "ld a, [wRivalStarter]")
            end = next(i for i in range(load, len(rows))
                       if rows[i][1] == "ld [wTrainerNo], a")
            block = [code.rstrip(":") for _, code in rows[load + 1:end]]
            require(len(block) == 12, f"Changed rival selector: {area}")
            require(block[0] == "cp STARTER2" and block[5] == "cp STARTER3",
                    f"Changed rival branch order: {area}")
            second, third, done = block[4], block[9], block[11]
            require(all(re.fullmatch(r"\.\w+", label) for label in (second, third, done)) and
                    len({second, third, done}) == 3 and
                    block[1] == f"jr nz, {second}" and block[6] == f"jr nz, {third}" and
                    block[3] == block[8] == f"jr {done}",
                    f"Changed rival branch jumps: {area}")
            actual = []
            for index in (2, 7, 10):
                require(block[index].startswith("ld a, "), f"Changed rival team load: {area}")
                actual.append(number(block[index][6:]))
            require(actual == [first, first + 1, first + 2],
                    f"Changed rival team IDs: {area}")
            line = rows[load][0]
        result.append({"id": f"{area}/rival-{stage}", "source": path, "line": line,
                       "class": class_name,
                       "starter_constants": [2, 3, 1],
                       "party_indices": [first, first + 1, first + 2]})
    return result


def ship_retirement(encounters, scripts, version):
    path = "scripts/VermilionDock.asm"
    rows = list(clean(scripts[path], version))
    codes = [code for _, code in rows]
    expected = ["CheckEventReuseHL EVENT_GOT_HM01", "ret z",
                "ld a, [wDestinationWarpID]", "cp $1", "ret nz",
                "CheckEventReuseHL EVENT_SS_ANNE_LEFT",
                "jp z, VermilionDockSSAnneLeavesScript"]
    start = codes.index(expected[0])
    require(codes[start:start + len(expected)] == expected, "Changed ship departure condition")
    depart = codes.index("VermilionDockSSAnneLeavesScript:")
    require(codes[depart + 1] == "SetEventForceReuseHL EVENT_SS_ANNE_LEFT",
            "Changed ship departure event")
    retired = [e["id"] for e in encounters if e["area"].startswith("SSAnne")]
    require(len(retired) == 17, "Changed ship encounter count")
    return {"after_event": "EVENT_SS_ANNE_LEFT", "retires": sorted(retired),
            "source": path, "line": rows[start][0],
            "requires_event": "EVENT_GOT_HM01", "destination_warp": 1,
            "note": "Winning the ship rival alone does not depart the ship."}


def access_fact_audit(source, version):
    facts = []
    specifications = (
        ("cut_badge", "engine/menus/start_sub_menus.asm",
         [".cut", "bit BIT_CASCADEBADGE, a", "jp z, .newBadgeRequired"]),
        ("surf_badge", "engine/menus/start_sub_menus.asm",
         [".surf", "bit BIT_SOULBADGE, a", "jp z, .newBadgeRequired"]),
        ("strength_badge", "engine/menus/start_sub_menus.asm",
         [".strength", "bit BIT_RAINBOWBADGE, a", "jp z, .newBadgeRequired"]),
        ("tower_scope", "engine/battle/core.asm",
         ["IsGhostBattle:", "ld a, [wIsInBattle]", "dec a", "ret nz",
          "ld a, [wCurMap]", "cp POKEMON_TOWER_1F", "jr c, .next",
          "cp POKEMON_TOWER_7F + 1", "jr nc, .next", "ld b, SILPH_SCOPE",
          "call IsItemInBag", "ret z"]),
        ("snorlax_flute", "engine/items/item_effects.asm",
         ["ItemUsePokeFlute:", "ld a, [wIsInBattle]", "and a", "jr nz, .inBattle",
          "call ItemUseReloadOverworldData", "ld a, [wCurMap]", "cp ROUTE_12",
          "jr nz, .notRoute12", "CheckEvent EVENT_BEAT_ROUTE12_SNORLAX",
          "jr nz, .noSnorlaxToWakeUp", "ld hl, Route12SnorlaxFluteCoords",
          "call ArePlayerCoordsInArray", "jr nc, .noSnorlaxToWakeUp",
          "ld hl, PlayedFluteHadEffectText", "call PrintText",
          "SetEvent EVENT_FIGHT_ROUTE12_SNORLAX"]),
        ("snorlax_flute_west", "engine/items/item_effects.asm",
         [".notRoute12", "cp ROUTE_16", "jr nz, .noSnorlaxToWakeUp",
          "CheckEvent EVENT_BEAT_ROUTE16_SNORLAX", "jr nz, .noSnorlaxToWakeUp",
          "ld hl, Route16SnorlaxFluteCoords", "call ArePlayerCoordsInArray",
          "jr nc, .noSnorlaxToWakeUp", "ld hl, PlayedFluteHadEffectText",
          "call PrintText", "SetEvent EVENT_FIGHT_ROUTE16_SNORLAX"]),
        ("postgame_cave", "scripts/HallOfFame.asm",
         ["ld a, TOGGLE_CERULEAN_CAVE_GUY", "ld [wToggleableObjectIndex], a",
          "predef HideObject"]),
        ("cut_acquisition", "scripts/SSAnneCaptainsRoom.asm",
         ["lb bc, HM_CUT, 1", "call GiveItem", "jr nc, .bag_full",
          "ld hl, SSAnneCaptainsRoomCaptainReceivedHM01Text", "call PrintText",
          "SetEvent EVENT_GOT_HM01"]),
        ("surf_acquisition", "scripts/SafariZoneSecretHouse.asm",
         ["lb bc, HM_SURF, 1", "call GiveItem", "jr nc, .bag_full",
          "ld hl, .ReceivedHM03Text", "call PrintText", "SetEvent EVENT_GOT_HM03"]),
        ("strength_acquisition", "scripts/WardensHouse.asm",
         ["lb bc, HM_STRENGTH, 1", "call GiveItem", "jr nc, .bag_full",
          "ld hl, .ReceivedHM04Text", "call PrintText", "SetEvent EVENT_GOT_HM04"]),
        ("amber_acquisition", "scripts/Museum1F.asm",
         ["lb bc, OLD_AMBER, 1", "call GiveItem", "jr nc, .bag_full",
          "SetEvent EVENT_GOT_OLD_AMBER"]),
        ("fossil_exclusion", "scripts/MtMoonB2F.asm",
         ["MtMoonB2FCheckGotAFossil:", "CheckEitherEventSet EVENT_GOT_DOME_FOSSIL, EVENT_GOT_HELIX_FOSSIL",
          "jp z, CheckFightingMapTrainers", "ret"]),
        ("viridian_seven_badges", "scripts/ViridianCity.asm",
         ["ld a, [wObtainedBadges]", "cp ~(1 << BIT_EARTHBADGE)",
          "jr nz, .gym_closed", "SetEvent EVENT_VIRIDIAN_GYM_OPEN"]),
    )
    for identifier, path, expected in specifications:
        rows = list(clean(source.get(path), version))
        codes = [code for _, code in rows]
        starts = [i for i, code in enumerate(codes)
                  if codes[i:i + len(expected)] == expected]
        require(len(starts) == 1, f"Changed access fact: {identifier}")
        facts.append({"id": identifier, "source": path, "line": rows[starts[0]][0],
                      "end_line": rows[starts[0] + len(expected) - 1][0]})
    return facts


def retirement_audit(encounters, scripts, version):
    """Check bounded retirement facts, not general assembly control flow."""
    rules = []
    for area, leader, count, operation in RETIREMENT_AREAS:
        path = f"scripts/{area}.asm"
        rows = list(clean(scripts[path], version))
        if area == "CinnabarGym":
            headers = [re.fullmatch(r"CheckEvent (EVENT_BEAT_CINNABAR_GYM_TRAINER_\d+)", code)
                       for _, code in rows if code.startswith("CheckEvent EVENT_BEAT_CINNABAR_GYM_TRAINER_")]
        else:
            headers = [re.match(r"trainer (EVENT_\w+),", code)
                       for _, code in rows if code.startswith("trainer ")]
        require(len(headers) == count and all(headers),
                f"Changed retirement trainer headers in {area}")
        flags = [match[1] for match in headers]
        prefix = flags[0].rsplit("_", 1)[0]
        require(flags == [f"{prefix}_{i}" for i in range(count)],
                f"Non-contiguous retirement flags in {area}")
        if area == "FightingDojo":
            operands = ["EVENT_BEAT_KARATE_MASTER", flags[-1]]
            trigger = [e for e in encounters if e["area"] == area and e.get("party") == 1]
        else:
            operands = flags if count <= 2 else [flags[0], flags[-1]]
            trigger = [e for e in encounters if e["area"] == area and e.get("class") == leader]
        directive = operation + " " + ", ".join(operands)
        matches = [line for line, code in rows if code == directive]
        require(len(matches) == 1 and len(trigger) == 1,
                f"Changed retirement trigger in {area}")
        retired = [e["id"] for e in encounters if e["area"] == area and
                   e["kind"] == "trainer" and e["id"] != trigger[0]["id"]]
        require(len(retired) == count, f"Changed retirement inventory in {area}")
        rules.append({"after": trigger[0]["id"], "retires": sorted(retired),
                      "source": path, "line": matches[0], "operation": operation,
                      "events": operands})
    path = "scripts/PewterGym.asm"
    matches = [line for line, code in clean(scripts[path], version)
               if code == "ResetEvents EVENT_1ST_ROUTE22_RIVAL_BATTLE, EVENT_ROUTE22_RIVAL_WANTS_BATTLE"]
    require(len(matches) == 1, "Changed early rival retirement")
    early = "Route22/rival-early"
    require(any(e["id"] == early for e in encounters), "Missing early rival")
    rules.append({"after": rules[0]["after"], "retires": [early],
                  "source": path, "line": matches[0], "operation": "ResetEvents",
                  "events": ["EVENT_1ST_ROUTE22_RIVAL_BATTLE",
                             "EVENT_ROUTE22_RIVAL_WANTS_BATTLE"]})
    return rules


def audit(source):
    paths = source.paths()
    for path in ("home/move_mon.asm", "engine/pokemon/experience.asm",
                 "engine/pokemon/evos_moves.asm", "engine/pokemon/add_mon.asm",
                 "engine/battle/read_trainer_party.asm", "engine/battle/experience.asm"):
        source.get(path)
    dex = constants(source.get("constants/pokedex_constants.asm"))
    dex = {name: value for name, value in dex.items() if name.startswith("DEX_")}
    require(len(dex) == 151 and set(dex.values()) == set(range(1, 152)), "Invalid dex")
    pokemon_constants = source.get("constants/pokemon_constants.asm")
    internal = constants(pokemon_constants)
    for starter, species_name in ((1, "CHARMANDER"), (2, "SQUIRTLE"), (3, "BULBASAUR")):
        require(re.search(rf"^DEF STARTER{starter} EQU {species_name}$",
                          pokemon_constants, re.M), "Changed starter constant identity")
    move_ids, pp = moves(source.get("data/moves/moves.asm"))
    base_paths = sorted(p for p in paths if re.fullmatch(r"data/pokemon/base_stats/\w+\.asm", p))
    if "data/pokemon/base_stats/mew.asm" not in base_paths:
        base_paths.append("data/pokemon/mew.asm")
    object_paths = sorted(p for p in paths if re.fullmatch(r"data/maps/objects/\w+\.asm", p))
    with ThreadPoolExecutor(max_workers=6) as pool:
        texts = dict(zip(base_paths + object_paths,
                         pool.map(source.get, base_paths + object_paths)))
    species = {}
    for path in base_paths:
        record = base_stats(texts[path], dex, move_ids)
        require(record["id"] not in species, "Duplicate species")
        record["source"] = path
        species[record["id"]] = record
    require(set(species) == set(range(1, 152)), "Missing species data")
    evos = evolution_moves(source.get("data/pokemon/evos_moves.asm"), internal, dex, move_ids)
    for identifier, (evolution, learn) in evos.items():
        species[identifier].update(evolutions=evolution, learn=learn)
    groups = trainer_parties(source.get("data/trainers/parties.asm"), dex)
    special = source.get("data/trainers/special_moves.asm")
    rows = [line[3:].replace(" ", "") for _, line in clean(special) if line.startswith("db ")]
    require(rows[:8] == [f"{slot},{move}" for _, slot, move in SINGLE_MOVES], "Changed leader overrides")
    require(rows[8:12] == [f"{cls},{move}" for cls, move in TEAM_MOVES.items()], "Changed E4 overrides")
    dvs = source.get("constants/battle_constants.asm")
    require(re.search(r"ATKDEFDV_TRAINER EQU \$98\b", dvs) and
            re.search(r"SPDSPCDV_TRAINER EQU \$88\b", dvs), "Changed trainer DVs")
    encounters = {}
    scripts = set(area for area, _, _, _ in RIVALS) | set(area for area, _, _, _ in GIFTS)
    scripts |= {"PokemonTower6F", "MtMoonB2F", "Museum1F", "Museum2F",
                "SSAnneCaptainsRoom", "VermilionDock"}
    used = set()
    for version in ("red", "blue"):
        output = []
        for path in object_paths:
            area = Path(path).stem
            for line, text_id, opponent, index in object_battles(texts[path], version):
                if opponent.startswith("OPP_"):
                    class_name = opponent[4:]
                    if class_name.startswith("RIVAL"):
                        continue
                    is_leader = area.endswith("Gym") and class_name in {c for c, _, _ in SINGLE_MOVES}
                    mons = party(groups, class_name, index, species, move_ids, pp, is_leader)
                    used.add((trainer_key(class_name), index))
                    output.append({"id": f"{area}/{text_id}", "area": area,
                                   "kind": "trainer", "class": class_name, "party": index,
                                   "teams": [mons], "source": path, "line": line,
                                   "party_line": groups[trainer_key(class_name)][index - 1]["line"]})
                    scripts.add(area)
                elif "DEX_" + opponent in dex:
                    require(area in {"PowerPlant", "SeafoamIslandsB4F", "VictoryRoad2F", "CeruleanCaveB1F"},
                            f"Unclassified static object {area}/{opponent}")
                    identifier = dex["DEX_" + opponent]
                    slots = construct_moves(species[identifier], index)
                    output.append({"id": f"{area}/{text_id}", "area": area, "kind": "static",
                                   "teams": [[{"species": identifier, "level": index, "moves": slots,
                                              "pp": [pp[m] for m in slots]}]],
                                   "source": path, "line": line})
                else:
                    # Item object IDs are not encounters.
                    require(opponent not in internal or opponent == "NO_MON",
                            f"Non-dex species object {path}:{line}")
        for area, stage, class_name, first in RIVALS:
            teams = [party(groups, class_name, first + branch, species, move_ids, pp)
                     for branch in range(3)]
            output.append({"id": f"{area}/rival-{stage}", "area": area, "kind": "rival",
                           "class": class_name, "party": first, "teams": teams,
                           "source": f"scripts/{area}.asm"})
            used.update((class_name, first + branch) for branch in range(3))
        for area, gift, options, level in GIFTS:
            teams = []
            for identifier in options:
                slots = construct_moves(species[identifier], level)
                teams.append([{"species": identifier, "level": level, "moves": slots,
                               "pp": [pp[m] for m in slots]}])
            output.append({"id": f"{area}/gift-{gift}", "area": area, "kind": "gift",
                           "teams": teams, "source": f"scripts/{area}.asm"})
        for area in ("Route12", "Route16", "PokemonTower6F"):
            identifier = 105 if area == "PokemonTower6F" else 143
            slots = construct_moves(species[identifier], 30)
            output.append({"id": f"{area}/{'ghost' if identifier == 105 else 'snorlax'}",
                           "area": area, "kind": "ghost" if identifier == 105 else "static",
                           "teams": [[{"species": identifier, "level": 30, "moves": slots,
                                      "pp": [pp[m] for m in slots]}]],
                           "source": f"scripts/{area}.asm"})
            scripts.add(area)
        output.sort(key=lambda entry: entry["id"])
        require(len({entry["id"] for entry in output}) == len(output), "Duplicate encounter ID")
        encounters[version] = output
    script_paths = [f"scripts/{area}.asm" for area in sorted(scripts)]
    with ThreadPoolExecutor(max_workers=6) as pool:
        script_texts = dict(zip(script_paths, pool.map(source.get, script_paths)))
    evidence = {}
    for path, text in script_texts.items():
        # Export only event operands and line numbers, never scripts/dialogue.
        event_lines = []
        for line, code in clean(text, "red"):
            if re.search(r"\b(?:Check\w*Event\w*|SetEvents?|ResetEvents?|SetEventRange)\b", code):
                tokens = re.findall(r"\bEVENT_\w+\b", code)
                if tokens:
                    event_lines.append({"line": line, "events": tokens,
                                        "operation": code.split()[0]})
        evidence[path] = event_lines
    for area, _, class_name, _ in RIVALS:
        text = script_texts[f"scripts/{area}.asm"]
        require("OPP_" + class_name in text or area == "OaksLab",
                f"No scripted rival class evidence in {area}")
    for area, _, options, level in GIFTS:
        text = script_texts[f"scripts/{area}.asm"]
        if len(options) == 1 and area != "CinnabarLabFossilRoom":
            symbol = species[options[0]]["symbol"]
            require(re.search(rf"\b{symbol}\s*,\s*{level}\b", text),
                    f"Gift species/level changed in {area}")
        else:
            require(re.search(rf"ld c, {level}\b", text), f"Gift level changed in {area}")
    toggle_constants = source.get("constants/toggle_constants.asm")
    toggle_objects = source.get("data/maps/toggleable_objects.asm")
    all_parties = {(key, index + 1) for key, entries in groups.items()
                   for index in range(len(entries))}
    return {"schema": 1, "revision": REVISION, "species": list(sorted(species.values(), key=lambda e: e["id"])),
            "pp": pp, "encounters": encounters, "stored_parties": 391,
            "unreferenced_parties": [list(entry) for entry in sorted(all_parties - used)],
            "script_event_evidence": evidence,
            "retirement_audit": {
                version: retirement_audit(entries, script_texts, version) +
                         [silph_retirement(entries, script_texts, toggle_constants,
                                           toggle_objects, version)]
                for version, entries in encounters.items()},
            "rival_branch_audit": {
                version: rival_branch_audit(script_texts, version)
                for version in encounters},
            "event_retirement_audit": {
                version: [ship_retirement(entries, script_texts, version)]
                for version, entries in encounters.items()},
            "access_fact_audit": {version: access_fact_audit(source, version)
                                  for version in encounters},
            "availability_audit_complete": False,
            "availability_note": "Map/script encounter inventory is not a verified traversal/retirement graph.",
            "sources": dict(sorted(source.provenance.items()))}


def c_array(values):
    return "{" + ", ".join(map(str, values)) + "}"


def emit_c(data, path):
    lines = ['#include "src/story/sc_data.h"', "",
             "const RlU8 sc_pp[166] = " + c_array(data["pp"]) + ";",
             "const ScSpecies sc_species[152] = {",
             "    {{0, 0, 0, 0, 0}, 0, 0, {0, 0, 0, 0}, 0, 0, {{0, 0}}, {{0, 0, 0, 0}}},"]
    for mon in data["species"]:
        learn = "{" + ", ".join(c_array(pair) for pair in mon["learn"]) + "}" if mon["learn"] else "{{0}}"
        evos = "{" + ", ".join(c_array(evo) for evo in mon["evolutions"]) + "}" if mon["evolutions"] else "{{0}}"
        lines.append("    {%s, %d, %d, %s, %d, %d, %s, %s}," %
                     (c_array(mon["base"]), mon["yield"], mon["growth"],
                      c_array(mon["moves"]), len(mon["learn"]), len(mon["evolutions"]), learn, evos))
    lines.extend(["};", "", "const ScEncounter sc_encounters[] = {"])
    kinds = {"trainer": 0, "rival": 1, "static": 2, "gift": 3, "ghost": 4}
    # English Red/Blue share every encounter in this bounded scope.
    require(data["encounters"]["red"] == data["encounters"]["blue"],
            "Version-specific encounters require separate catalogs")
    for entry in data["encounters"]["red"]:
        teams = []
        for team in entry["teams"]:
            mons = ["{%d, %d, %s, %d, %d}" %
                    (mon["species"], mon["level"], c_array(mon["moves"]),
                     int("dvs" in mon), mon.get("dvs", 0)) for mon in team]
            teams.append("{%d, {%s}}" % (len(team), ", ".join(mons)))
        lines.append("    {%s, %d, %d, {%s}, {0, 0, 0, {0}, 0}, 0, 0}," %
                     (json.dumps(entry["id"]), kinds[entry["kind"]], len(teams), ", ".join(teams)))
    lines.extend(["};", "", "const ScCatalog sc_catalog = {",
                  "    sc_species, sc_pp, sc_encounters,",
                  "    sizeof(sc_encounters) / sizeof(sc_encounters[0]), 0", "};", ""])
    destination = Path(path)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text("\n".join(lines), encoding="ascii")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fetch", action="store_true", help="Fetch allowlisted public source files")
    parser.add_argument("--cache", default=".local/story-source")
    parser.add_argument("--out", default="build/story/data.json")
    parser.add_argument("--c-out", default="build/story/sc_data.c")
    args = parser.parse_args()
    try:
        data = audit(Source(args.cache, args.fetch))
        destination = Path(args.out)
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
        emit_c(data, args.c_out)
    except (DataError, KeyError, ValueError, OSError) as error:
        parser.exit(1, f"Story data audit failed: {error}\n")
    for version, entries in data["encounters"].items():
        counts = {kind: sum(e["kind"] == kind for e in entries)
                  for kind in ("trainer", "rival", "static", "gift", "ghost")}
        print(f"{version}: {len(entries)} encounter instances: {counts}")
    print(f"151 species; 391 stored parties; {len(data['unreferenced_parties'])} unreferenced parties")
    print("Availability/retirement graph review remains required.")


if __name__ == "__main__":
    main()
