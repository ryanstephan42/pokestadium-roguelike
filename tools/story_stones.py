"""Finite, source-backed Moon Stones; independent review of the graph extension."""

import copy
import hashlib
import json
import re

from tools.story_data import clean, require
from tools.story_graph import REVIEWED_DIGEST

# Remains unreviewed until the exact facts/extended graph digest is accepted.
MOON_REVIEWED_DIGEST = None

PICKUPS = (
    ("Route2", 13, 54, False, "field:cut"),
    ("MtMoon1F", 2, 2, False, "PewterGym/TEXT_PEWTERGYM_BROCK"),
    ("MtMoonB2F", 18, 12, True, "PewterGym/TEXT_PEWTERGYM_BROCK"),
    ("RocketHideoutB2F", 1, 11, False, "GameCorner/TEXT_GAMECORNER_ROCKET"),
    ("PokemonMansion1F", 8, 16, True, "field:surf"),
)


def moon_audit(source):
    facts, paths = [], set()
    for area, x, y, hidden, gate in PICKUPS:
        path = "data/events/hidden_events.asm" if hidden else f"data/maps/objects/{area}.asm"
        paths.add(path)
        content = source.get(path)
        for version in ("red", "blue"):
            matches = []
            current = None
            for line, code in clean(content, version):
                if code.startswith("hidden_events_for "):
                    current = code.split()[1]
                if hidden:
                    match = re.fullmatch(r"hidden_event\s+(\d+),\s*(\d+),\s*HiddenItems,\s*MOON_STONE", code)
                    expected = {"MtMoonB2F": "MT_MOON_B2F", "PokemonMansion1F": "POKEMON_MANSION_1F"}
                    if match and current == expected[area]:
                        matches.append((line, int(match[1]), int(match[2])))
                else:
                    match = re.fullmatch(
                        r"object_event\s+(\d+),\s*(\d+),\s*SPRITE_POKE_BALL,\s*STAY,\s*NONE,"
                        r"\s*TEXT_\w+,\s*MOON_STONE", code)
                    if match:
                        matches.append((line, int(match[1]), int(match[2])))
            require(len(matches) == 1 and matches[0][1:] == (x, y),
                    f"Changed Moon Stone pickup: {area}/{version}")
        facts.append({"id": f"stone:{area}:{x},{y}", "area": area, "x": x, "y": y,
                      "hidden": hidden, "source": path, "line": matches[0][0], "gate": gate})

    dimensions_path = "constants/map_constants.asm"
    paths.add(dimensions_path)
    dimensions = source.get(dimensions_path)
    require(re.search(r"map_const ROUTE_2,\s*10,\s*36\b", dimensions), "Changed Route2 dimensions")
    map_path, swaps_path = "maps/Route2.blk", "data/tilesets/cut_tree_blocks.asm"
    paths.update((map_path, swaps_path))
    blocks = source.read(map_path)
    swaps = source.get(swaps_path)
    require(len(blocks) == 360 and blocks[11 * 10 + 7] == 0x32 and
            blocks[26 * 10 + 6] == 0x34, "Changed Route2 north/item Cut barriers")
    require(re.search(r"db \$32,\s*\$6D\b", swaps) and re.search(r"db \$34,\s*\$6F\b", swaps),
            "Changed Route2 Cut block transformations")

    hidden_path, use_path = "engine/events/hidden_items.asm", "engine/items/item_effects.asm"
    paths.update((hidden_path, use_path))
    hidden = source.get(hidden_path)
    use = source.get(use_path)
    require(re.search(r"ld b, FLAG_TEST\s+predef FlagActionPredef", hidden) and
            re.search(r"call GiveItem\s+jr nc, \.bagFull", hidden) and
            re.search(r"ld b, FLAG_SET\s+predef FlagActionPredef", hidden),
            "Changed one-time hidden pickup semantics")
    require(re.search(r"ItemUseEvoStone:\s+ld a, \[wIsInBattle\]\s+and a\s+jp nz, ItemUseNotTime", use) and
            re.search(r"callfar TryEvolvingMon[^\n]*\n\s*ld a, \[wEvolutionOccurred\]\s+and a\s+jr z, \.noEffect", use) and
            re.search(r"ld a, 1[^\n]*\n\s*ld \[wItemQuantity\], a\s+jp RemoveItemFromInventory", use),
            "Changed successful-use stone consumption semantics")
    return {"pickups": facts, "route2_cut_blocks": [[14, 22, 0x32], [12, 52, 0x34]],
            "sources": {path: source.provenance[path] for path in sorted(paths)}}


def extend_graph(base, facts):
    require(len(facts["pickups"]) == 5, "Incomplete finite Moon Stone inventory")
    graph = copy.deepcopy(base)
    for pickup in facts["pickups"]:
        identifier, gate = pickup["id"], pickup["gate"]
        require(identifier not in graph["nodes"] and gate in graph["nodes"],
                "Invalid Moon Stone graph extension")
        graph["nodes"][identifier] = {"clauses": [[gate]], "kind": "action", "variants": 1}
    identity = hashlib.sha256(json.dumps(
        {"base_identity": REVIEWED_DIGEST, "facts": facts, "graph": graph},
        default=lambda value: sorted(value), sort_keys=True,
        separators=(",", ":")).encode("ascii")).hexdigest()
    if MOON_REVIEWED_DIGEST is not None:
        require(identity == MOON_REVIEWED_DIGEST, "Moon Stone facts/graph differ from accepted review")
    return graph, identity, identity == MOON_REVIEWED_DIGEST
