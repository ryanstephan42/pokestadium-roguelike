#ifndef RL_NATIVE_IMPORTS_H
#define RL_NATIVE_IMPORTS_H

typedef struct RlArchiveImport {
    unsigned int original;
    unsigned int linked;
} RlArchiveImport;

extern const RlArchiveImport rl_archive_imports[];
extern const unsigned int rl_archive_import_count;

/* Original archive loaders only. Returns -1 for unsupported imports, else
 * the number of repaired instructions. Linked ELF output is ineligible. */
int rl_relocate_archive_imports(unsigned int id, unsigned int* fragment);

#endif
