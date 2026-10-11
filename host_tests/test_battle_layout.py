"""Compare harness ctypes offsets with the compiled native C declarations."""

import ctypes
import json
from pathlib import Path
import subprocess
import unittest

from tools.rltest.test_custom_battle import BattleRules, Mon, PendingBattle
from tools.rltest.test_play_run import Reward


class BattleLayoutTests(unittest.TestCase):
    def test_native_request_layout(self):
        root = Path(__file__).resolve().parents[1]
        actual = json.loads(subprocess.check_output(
            [str(root / "build/host_tests/play-ui-adapter"), "--layout"],
            text=True,
        ))
        self.assertEqual(actual, {
            "mon_size": ctypes.sizeof(Mon),
            "reward_size": ctypes.sizeof(Reward),
            "reward_mon": Reward.mon.offset,
            "rules_size": ctypes.sizeof(BattleRules),
            "relics": BattleRules.relics.offset,
            "pending_size": ctypes.sizeof(PendingBattle),
            "rules": PendingBattle.rules.offset,
            "player_slots": PendingBattle.player_slots.offset,
            "trainer": PendingBattle.trainer.offset,
            "trainer_count": PendingBattle.trainer_count.offset,
        })
        self.assertEqual(ctypes.sizeof(BattleRules), 12)
        self.assertEqual(BattleRules.relics.offset, 8)
        for relics in (0, 1 << 19):
            request = PendingBattle()
            request.rules.relics = relics
            request.player_slots[:] = [2, 0, 1]
            encoded = bytes(request)
            offset = PendingBattle.player_slots.offset
            self.assertEqual(encoded[offset:offset + 3], bytes([2, 0, 1]))
            self.assertEqual(encoded[PendingBattle.rules.offset + 8:
                                     PendingBattle.rules.offset + 12],
                             relics.to_bytes(4, "big"))


if __name__ == "__main__":
    unittest.main()
