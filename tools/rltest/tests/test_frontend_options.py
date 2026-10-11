import ctypes as ct
import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

from tools.rltest.frontend import (
    ANGRYLION_THREADS_OPTION, Frontend, HarnessError, SOFTWARE_OPTIONS,
    SystemInfo, Variable,
)


class FrontendOptionTests(unittest.TestCase):
    def setUp(self):
        self.environment = patch.dict(os.environ, {}, clear=True)
        self.environment.start()
        self.addCleanup(self.environment.stop)
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.core_path = self.root / "core.so"
        self.rom = self.root / "test.z64"
        self.map = self.root / "test.map"
        self.core_path.touch()
        self.rom.touch()
        self.map.write_text("0x80000010 test_symbol\n")

    def frontend(self, tag="one", *, choices=None, supported=True, options=None,
                 register=True):
        core = Mock()
        core.retro_api_version.return_value = 1
        core.retro_load_game.return_value = True

        def system_info(pointer):
            ct.cast(pointer, ct.POINTER(SystemInfo)).contents.need_fullpath = True

        def initialize():
            if not register:
                return
            callback = core.retro_set_environment.call_args.args[0]
            choices_text = choices or "all threads|" + "|".join(map(str, range(1, 65)))
            definitions = (Variable * 2)(
                Variable(
                    ANGRYLION_THREADS_OPTION.encode() if supported else b"other-option",
                    ("Threads; " + choices_text).encode(),
                ),
                Variable(None, None),
            )
            callback(16, ct.cast(definitions, ct.c_void_p))

        core.retro_get_system_info.side_effect = system_info
        core.retro_init.side_effect = initialize
        with patch("tools.rltest.frontend.ct.CDLL", return_value=core), \
                patch.object(Frontend, "step"):
            emu = Frontend(self.core_path, self.rom, self.map,
                           self.root / tag, options=options)
        self.addCleanup(emu.close)
        return emu

    def test_unset_preserves_renderer_and_core_default(self):
        emu = self.frontend()
        self.assertEqual(emu.options, {
            **SOFTWARE_OPTIONS, ANGRYLION_THREADS_OPTION: "all threads",
        })
        self.assertNotIn(ANGRYLION_THREADS_OPTION, SOFTWARE_OPTIONS)

    def test_cap_reaches_native_get_variable(self):
        for threads in ("1", "4", "64"):
            with self.subTest(threads=threads):
                os.environ["RLTEST_ANGRYLION_THREADS"] = threads
                emu = self.frontend(threads)
                variable = Variable(ANGRYLION_THREADS_OPTION.encode(), None)
                self.assertTrue(emu._environment(15, ct.byref(variable)))
                self.assertEqual(variable.value, threads.encode())
                for key, value in SOFTWARE_OPTIONS.items():
                    self.assertEqual(emu.options[key], value)

    def test_invalid_env_fails_before_loading_core_or_writing_output(self):
        for threads in ("", "0", "-1", "65", "4.0", "04", " 4", "4 ",
                        "all threads", "four"):
            with self.subTest(threads=threads):
                os.environ["RLTEST_ANGRYLION_THREADS"] = threads
                with patch("tools.rltest.frontend.ct.CDLL") as load, \
                        self.assertRaisesRegex(HarnessError, "RLTEST_ANGRYLION_THREADS"):
                    Frontend(self.core_path, self.rom, self.map, self.root / "invalid")
                load.assert_not_called()
                self.assertFalse((self.root / "invalid").exists())

    def test_core_rejects_unadvertised_cap(self):
        os.environ["RLTEST_ANGRYLION_THREADS"] = "4"
        with self.assertRaisesRegex(HarnessError, "Unsupported core option"):
            self.frontend(choices="all threads|1|2")
        with self.assertRaisesRegex(HarnessError, "Core does not support"):
            self.frontend(supported=False)
        with self.assertRaisesRegex(HarnessError, "Core does not support"):
            self.frontend(register=False)

    def test_options_and_directories_are_instance_local(self):
        options = {ANGRYLION_THREADS_OPTION: "2"}
        os.environ["RLTEST_ANGRYLION_THREADS"] = "4"
        capped = self.frontend("capped", options=options)
        os.environ.pop("RLTEST_ANGRYLION_THREADS")
        baseline = self.frontend("baseline")
        self.assertEqual(capped.options[ANGRYLION_THREADS_OPTION], "4")
        self.assertEqual(baseline.options[ANGRYLION_THREADS_OPTION], "all threads")
        self.assertEqual(options, {ANGRYLION_THREADS_OPTION: "2"})
        self.assertNotIn(ANGRYLION_THREADS_OPTION, SOFTWARE_OPTIONS)
        for command in (9, 30, 31):
            directories = []
            for emu in (capped, baseline):
                directory = ct.c_char_p()
                self.assertTrue(emu._environment(command, ct.byref(directory)))
                directories.append(directory.value)
            self.assertEqual(directories, [
                str(self.root / "capped").encode(),
                str(self.root / "baseline").encode(),
            ])


if __name__ == "__main__":
    unittest.main()
