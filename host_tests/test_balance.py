import csv
import io
from pathlib import Path
import subprocess
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]


class BalanceTests(unittest.TestCase):
    def test_csv_shape_and_determinism(self):
        if not (ROOT / "baseroms/us/baserom.z64").exists():
            self.skipTest("local baserom not installed")
        command = [sys.executable, str(ROOT / "tools/rl_balance.py"),
                   "--runs", "32", "--seed", "4294967280"]
        first = subprocess.check_output(command, text=True)
        self.assertEqual(first, subprocess.check_output(command, text=True))
        self.assertIn("not native", first.splitlines()[0])
        rows = list(csv.DictReader(io.StringIO("\n".join(first.splitlines()[1:]))))
        self.assertEqual(len(rows), 44)
        for index, row in enumerate(rows):
            self.assertEqual(int(row["ascension"]), index // 4)
            self.assertEqual(int(row["act"]), index % 4)
            self.assertEqual(int(row["runs"]), 32)
            self.assertLessEqual(int(row["run_wins"]), 32)
            self.assertLessEqual(int(row["reached"]), 32)
            self.assertTrue(0 <= float(row["battle_win_pct"]) <= 100)
            self.assertTrue(0 <= float(row["run_win_pct"]) <= 100)


if __name__ == "__main__":
    unittest.main()
