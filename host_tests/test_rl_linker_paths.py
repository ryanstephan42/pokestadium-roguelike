import unittest

from tools.rl.gen_rl_ld import remap_build_paths


class LinkerPathTests(unittest.TestCase):
    def test_nested_build_directory_does_not_double_prefix_explicit_objects(self):
        text = "build/src/main.o(.text);\nbuild/story/candidate/src/roguelike/rl_battle.o(.text);"
        expected = ("build/story/candidate/src/main.o(.text);\n"
                    "build/story/candidate/src/roguelike/rl_battle.o(.text);")
        self.assertEqual(remap_build_paths(text, "build/story/candidate"), expected)
        self.assertEqual(remap_build_paths(expected, "build/story/candidate"), expected)

    def test_normal_rl_directory_and_nonpath_text_are_unchanged(self):
        text = "build/src/main.o(.text); build_rl/src/roguelike/rl_battle.o(.text); rebuild/a.o"
        self.assertEqual(remap_build_paths(text, "build_rl"),
                         "build_rl/src/main.o(.text); build_rl/src/roguelike/rl_battle.o(.text); rebuild/a.o")
        self.assertEqual(remap_build_paths(text, "build"), text)


if __name__ == "__main__":
    unittest.main()
