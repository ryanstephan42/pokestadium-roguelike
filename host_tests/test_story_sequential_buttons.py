import unittest
from types import SimpleNamespace
from tools.rltest.frontend import HarnessError
from tools.rltest.story_sequential_buttons import (
    choose_move, choose_party, action_menu, press_verified, read_menu, accepted)


class SequentialButtonTests(unittest.TestCase):
    def test_source_menu_reader_and_acknowledgement_are_read_only(self):
        values = {"gScCampaignBattle": 1, "D_84390010": 0x80010000,
                  0x80001000: 4, 0x80010720: 0x80020000, 0x80020001: 1,
                  0x80020008: 0x80030000, 0x80030001: 0, 0x80030002: 6,
                  0x80010658: 1, 0x80010664: 1,
                  0x800106DE: 0x31}
        class Reader:
            def read_u32(self, address):
                return values.get(address, 0)
            read_u8 = read_u32
            read_u16 = read_u32
            def read(self, address, size):
                return bytes([52] * size if address == 0x800106AB else [20] * size)
        menu = read_menu(Reader(), 0x80001000)
        self.assertEqual(menu["disabled"], 3)
        self.assertEqual(menu["moves"], [52] * 4)
        self.assertEqual(menu["hp"], [0] * 6)
        self.assertFalse(accepted(menu, "move", 3))
        self.assertTrue(accepted({**menu, "mode": 3, "selected_move": 3}, "move", 3))
        values[0x80001000] = 5
        with self.assertRaises(HarnessError):
            read_menu(Reader(), 0x80001000)

    def test_verified_edge_and_release_use_counted_steps_only(self):
        emu = SimpleNamespace(held=[set(), set()], analog=[[0] * 4, [0] * 4])
        calls = []
        def step(frames):
            calls.append((frames, set(emu.held[0])))
        with self.assertRaises(HarnessError):
            press_verified(emu, "C_LEFT", {}, step)
        self.assertFalse(calls)
        press_verified(emu, "N64_A", {"N64_A": {"kind": "joypad", "id": 0}}, step)
        self.assertEqual(calls, [(1, {0}), (1, set())])
        emu.analog[0][2] = 1
        with self.assertRaises(HarnessError):
            press_verified(emu, "N64_A", {"N64_A": {"kind": "joypad", "id": 0}}, step)

    def test_actual_four_move_button_order_and_guards(self):
        menu = dict(action=1, mode=1, locked=False, moves=[10, 45, 0, 52],
                    pp=[35, 40, 0, 25], disabled=-1)
        self.assertEqual(choose_move(menu, 3), "C_LEFT")
        self.assertEqual(choose_move(menu, 1), "C_RIGHT")
        for key, value in (("action", 0), ("mode", 0), ("locked", True), ("disabled", 3)):
            with self.assertRaises(HarnessError):
                choose_move({**menu, key: value}, 3)
        with self.assertRaises(HarnessError):
            choose_move(menu, 2)
        with self.assertRaises(HarnessError):
            choose_move({**menu, "pp": [0] * 4}, 3)

    def test_six_party_is_not_four_move_order(self):
        menu = dict(count=6, mode=2, locked=False, hp=[30] * 6, active=0)
        self.assertEqual(choose_party(menu, 1), "C_LEFT")
        self.assertEqual(choose_party(menu, 5), "N64_A")
        with self.assertRaises(HarnessError):
            choose_party(menu, 0)
        with self.assertRaises(HarnessError):
            choose_party({**menu, "hp": [0] * 6}, 1)
        self.assertEqual(choose_party({**menu, "count": 3}, 1), "C_RIGHT")
        self.assertEqual(action_menu("fight"), "N64_A")
        self.assertEqual(action_menu("switch"), "N64_B")
        with self.assertRaises(HarnessError):
            action_menu("forfeit")


if __name__ == "__main__":
    unittest.main()
