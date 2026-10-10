import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock, call, patch

from tools.rltest.frontend import HarnessError, Symbols, _png


class HelperTests(unittest.TestCase):
    def test_node_fixture_enters_menu_before_native_probe(self):
        from tools.rltest import test_nodes

        emu = Mock()

        def create_fixture(frontend, command, *, create):
            self.assertIs(frontend, emu)
            self.assertEqual((command, create), (4, 1))
            self.assertEqual(emu.mock_calls[-3:], [
                call.wait_state(4, frames=600),
                call.press("N64_A", frames=6, after=120),
                call.step(120),
            ])
            return (0, 0)

        with patch.object(test_nodes, "reenter_roguelike"), \
                patch.object(test_nodes, "press"), \
                patch.object(test_nodes, "wait_scene"), \
                patch("tools.rltest.test_play_run.wait_scene"), \
                patch.object(test_nodes, "native_command", side_effect=create_fixture):
            test_nodes.boot(emu)
        self.assertEqual(emu.mock_calls[-4:], [
            call.hold("L"),
            call.press("N64_A", frames=6, after=120),
            call.wait_state(0x30, frames=600),
            call.release("L"),
        ])

    def test_symbol_lookup_and_nearest(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "test.map"
            path.write_text("                0x80000010 first_symbol\n"
                            "                0x80000040 next_symbol\n")
            symbols = Symbols(path)
            self.assertEqual(symbols.address("first_symbol"), 0x80000010)
            self.assertEqual(symbols.nearest(0x8000001C), "first_symbol+0xc")
            with self.assertRaises(HarnessError):
                symbols.address("missing")

    def test_png_rgb565_and_xrgb8888(self):
        rgb565 = _png(b"\x00\xf8", 1, 1, 2, 2)
        self.assertTrue(rgb565.startswith(b"\x89PNG\r\n\x1a\n"))
        xrgb = _png(b"\x00\x00\xff\x00", 1, 1, 4, 1)
        self.assertTrue(xrgb.startswith(b"\x89PNG\r\n\x1a\n"))

    def test_png_rejects_invalid_geometry(self):
        with self.assertRaises(HarnessError):
            _png(b"", 2, 1, 2, 0)


if __name__ == "__main__":
    unittest.main()
