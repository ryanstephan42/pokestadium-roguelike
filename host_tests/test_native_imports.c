#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/roguelike/rl_native_imports.h"

const RlArchiveImport rl_archive_imports[] = {
    { 0x8434D890U, 0x8434E6ACU },
    { 0x8434E6ACU, 0x84350000U }
};
const unsigned int rl_archive_import_count = 2;

static void setup(unsigned int* fragment, unsigned int opcode, unsigned int target) {
    memset(fragment, 0, 32 * sizeof(*fragment));
    fragment[5] = 0x40;
    fragment[8] = opcode | ((target & 0x0FFFFFFFU) >> 2);
    fragment[16] = 1;
    fragment[17] = 0x04000020;
}

int main(void) {
    unsigned int fragment[32];
    unsigned int instruction;
    unsigned int targets[] = { 0x81000020U, 0x80012340U, 0x84201234U };
    unsigned int i;

    setup(fragment, 0x08000000U, 0x8434D890U);
    assert(rl_relocate_archive_imports(0, fragment) == 1);
    assert(fragment[8] == (0x08000000U | ((0x8434E6ACU & 0x0FFFFFFFU) >> 2)));
    /* Normal relocation clears the import table before any subsequent load.
     * The new PC deliberately aliases another original function's PC. */
    fragment[16] = 0;
    instruction = fragment[8];
    assert(rl_relocate_archive_imports(0, fragment) == 0);
    assert(fragment[8] == instruction);

    setup(fragment, 0x0C000000U, 0x8434D890U);
    assert(rl_relocate_archive_imports(0, fragment) == 1);
    assert((fragment[8] & 0xFC000000U) == 0x0C000000U);
    setup(fragment, 0x08000000U, 0x8434E6ACU);
    instruction = fragment[8];
    assert(rl_relocate_archive_imports(62, fragment) == 0);
    assert(fragment[8] == instruction);
    for (i = 0; i < sizeof(targets) / sizeof(targets[0]); i++) {
        setup(fragment, 0x08000000U, targets[i]);
        instruction = fragment[8];
        assert(rl_relocate_archive_imports(0, fragment) == 0);
        assert(fragment[8] == instruction);
    }
    setup(fragment, 0x08000000U, 0x8434D894U);
    assert(rl_relocate_archive_imports(0, fragment) == -1);
    setup(fragment, 0x24000000U, 0x8434D890U);
    assert(rl_relocate_archive_imports(0, fragment) == -1);
    setup(fragment, 0x08000000U, 0x8434D890U);
    fragment[17] = 0x02000020;
    instruction = fragment[8];
    assert(rl_relocate_archive_imports(0, fragment) == 0);
    assert(fragment[8] == instruction);
    puts("Native archive imports: OK");
    return 0;
}
