import hashlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from tools.story_data import (
    DataError, REVISION, Source, base_stats, clean, construct_moves,
    constants, object_battles, trainer_key,
    retirement_audit,
    rival_branch_audit, ship_retirement,
    access_fact_audit,
)
from tools.story_graph import advance, available, candidate, healing_available, traverse


class ParserTests(unittest.TestCase):
    def test_retail_conditionals(self):
        text = """IF DEF(_RED)
db 1
ELSE
db 2
ENDC
IF DEF(_DEBUG)
db 3
ENDC"""
        self.assertEqual(list(clean(text, "red")), [(2, "db 1")])
        self.assertEqual(list(clean(text, "blue")), [(4, "db 2")])
        self.assertEqual(list(clean("IF DEF(_RED_VC) || DEF(_BLUE_VC)\ndb 1\nELSE\ndb 2\nENDC",
                                    "red")), [(4, "db 2")])
        for text in ("ELSE", "ENDC", "IF DEF(_RED)", "IF BAD"):
            with self.assertRaises(DataError):
                list(clean(text, "red"))

    def test_duplicate_skip_shift(self):
        mon = {"moves": [1, 2, 0, 0], "learn": [[3, 2], [4, 3], [5, 4], [6, 5]]}
        self.assertEqual(construct_moves(mon, 3), [1, 2, 0, 0])
        self.assertEqual(construct_moves(mon, 5), [1, 2, 3, 4])
        self.assertEqual(construct_moves(mon, 6), [2, 3, 4, 5])

    def test_constant_ids_not_dex(self):
        result = constants("const_def\nconst RHYDON\nconst_skip\nconst BULBASAUR")
        self.assertEqual(result, {"RHYDON": 0, "BULBASAUR": 2})
        self.assertEqual(trainer_key("PSYCHIC_TR"), "PSYCHIC")

    def test_objects_fail_closed(self):
        self.assertEqual(list(object_battles(
            "object_event 1, 2, SPRITE_BLUE, STAY, NONE, TEXT_RIVAL, OPP_RIVAL1, 1", "red")),
            [(1, "TEXT_RIVAL", "OPP_RIVAL1", 1)])
        with self.assertRaises(DataError):
            list(object_battles("object_event 1, 2, 3", "red"))

    def test_cache_hash_and_path_validation(self):
        with tempfile.TemporaryDirectory() as root:
            source = Source(root, False)
            path = "constants/pokedex_constants.asm"
            raw = b"const_def 1\nconst DEX_BULBASAUR\n"
            destination = Path(root) / REVISION / path
            destination.parent.mkdir(parents=True)
            destination.write_bytes(raw)
            source.blobs[path] = hashlib.sha1(f"blob {len(raw)}\0".encode() + raw).hexdigest()
            self.assertEqual(source.get(path), raw.decode())
            destination.write_bytes(raw + b"; tampered\n")
            with self.assertRaisesRegex(DataError, "hash mismatch"):
                source.get(path)
            with self.assertRaisesRegex(DataError, "allowlisted"):
                source.get("../baserom.z64")

    def test_malformed_base_data(self):
        with self.assertRaises(DataError):
            base_stats("db DEX_BULBASAUR", {"DEX_BULBASAUR": 1}, {})


class CanonicalInventoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = Path(__file__).resolve().parents[1] / "build/story/data.json"
        if not path.exists():
            raise RuntimeError("Run python3 tools/story_data.py --fetch before canonical data tests")
        cls.data = json.loads(path.read_text())

    def test_inventory_counts_and_versions(self):
        self.assertEqual(self.data["revision"], REVISION)
        self.assertEqual(self.data["stored_parties"], 391)
        self.assertEqual(len(self.data["species"]), 151)
        self.assertEqual(len(self.data["unreferenced_parties"]), 39)
        self.assertEqual(self.data["encounters"]["red"], self.data["encounters"]["blue"])
        entries = self.data["encounters"]["red"]
        self.assertEqual(len(entries), 361)
        self.assertEqual(len({entry["id"] for entry in entries}), 361)
        self.assertEqual(sum(e["kind"] in ("trainer", "rival") for e in entries), 340)
        self.assertFalse(self.data["availability_audit_complete"])
        self.assertIn(["PROFOAK", 1], self.data["unreferenced_parties"])

    def test_boss_overrides_and_pp(self):
        by_class = {e.get("class"): e for e in self.data["encounters"]["red"]
                    if e["area"].endswith("Gym") or e.get("class") in ("LORELEI", "BRUNO", "AGATHA", "LANCE")}
        # Gen I move IDs: Bide 117; BubbleBeam 61; Thunderbolt 85; Mega Drain 72.
        for cls, slot, move in (("BROCK", 1, 117), ("MISTY", 1, 61),
                                ("LT_SURGE", 2, 85), ("ERIKA", 2, 72),
                                ("KOGA", 3, 92), ("SABRINA", 3, 149),
                                ("BLAINE", 3, 126), ("GIOVANNI", 4, 90),
                                ("LORELEI", 4, 59), ("BRUNO", 4, 90),
                                ("AGATHA", 4, 92), ("LANCE", 4, 112)):
            mon = by_class[cls]["teams"][0][slot]
            self.assertEqual(mon["moves"][2], move, cls)
            self.assertEqual(mon["pp"][2], self.data["pp"][move])
            self.assertEqual(mon["dvs"], 0x8889)
            self.assertEqual(mon["stat_exp"], [0] * 5)

    def test_rival_branches(self):
        rivals = [e for e in self.data["encounters"]["red"] if e["kind"] == "rival"]
        self.assertEqual(len(rivals), 8)
        for entry in rivals:
            self.assertEqual(len(entry["teams"]), 3)
            starters = [team[-1]["species"] for team in entry["teams"]]
            self.assertIn(starters, ([7, 1, 4], [8, 2, 5], [9, 3, 6]))
        champion = next(e for e in rivals if e["area"] == "ChampionsRoom")
        for team, move in zip(champion["teams"], (59, 72, 126)):
            self.assertEqual([mon["level"] for mon in team], [61, 59, 61, 61, 63, 65])
            self.assertEqual(team[0]["moves"][2], 143)
            self.assertEqual(team[5]["moves"][2], move)

    def test_rival_selector_and_ship_window_evidence(self):
        self.assertEqual(self.data["rival_branch_audit"]["red"],
                         self.data["rival_branch_audit"]["blue"])
        self.assertEqual(len(self.data["rival_branch_audit"]["red"]), 8)
        entries = {e["id"]: e for e in self.data["encounters"]["red"]}
        for audit in self.data["rival_branch_audit"]["red"]:
            entry = entries[audit["id"]]
            self.assertEqual(audit["party_indices"],
                             list(range(entry["party"], entry["party"] + 3)))
            self.assertEqual(audit["starter_constants"], [2, 3, 1])
        windows = self.data["event_retirement_audit"]["red"]
        self.assertEqual(windows, self.data["event_retirement_audit"]["blue"])
        self.assertEqual(len(windows), 1)
        self.assertEqual(len(windows[0]["retires"]), 17)
        self.assertIn("SSAnne2F/rival-ship", windows[0]["retires"])
        self.assertEqual(windows[0]["requires_event"], "EVENT_GOT_HM01")
        source = Source(Path(__file__).resolve().parents[1] / ".local/story-source", False)
        source.paths()
        scripts = {audit["source"]: source.get(audit["source"])
                   for audit in self.data["rival_branch_audit"]["red"]}
        scripts["scripts/OaksLab.asm"] = scripts["scripts/OaksLab.asm"].replace(
            "ld a, $1\n\tjr .done", "ld a, $2\n\tjr .done")
        with self.assertRaisesRegex(DataError, "team IDs"):
            rival_branch_audit(scripts, "red")
        scripts["scripts/VermilionDock.asm"] = source.get("scripts/VermilionDock.asm").replace(
            "CheckEventReuseHL EVENT_GOT_HM01", "CheckEventReuseHL EVENT_BEAT_SS_ANNE_RIVAL")
        with self.assertRaises(ValueError):
            ship_retirement(list(entries.values()), scripts, "red")

    def test_finite_static_schedule(self):
        entries = self.data["encounters"]["red"]
        wild = [e["teams"][0][0] for e in entries if e["kind"] == "static"]
        self.assertEqual(len(wild), 14)
        self.assertEqual(sum(mon["species"] == 100 and mon["level"] == 40 for mon in wild), 6)
        self.assertEqual(sum(mon["species"] == 101 and mon["level"] == 43 for mon in wild), 2)
        self.assertEqual(sum(mon["species"] == 143 and mon["level"] == 30 for mon in wild), 2)
        self.assertEqual(sum(mon["species"] == 150 and mon["level"] == 70 for mon in wild), 1)
        self.assertFalse(any(mon["species"] == 151 for mon in wild))
        self.assertEqual(sum(e["kind"] == "gift" for e in entries), 6)
        self.assertEqual(sum(e["kind"] == "ghost" for e in entries), 1)

    def test_reviewed_retirement_subset(self):
        red = self.data["retirement_audit"]["red"]
        self.assertEqual(red, self.data["retirement_audit"]["blue"])
        self.assertEqual(len(red), 11)
        retired = [identifier for rule in red for identifier in rule["retires"]]
        self.assertEqual(len(retired), 76)
        self.assertEqual(len(set(retired)), 76)
        entries = {e["id"]: e for e in self.data["encounters"]["red"]}
        for rule in red:
            self.assertIn(rule["after"], entries)
            self.assertNotIn(rule["after"], rule["retires"])
            self.assertTrue(all(identifier in entries for identifier in rule["retires"]))
            self.assertTrue(rule["line"] > 0)
        self.assertEqual(red[-2]["retires"], ["Route22/rival-early"])
        self.assertEqual(red[-2]["after"], red[0]["after"])
        self.assertEqual(len(red[-1]["retires"]), 30)
        self.assertNotIn("SilphCo7F/rival-silph", red[-1]["retires"])
        self.assertNotIn("SilphCo7F/gift-lapras", red[-1]["retires"])
        # Matching inventory alone cannot authorize a retirement policy.
        with self.assertRaisesRegex(DataError, "headers"):
            retirement_audit(list(entries.values()), {"scripts/PewterGym.asm": ""}, "red")

    def test_authenticated_tree_cache(self):
        root = Path(__file__).resolve().parents[1]
        original = json.loads((root / ".local/story-source" / REVISION / "tree.json").read_text())
        with tempfile.TemporaryDirectory() as cache:
            source = Source(cache, True)
            with patch("tools.story_data.urllib.request.urlopen",
                       return_value=io.BytesIO(json.dumps(original).encode())):
                fetched = source.paths()
            offline = Source(cache, False)
            self.assertEqual(fetched, offline.paths())
            self.assertEqual(source.blobs, offline.blobs)
            destination = Path(cache) / REVISION / "tree.json"
            path = "data/pokemon/base_stats/bulbasaur.asm"
            file = Path(cache) / REVISION / path
            with self.assertRaisesRegex(DataError, "Missing cached source"):
                offline.get(path)
            file.parent.mkdir(parents=True)
            factual = (root / ".local/story-source" / REVISION / path).read_text()
            self.assertIn("45,  49,  49,  45,  65", factual)
            file.write_text(factual.replace("45,  49,  49,  45,  65", "45,  50,  49,  45,  65"))
            entry = next(e for e in original["tree"] if e["path"] == path)
            raw = file.read_bytes()
            entry["sha"] = hashlib.sha1(f"blob {len(raw)}\0".encode() + raw).hexdigest()
            destination.write_text(json.dumps(original))
            with self.assertRaisesRegex(DataError, "digest mismatch"):
                offline.paths()
            self.assertEqual(offline.blobs, {})
            destination.unlink()
            with patch("tools.story_data.urllib.request.urlopen",
                       return_value=io.BytesIO(json.dumps(original).encode())):
                with self.assertRaisesRegex(DataError, "digest mismatch"):
                    source.paths()
            self.assertFalse(destination.exists())
            original["tree"].remove(entry)
            destination.write_text(json.dumps(original))
            with self.assertRaisesRegex(DataError, "digest mismatch"):
                offline.paths()
            original["sha"] = "0" * 40
            destination.write_text(json.dumps(original))
            with self.assertRaisesRegex(DataError, "Wrong source tree"):
                offline.paths()
            destination.unlink()
            with self.assertRaisesRegex(DataError, "Missing tree cache"):
                offline.paths()
            with patch("tools.story_data.urllib.request.urlopen",
                       return_value=io.BytesIO(json.dumps(original).encode())):
                with self.assertRaisesRegex(DataError, "Wrong source tree"):
                    source.paths()
            self.assertFalse(destination.exists())

    def test_candidate_full_traversals(self):
        for version in ("red", "blue"):
            for branch in range(3):
                for fossil in range(2):
                    graph = candidate(self.data, version, branch, fossil)
                    order, retired = traverse(graph)
                    self.assertFalse(graph["reviewed"])
                    self.assertEqual(set(order), set(graph["nodes"]))
                    self.assertFalse(retired)
                    self.assertEqual(len(set(order) & graph["encounters"].keys()), 361)
                    rivals = [e for e in graph["encounters"].values() if e["kind"] == "rival"]
                    self.assertEqual(len(rivals), 8)
                    for entry in rivals:
                        variant = graph["nodes"][entry["id"]]["variant"]
                        self.assertEqual(variant, branch)
                        self.assertIn(entry["teams"][variant][-1]["species"],
                                      ((7, 8, 9), (1, 2, 3), (4, 5, 6))[branch])
                    self.assertEqual(graph["nodes"]["travel:fossil"]["variant"], fossil)
                    species = graph["encounters"][graph["fossil_offer"]]["teams"][fossil][0]["species"]
                    self.assertEqual(species, (138, 140)[fossil])
                    self.assertEqual(graph["nodes"]["travel:fossil"]["item"],
                                     ("HELIX_FOSSIL", "DOME_FOSSIL")[fossil])
                    self.assertEqual(graph["nodes"][graph["fossil_offer"]]["variant"], fossil)
                    self.assertLess(order.index("travel:scope"),
                                    order.index("PokemonTower6F/ghost"))
                    self.assertLess(order.index("ChampionsRoom/rival-champion"),
                                    next(i for i, e in enumerate(order) if e.startswith("CeruleanCave")))
        with self.assertRaises(DataError):
            candidate(self.data, "yellow")
        with self.assertRaises(DataError):
            candidate(self.data, "red", 3)

    def test_access_fact_evidence(self):
        facts = self.data["access_fact_audit"]["red"]
        self.assertEqual(facts, self.data["access_fact_audit"]["blue"])
        self.assertEqual(len(facts), 17)
        self.assertEqual(len({fact["id"] for fact in facts}), 17)
        self.assertTrue(all(fact["line"] <= fact["end_line"] for fact in facts))
        source = Source(Path(__file__).resolve().parents[1] / ".local/story-source", False)
        source.paths()
        original = source.get
        def altered(path):
            return original(path).replace("bit BIT_CASCADEBADGE, a", "bit BIT_SOULBADGE, a")
        with patch.object(source, "get", side_effect=altered):
            with self.assertRaisesRegex(DataError, "cut_badge"):
                access_fact_audit(source, "red")
        def switched_fossil(path):
            return original(path).replace("ld b, OMANYTE", "ld b, KABUTO")
        with patch.object(source, "get", side_effect=switched_fossil):
            with self.assertRaisesRegex(DataError, "fossil_species_mapping"):
                access_fact_audit(source, "red")

    def test_candidate_optional_routes_and_retirement(self):
        for route in ("east", "west"):
            graph = candidate(self.data, "red")
            if route == "west":
                graph["nodes"]["travel:fuchsia"]["clauses"].reverse()
            completed, retired = set(), set()
            def reach(identifier):
                if identifier in completed:
                    return
                self.assertNotIn(identifier, retired)
                clause = graph["nodes"][identifier]["clauses"][0]
                for dependency in clause:
                    reach(dependency)
                advance(graph, completed, retired, identifier, destination_warp=1)
            reach("travel:fuchsia")
            self.assertIn("Route12/snorlax" if route == "east" else "Route16/snorlax", completed)
            self.assertNotIn("Route16/snorlax" if route == "east" else "Route12/snorlax", completed)
            self.assertTrue(available(graph, completed, retired,
                                      "Route19/TEXT_ROUTE19_COOLTRAINER_M1"))
            self.assertFalse(available(graph, completed, retired,
                                       "Route19/TEXT_ROUTE19_SWIMMER1"))
            # Reaching one coast does not erase the other coast's finite encounters.
            reach("Route16/snorlax" if route == "east" else "Route12/snorlax")
            reach("ChampionsRoom/rival-champion")
            reach("travel:ship-departure")
            self.assertIn("Route22/rival-early", retired)
            self.assertIn("SSAnne1FRooms/TEXT_SSANNE1FROOMS_YOUNGSTER", retired)
            self.assertFalse(available(graph, completed, retired, "Route22/rival-early"))
            self.assertNotIn("SilphCo7F/gift-lapras", retired)
            self.assertFalse(set(graph["encounters"]) <= completed)
            with self.assertRaises(DataError):
                advance(graph, completed, retired, "Route22/rival-early")

    def test_ship_requires_captain_receipt_and_exit_warp(self):
        graph = candidate(self.data, "red")
        completed, retired = {"SSAnne2F/rival-ship", "travel:bill"}, set()
        departure = "travel:ship-departure"
        receipt = "event:hm01-received"
        self.assertFalse(available(graph, completed, retired, departure, destination_warp=1))
        self.assertTrue(available(graph, completed, retired, receipt))
        self.assertEqual(graph["nodes"][receipt]["sets_event"], "EVENT_GOT_HM01")
        advance(graph, completed, retired, receipt)
        self.assertFalse(available(graph, completed, retired, departure))
        self.assertFalse(available(graph, completed, retired, departure, destination_warp=2))
        self.assertTrue(available(graph, completed, retired,
                                  "SSAnne1FRooms/TEXT_SSANNE1FROOMS_YOUNGSTER"))
        self.assertTrue(available(graph, completed, retired, departure, destination_warp=1))
        # Receiving Cut is distinct from permission to use it: no Misty badge is needed to leave.
        self.assertFalse(available(graph, completed, retired, "field:cut"))
        with self.assertRaises(DataError):
            advance(graph, completed, retired, departure, destination_warp=2)
        self.assertFalse(retired)
        advance(graph, completed, retired, departure, destination_warp=1)
        self.assertEqual(len(retired), 16)
        self.assertFalse(available(graph, completed, retired,
                                   "SSAnne1FRooms/TEXT_SSANNE1FROOMS_YOUNGSTER"))
        self.assertNotIn("SSAnne2F/rival-ship", retired)

    def test_finite_static_and_gift_permission_clauses(self):
        graph = candidate(self.data, "red")
        for entry in graph["encounters"].values():
            if entry["kind"] not in ("static", "ghost", "gift"):
                continue
            clauses = graph["nodes"][entry["id"]]["clauses"]
            for clause in clauses:
                self.assertTrue(available(graph, set(clause), set(), entry["id"]))
            if entry["area"] == "PowerPlant":
                self.assertEqual(clauses, [["field:surf"]])
            elif entry["area"] == "SeafoamIslandsB4F":
                self.assertEqual(clauses, [["field:surf", "field:strength"]])
            elif entry["area"] == "CeruleanCaveB1F":
                self.assertEqual(clauses, [["ChampionsRoom/rival-champion", "field:surf"]])
            elif entry["kind"] == "ghost":
                self.assertTrue(all("travel:scope" in clause for clause in clauses))
            elif entry["id"].endswith("/snorlax"):
                self.assertTrue(all("travel:flute" in clause for clause in clauses))
            elif entry["id"].endswith("/gift-dojo"):
                self.assertTrue(all("FightingDojo/TEXT_FIGHTINGDOJO_KARATE_MASTER"
                                    in clause for clause in clauses))
            elif entry["id"].endswith("/gift-lapras"):
                self.assertTrue(all("SilphCo7F/rival-silph" in clause for clause in clauses))
            elif entry["id"].endswith("/gift-fossil"):
                self.assertTrue(all("travel:fossil" in clause for clause in clauses))
            elif entry["id"].endswith("/gift-amber"):
                self.assertTrue(all("travel:amber" in clause for clause in clauses))
            self.assertFalse(available(graph, set(), set(), entry["id"]))
        bridge1 = "Route24/TEXT_ROUTE24_YOUNGSTER2"
        bridge2 = "Route24/TEXT_ROUTE24_COOLTRAINER_F2"
        self.assertFalse(available(graph, {"CeruleanCity/rival-bridge"}, set(), bridge2))
        self.assertTrue(available(graph, {"CeruleanCity/rival-bridge", bridge1}, set(), bridge2))
        self.assertFalse(available(graph, {"CeruleanCity/rival-bridge"}, set(),
                                   "Route25/TEXT_ROUTE25_HIKER1"))

    def test_healing_identity_access_and_retirement(self):
        self.assertEqual(self.data["healing_fact_audit"]["red"],
                         self.data["healing_fact_audit"]["blue"])
        self.assertEqual(len(self.data["healing_fact_audit"]["red"]), 15)
        graph = candidate(self.data, "red")
        self.assertFalse(healing_available(graph, set(), "heal:SilphCo9F", "standard"))
        completed = {"travel:silph", "travel:card-key"}
        self.assertTrue(healing_available(graph, completed, "heal:SilphCo9F", "standard"))
        self.assertFalse(healing_available(graph, completed, "heal:SilphCo9F", "brutal"))
        self.assertFalse(healing_available(graph, completed, "heal:SilphCo9F", "limited",
                                           {"heal:SilphCo9F"}))
        completed.add("SilphCo11F/TEXT_SILPHCO11F_GIOVANNI")
        self.assertFalse(healing_available(graph, completed, "heal:SilphCo9F", "standard"))
        order, _ = traverse(graph)
        completed = set(order)
        self.assertTrue(healing_available(graph, completed, "heal:IndigoPlateauLobby", "standard"))
        completed.remove("ChampionsRoom/rival-champion")
        self.assertFalse(healing_available(graph, completed, "heal:IndigoPlateauLobby", "standard"))
        self.assertFalse(healing_available(graph, completed, "heal:RedsHouse1F", "standard"))
        with self.assertRaises(DataError):
            healing_available(graph, set(), "heal:SSAnne1FRooms", "standard")

    def test_construct_every_species_level(self):
        for mon in self.data["species"]:
            for level in range(1, 101):
                result = construct_moves(mon, level)
                self.assertTrue(result[0])
                self.assertEqual(len(result), 4)
                self.assertEqual(len(set(m for m in result if m)), sum(bool(m) for m in result))
                self.assertTrue(all(0 <= move <= 165 for move in result))


if __name__ == "__main__":
    unittest.main()
