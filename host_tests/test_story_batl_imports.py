"""HOST relocation seam: no native gameplay claim."""

import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class BattleImportTests(unittest.TestCase):
    def test_mapped_callback_and_import_bounds(self):
        source = (ROOT / "src/story/sc_batl_scope.c").read_text()
        functions = source[source.index("static uintptr_t battle_import("):
                           source.index("static int roots_match(")]
        fixture = r"""
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
typedef float f32;
typedef uint32_t u32;
static uintptr_t base;
static size_t extent;
static unsigned translations, callbacks, missing;
static f32 clock_value;
const f32 *sc_batl_clock_source(void) { abort(); }
static const f32 *loaded_clock(void) { ++callbacks; return &clock_value; }
static uintptr_t Memmap_GetFragmentBaseVaddr(unsigned id) {
    assert(id == 0x33); return base;
}
static size_t Memmap_GetFragmentSize(unsigned id) {
    assert(id == 0x33); return extent;
}
static uintptr_t Memmap_GetFragmentVaddr(void *symbol) {
    ++translations;
    if (symbol == (void *)sc_batl_clock_source)
        return missing ? 0 : (uintptr_t)loaded_clock;
    return base + ((uintptr_t)symbol & 0xfffffU);
}
"""
        checks = r"""
int main(void) {
    extent = 32;
    assert(!battle_import((void *)0x84300010U, 4));
    assert(!translations);
    base = 0x1000;
    assert(!battle_import((void *)0x8430001dU, 4));
    assert(!battle_import((void *)0x84300000U, 33));
    assert(!translations);
    assert(battle_import((void *)0x8430001cU, 4) == 0x101c);
    assert(translations == 1);
    assert(battle_clock() == &clock_value);
    assert(callbacks == 1 && translations == 2);
    missing = 1;
    assert(!battle_clock() && callbacks == 1);
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory)
            (path / "imports.c").write_text(fixture + functions + checks)
            subprocess.run(["gcc", "-std=c99", "-Wall", "-Wextra", "-Werror",
                            str(path / "imports.c"), "-o", str(path / "imports")],
                           check=True, capture_output=True)
            subprocess.run([str(path / "imports")], check=True, capture_output=True)


if __name__ == "__main__":
    unittest.main()
