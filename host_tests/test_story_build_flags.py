import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SERVICE = [
    "ROGUELIKE=1", "NON_MATCHING=1", "COMPARE=0", "STORY_PROBE=1",
    "STORY_KO_PROBE=1", "STORY_SIX_PROBE=1", "STORY_SERVICE_CANDIDATE=1",
]


class StoryBuildFlagsTest(unittest.TestCase):
    def refused(self, flags, message):
        result = subprocess.run(
            ["make", "--no-print-directory", "-n", *flags],
            cwd=ROOT, text=True, capture_output=True,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(message, result.stderr)

    def test_service_requires_explicit_authority(self):
        self.refused(
            SERVICE + ["SC_SERVICE_AUTH_DIR=", "SC_SERVICE_SPAN_DIR="],
            "requires explicit SC_SERVICE_AUTH_DIR",
        )

    def test_service_requires_explicit_rights(self):
        self.refused(
            SERVICE + ["SC_SERVICE_AUTH_DIR=unused", "SC_SERVICE_SPAN_DIR="],
            "requires explicit SC_SERVICE_SPAN_DIR",
        )

    def test_service_requires_explicit_closure_elf(self):
        self.refused(
            SERVICE + ["SC_SERVICE_AUTH_DIR=unused", "SC_SERVICE_SPAN_DIR=unused",
                       "SC_AI_BASE="],
            "requires explicit SC_AI_BASE ELF",
        )

    def test_service_excludes_legacy_selector(self):
        self.refused(
            SERVICE + ["STORY_AI_PROTOTYPE=1"],
            "excludes the legacy SELECT prototype",
        )

    def test_native_ui_requires_story_probe(self):
        self.refused(
            ["STORY_NATIVE_UI=1", "STORY_PROBE=0"],
            "requires STORY_PROBE=1",
        )


if __name__ == "__main__":
    unittest.main()
