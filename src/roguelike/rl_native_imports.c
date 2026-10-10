/* PURE */
#include "rl_native_imports.h"

static unsigned int rl_archive_target(unsigned int original) {
    unsigned int low = 0;
    unsigned int high = rl_archive_import_count;
    unsigned int middle;

    while (low < high) {
        middle = low + (high - low) / 2;
        if (rl_archive_imports[middle].original < original) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    if (low < rl_archive_import_count && rl_archive_imports[low].original == original) {
        return rl_archive_imports[low].linked;
    }
    return 0;
}

int rl_relocate_archive_imports(unsigned int id, unsigned int* fragment) {
    unsigned int* relocations;
    unsigned int* instruction;
    unsigned int entry;
    unsigned int target;
    unsigned int i;
    unsigned int mapped;
    int changed = 0;

    /* Archive fragment zero imports native battle callbacks by their original PCs.
     * Linked ELF fragments must not pass through this old-address translation. */
    if (id != 0 || !rl_archive_import_count) {
        return 0;
    }
    relocations = (unsigned int*)((unsigned char*)fragment + fragment[5]);
    for (i = 0; i < relocations[0]; i++) {
        entry = relocations[i + 1];
        if (((entry >> 24) & 0x7F) != 4) {
            continue;
        }
        instruction = (unsigned int*)((unsigned char*)fragment + (entry & 0xFFFFFF));
        target = 0x80000000U | ((*instruction & 0x03FFFFFFU) << 2);
        if (target < 0x84300000U || target >= 0x84400000U) {
            continue;
        }
        mapped = rl_archive_target(target);
        if (!mapped || (mapped & 3) || mapped < 0x84300000U || mapped >= 0x84400000U ||
            ((*instruction >> 26) != 2 && (*instruction >> 26) != 3)) {
            return -1;
        }
        *instruction = (*instruction & 0xFC000000U) | ((mapped & 0x0FFFFFFFU) >> 2);
        changed++;
    }
    return changed;
}
