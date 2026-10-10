import tempfile
import unittest
from pathlib import Path

from tools.rltest.frontend import HarnessError, Symbols, _png


class HelperTests(unittest.TestCase):
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
