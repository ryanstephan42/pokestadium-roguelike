import copy
import unittest

from tools.rltest.frontend import HarnessError
from tools.rltest.story_sequential import (
    ACTIVE, BATTLE, REOPEN, SequentialBudget, SequentialLedger, decode_ledger, capture,
)


class SequentialObserverTests(unittest.TestCase):
    def setUp(self):
        self.party = [dict(species=4, level=12, exp=1108, growth=3,
                           stat_exp=[0] * 5, moves=[10, 45, 0, 52], max_pp=[35, 40, 0, 25],
                           pp=[35, 40, 0, 25],
                           base_stats=[39, 52, 43, 65, 50], current_stats=[30, 19, 17, 20, 17],
                           max_hp=30, hp=30, fainted=0, status=0, dvs=0x4528)]
        self.enemy = [dict(level=12, yield_=86, base=[40, 80, 100, 20, 30]),
                      dict(level=14, yield_=108, base=[35, 45, 160, 70, 30])]
        for mon in self.enemy:
            mon["yield"] = mon.pop("yield_")
        self.identity = dict(operation=77, encounter=58, variant=0, party_count=1, enemy_count=2)
        self.ledger = dict(**self.identity, bound=1, count=0, next=0,
                           party_ids=[4, 0, 0, 0, 0, 0], events=[])

    def row(self, stage, ledger=None, party=None):
        return dict(stage=stage, ledger=copy.deepcopy(ledger or self.ledger),
                    request=copy.deepcopy(self.identity),
                    party=copy.deepcopy(party or self.party), phase=1,
                    result_ready=0, outcome=0, ui=None)

    def observer(self):
        return SequentialLedger(self.party, self.enemy)

    def test_capture_does_not_award_and_preserves_order(self):
        observer = self.observer()
        row = self.row(BATTLE)
        observer.accept(row)
        row["ledger"]["events"] = [dict(enemy=1, participants=1, recipients=1)]
        row["ledger"]["count"] = 1
        observer.accept(row)
        row["party"][0]["exp"] += 1
        with self.assertRaisesRegex(HarnessError, "mutated"):
            observer.accept(row)

    def completed(self, observer):
        row = self.row(BATTLE)
        row["ledger"]["events"] = [dict(enemy=1, participants=1, recipients=1),
                                   dict(enemy=0, participants=1, recipients=0)]
        row["ledger"]["count"] = 2
        observer.accept(row)
        row["stage"] = REOPEN; row["result_ready"] = 1; row["outcome"] = 1
        carry = copy.deepcopy(self.party[0]); carry.update(hp=0, fainted=1, status=3)
        row["carry"] = [(0, carry)]
        observer.accept(row)
        return row

    def test_postbattle_fainted_recipient_earns_without_revival(self):
        observer = self.observer()
        row = self.completed(observer)
        row["stage"] = ACTIVE
        row["party"] = observer.expected(2)
        row["party"][0].update(hp=0, fainted=1, status=3)
        row["ui"] = dict(ledger=dict(bound=0))
        observer.accept(row)
        self.assertEqual(row["party"][0]["exp"], 1108 + 324)
        self.assertEqual(row["party"][0]["stat_exp"], self.enemy[1]["base"])
        row["party"][0]["hp"] = 1
        with self.assertRaisesRegex(HarnessError, "revived"):
            observer.accept(row)

    def test_consumed_resident_result_survives_unobserved_reopen_frame(self):
        observer = self.observer()
        row = self.row(BATTLE)
        row["ledger"]["events"] = [dict(enemy=1, participants=1, recipients=1),
                                   dict(enemy=0, participants=1, recipients=0)]
        row["ledger"]["count"] = 2
        observer.accept(row)
        row.update(stage=ACTIVE, result_ready=0, result_consumed=True, outcome=1,
                   party=observer.expected(2), ui=dict(ledger=dict(bound=0)))
        row["party"][0].update(hp=0, fainted=1, status=3)
        carry = copy.deepcopy(self.party[0])
        carry.update(hp=0, fainted=1, status=3)
        row["carry"] = [(0, carry)]
        observer.accept(row)
        self.assertEqual(observer.result, 1)
        self.assertEqual(observer.applied, 2)
        wrong = copy.deepcopy(row)
        wrong["result_ready"] = 1
        with self.assertRaises(HarnessError):
            observer.accept(wrong)

    def test_applied_marker_during_postbattle_pak_io_keeps_result_identity(self):
        observer = self.observer()
        row = self.completed(observer)
        row.update(result_ready=0, resume_started=True)
        observer.accept(row)
        self.assertEqual(observer.result, 1)
        self.assertEqual(observer.applied, 0)
        wrong = copy.deepcopy(row)
        wrong["resume_started"] = False
        with self.assertRaisesRegex(HarnessError, "lacks completed result"):
            observer.accept(wrong)
        wrong = copy.deepcopy(row)
        wrong["outcome"] = 0
        with self.assertRaisesRegex(HarnessError, "completed result changed"):
            observer.accept(wrong)

    def test_consumed_capture_requires_launched_operation_and_inactive_transaction(self):
        control = bytearray(300)
        ui_address = 0x80404000
        control[:4] = ui_address.to_bytes(4, "big")
        control[4:8] = (77).to_bytes(4, "big")
        control[25] = 1
        control[150] = 1
        control[151] = 1
        control[162] = 4
        control[288:292] = (1).to_bytes(4, "big")
        run = bytearray(ACTIVE.to_bytes(4, "big") + bytes(4) + (77).to_bytes(4, "big"))
        state = bytearray(80)
        state[1] = 1
        state[12] = 4
        abi = dict(pointer_size=4, mon_size=68, event_size=3,
                   control=dict(size=300, ui_pointer=0, sequential_request=4,
                                sequential_state=24, sequential_ledger=104,
                                sequential_result=150, sequential_result_ready=284,
                                sequential_outcome=288, sequential_active=292),
                   state=dict(size=80, party=12, party_count=1, phase=2),
                   ui=dict(state=0, postbattle_ledger=80, screen=124, pending_save=128),
                   request=dict(size=20, operation_id=0, encounter=4, variant=6,
                                party_count=7, enemy_count=8),
                   result=dict(size=80, player_count=1, player_index=3, player=12, outcome=0),
                   ledger=dict(size=44, events=0, operation_id=20, encounter=24,
                               variant=26, party_ids=27, count=39, next=40,
                               enemy_count=41, party_count=42, bound=43),
                   run=dict(size=12, stage=0, error=4, launched=8))
        class Emu:
            frame = 5
            def read_u32(self, symbol):
                return 0x80001000
            def read(self, address, size):
                if address == "gScCampaignRun":
                    return run
                if address == 0x80001000:
                    return control
                if address == ui_address:
                    return state
                return bytes(size)
        row = capture(Emu(), abi, lambda raw: dict(species=raw[0]))
        self.assertTrue(row["result_consumed"])
        self.assertEqual(row["carry"], [(0, dict(species=4))])
        run[:4] = REOPEN.to_bytes(4, "big")
        self.assertTrue(capture(Emu(), abi, lambda raw: dict(species=raw[0]))["resume_started"])
        control[292:296] = (1).to_bytes(4, "big")
        self.assertFalse(capture(Emu(), abi, lambda raw: dict(species=raw[0]))["resume_started"])
        control[292:296] = bytes(4)
        run[:4] = ACTIVE.to_bytes(4, "big")
        for mutation in ("operation", "active"):
            if mutation == "operation":
                run[8:12] = (78).to_bytes(4, "big")
            else:
                run[8:12] = (77).to_bytes(4, "big")
                control[292:296] = (1).to_bytes(4, "big")
            row = capture(Emu(), abi, lambda raw: dict(species=raw[0]))
            self.assertFalse(row["result_consumed"])
            self.assertNotIn("carry", row)

    def test_living_carry_preserves_damage_and_pp_through_level_gain(self):
        observer = self.observer()
        row = self.completed(observer)
        carry = copy.deepcopy(self.party[0])
        carry.update(hp=8, pp=[34, 40, 0, 23])
        observer.carry = {0: carry}
        row.update(stage=ACTIVE, party=observer.expected(2),
                   ui=dict(ledger=dict(bound=0)))
        observer.accept(row)
        self.assertEqual(row["party"][0]["hp"],
                         8 + row["party"][0]["max_hp"] - self.party[0]["max_hp"])
        self.assertEqual(row["party"][0]["pp"], carry["pp"])
        for key in ("hp", "pp", "current_stats", "max_hp"):
            wrong = copy.deepcopy(row)
            if isinstance(wrong["party"][0][key], list):
                wrong["party"][0][key][0] += 1
            else:
                wrong["party"][0][key] += 1
            with self.assertRaisesRegex(HarnessError, "carry mismatch"):
                observer.accept(wrong)

    def test_result_carry_cannot_change_original_progression(self):
        for key in ("level", "exp", "moves", "stat_exp"):
            observer = self.observer()
            row = self.completed(observer)
            if isinstance(row["carry"][0][1][key], list):
                row["carry"][0][1][key][0] += 1
            else:
                row["carry"][0][1][key] += 1
            with self.assertRaisesRegex(HarnessError, "mapping refused"):
                observer.accept(row)

    def test_rejects_coherent_retarget_and_changed_party(self):
        for key in ("operation", "encounter", "variant", "party_count", "enemy_count"):
            observer = self.observer()
            observer.accept(self.row(BATTLE))
            row = self.row(BATTLE)
            row["ledger"][key] += 1; row["request"][key] += 1
            with self.assertRaises(HarnessError):
                observer.accept(row)
        row = self.row(BATTLE); row["ledger"]["party_ids"][0] = 7
        with self.assertRaises(HarnessError):
            self.observer().accept(row)

    def test_rejects_event_reorder_and_cursor_reverse(self):
        observer = self.observer()
        row = self.completed(observer)
        row["ledger"]["events"].reverse()
        with self.assertRaisesRegex(HarnessError, "order"):
            observer.accept(row)
        row = self.completed(self.observer())
        observer = self.observer()
        self.completed(observer)
        row["stage"] = ACTIVE; row["ui"] = dict(ledger=copy.deepcopy(row["ledger"]))
        row["ui"]["ledger"]["next"] = 1
        row["party"] = observer.expected(1)
        row["party"][0].update(hp=0, fainted=1, status=3)
        observer.accept(row)
        row["ui"]["ledger"]["next"] = 0
        with self.assertRaisesRegex(HarnessError, "cursor"):
            observer.accept(row)

    def test_loss_discards_full_party_progression_and_carry(self):
        observer = self.observer()
        row = self.completed(observer)
        observer.result = 0
        row["stage"] = ACTIVE; row["phase"] = 4
        row["ui"] = dict(ledger=dict(bound=0))
        row["party"] = copy.deepcopy(self.party)
        observer.accept(row)
        for key in ("exp", "hp", "status"):
            wrong = copy.deepcopy(row); wrong["party"][0][key] += 1
            with self.assertRaisesRegex(HarnessError, "loss"):
                observer.accept(wrong)

    def test_one_shared_budget_no_reset_or_learning_exclusion(self):
        now = [10]
        budget = SequentialBudget(100, lambda: now[0])
        self.assertEqual(budget.check(18100), 18000)
        with self.assertRaises(HarnessError):
            budget.check(18101)
        with self.assertRaises(HarnessError):
            budget.check(0)
        now[0] = 190
        with self.assertRaises(HarnessError):
            budget.check(18100)
        self.assertFalse(hasattr(budget, "exclude"))

    def test_invalid_raw_ledger_refuses_before_semantic_replay(self):
        layout = dict(events=0, operation_id=20, encounter=24, variant=26,
                      party_ids=27, count=39, next=40, enemy_count=41,
                      party_count=42, bound=43)
        raw = bytearray(44)
        raw[20:24] = (77).to_bytes(4, "big")
        raw[24:26] = (58).to_bytes(2, "big")
        raw[27] = 4
        raw[39:44] = bytes([2, 0, 2, 1, 1])
        raw[:6] = bytes([1, 1, 1, 1, 1, 1])
        with self.assertRaisesRegex(HarnessError, "duplicate"):
            decode_ledger(raw, layout, 3)

    def test_battle_capture_never_dereferences_destroyed_ui(self):
        control = bytearray(300)
        control[:4] = (0xDEADBEEF).to_bytes(4, "big")
        control[24 + 1] = 1
        control[24 + 2] = 1
        run = BATTLE.to_bytes(4, "big") + bytes(8)
        abi = dict(pointer_size=4, mon_size=68, event_size=3,
                   control=dict(size=300, ui_pointer=0, sequential_request=4,
                                sequential_state=24, sequential_ledger=104,
                                sequential_result=150, sequential_result_ready=284,
                                sequential_outcome=288),
                   state=dict(size=80, party=12, party_count=1, phase=2),
                   request=dict(size=20, operation_id=0, encounter=4, variant=6,
                                party_count=7, enemy_count=8),
                   ledger=dict(size=44, events=0, operation_id=20, encounter=24,
                               variant=26, party_ids=27, count=39, next=40,
                               enemy_count=41, party_count=42, bound=43),
                   run=dict(size=12, stage=0, error=4))

        class Emu:
            frame = 5

            def read_u32(self, symbol):
                assert symbol == "gScCampaignObserverControl"
                return 0x80001000

            def read(self, address, size):
                if address == "gScCampaignRun":
                    assert size == len(run)
                    return run
                assert address == 0x80001000 and size == len(control)
                return control

        row = capture(Emu(), abi, lambda raw: dict(species=raw[0]))
        self.assertEqual(row["stage"], BATTLE)
        self.assertIsNone(row["ui"])
        self.assertEqual(len(row["party"]), 1)


if __name__ == "__main__":
    unittest.main()
