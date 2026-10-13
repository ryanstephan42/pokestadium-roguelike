import copy
import unittest
from unittest.mock import patch

from tools.story_data import DataError, Source, audit
from tools.story_graph import reviewed_graph, REVIEWED_DIGEST
from tools.story_stones import moon_audit, extend_graph


class MoonSourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = Source(".local/story-source", False)
        cls.data = audit(cls.source)
        cls.base = reviewed_graph(cls.data)
        cls.facts = moon_audit(cls.source)

    def test_base_identity_and_extension(self):
        base_ids = list(self.base["nodes"])
        graph, identity, reviewed = extend_graph(self.base, self.facts)
        self.assertEqual(len(base_ids), 383)
        self.assertEqual(list(graph["nodes"])[:383], base_ids)
        self.assertEqual(len(graph["nodes"]), 388)
        self.assertNotEqual(identity, REVIEWED_DIGEST)
        self.assertEqual(len(self.facts["pickups"]), 5)
        for pickup in self.facts["pickups"]:
            self.assertEqual(graph["nodes"][pickup["id"]]["clauses"], [[pickup["gate"]]])
        with patch("tools.story_stones.MOON_REVIEWED_DIGEST", identity):
            self.assertTrue(extend_graph(self.base, self.facts)[2])
            bad = copy.deepcopy(self.facts)
            bad["pickups"][0]["gate"] = "travel:moon-exit"
            with self.assertRaisesRegex(DataError, "accepted review"):
                extend_graph(self.base, bad)

    def test_source_tampering_fails_closed(self):
        original = self.source.get
        def changed(path):
            result = original(path)
            if path == "data/maps/objects/Route2.asm":
                return result.replace("object_event 13, 54", "object_event 13, 53")
            return result
        with patch.object(self.source, "get", side_effect=changed):
            with self.assertRaisesRegex(DataError, "Changed Moon Stone pickup"):
                moon_audit(self.source)
        original_read = self.source.read
        def changed_map(path):
            result = original_read(path)
            if path == "maps/Route2.blk":
                result = bytearray(result)
                result[11 * 10 + 7] = 0
                return bytes(result)
            return result
        with patch.object(self.source, "read", side_effect=changed_map):
            with self.assertRaisesRegex(DataError, "Cut barriers"):
                moon_audit(self.source)


if __name__ == "__main__":
    unittest.main()
