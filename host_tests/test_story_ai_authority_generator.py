import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
GENERATOR = ROOT / "host_tests/generate_story_ai_authority.py"
POLICY = GENERATOR.with_name("story_ai_imports.json")


class AuthorityGeneratorTests(unittest.TestCase):
    def test_import_policy(self):
        names = json.loads(POLICY.read_text())
        self.assertEqual(len(names), 40)
        self.assertEqual(names, sorted(set(names)))
        self.assertTrue(all(isinstance(name, str) and name for name in names))

    @unittest.skipUnless(os.environ.get("SC_AUTHORITY_TEST_ELF"), "requires actual native ELF")
    def test_native_without_historical_artifacts(self):
        elf = Path(os.environ["SC_AUTHORITY_TEST_ELF"]).resolve()
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            outputs = [work / "first", work / "second"]
            for output in outputs:
                subprocess.run([sys.executable, str(GENERATOR), str(output), str(elf), "--native"],
                               cwd=work, check=True, capture_output=True)
            self.assertFalse((work / ".local").exists())
            for path in outputs[0].iterdir():
                self.assertEqual(path.read_bytes(), (outputs[1] / path.name).read_bytes())
            manifest = json.loads((outputs[0] / "authority.json").read_text())
            self.assertEqual(manifest["source_elf_sha256"], hashlib.sha256(elf.read_bytes()).hexdigest())
            self.assertEqual(manifest["import_policy_sha256"], hashlib.sha256(POLICY.read_bytes()).hexdigest())
            self.assertEqual(manifest["generator_sha256"], hashlib.sha256(GENERATOR.read_bytes()).hexdigest())

    @unittest.skipUnless(os.environ.get("SC_AUTHORITY_TEST_ELF"), "requires actual native ELF")
    def test_native_refuses_changed_import_policy(self):
        elf = Path(os.environ["SC_AUTHORITY_TEST_ELF"]).resolve()
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            generator = work / GENERATOR.name
            policy = work / POLICY.name
            shutil.copyfile(GENERATOR, generator)
            names = json.loads(POLICY.read_text())
            for replacement in ("unapproved_import", names[1]):
                changed = list(names)
                changed[0] = replacement
                policy.write_text(json.dumps(changed))
                result = subprocess.run(
                    [sys.executable, str(generator), str(work / "refused"), str(elf), "--native"],
                    cwd=work, capture_output=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse((work / "refused/ai_authority.h").exists())


if __name__ == "__main__":
    unittest.main()
