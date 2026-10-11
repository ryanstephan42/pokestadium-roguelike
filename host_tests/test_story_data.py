import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from tools.story_data import (
    DataError, REVISION, Source, base_stats, clean, construct_moves,
    constants, object_battles, trainer_key,
    retirement_audit,
)


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
        self.assertEqual(len(red), 10)
        retired = [identifier for rule in red for identifier in rule["retires"]]
        self.assertEqual(len(retired), 46)
        self.assertEqual(len(set(retired)), 46)
        entries = {e["id"]: e for e in self.data["encounters"]["red"]}
        for rule in red:
            self.assertIn(rule["after"], entries)
            self.assertNotIn(rule["after"], rule["retires"])
            self.assertTrue(all(identifier in entries for identifier in rule["retires"]))
            self.assertTrue(rule["line"] > 0)
        self.assertEqual(red[-1]["retires"], ["Route22/rival-early"])
        self.assertEqual(red[-1]["after"], red[0]["after"])
        # Matching inventory alone cannot authorize a retirement policy.
        with self.assertRaisesRegex(DataError, "headers"):
            retirement_audit(list(entries.values()), {"scripts/PewterGym.asm": ""}, "red")

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
