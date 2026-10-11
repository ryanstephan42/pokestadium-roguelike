#ifdef STORY_NATIVE_UI
#include "sc_native_icons.h"

static BinArchiveFile *icon_file(BinArchive *archive, unsigned species) {
    BinArchiveFile *file;
    if (!archive || !species || species > 151 || species > archive->num_files ||
        (archive->unk_00 & (1 | 4))) return NULL;
    file = (BinArchiveFile *)((u8 *)archive + sizeof(BinArchive)) + species - 1;
    return file->size == 40 * 40 * 2 ? file : NULL;
}

int sc_native_icon_cached(BinArchive *archive, unsigned species) {
    BinArchiveFile *file = icon_file(archive, species);
    return file && file->unk_08 != NULL;
}

int sc_native_icon_prefetch(BinArchive *archive, unsigned species) {
    if (!icon_file(archive, species)) return 0;
    if (!sc_native_icon_cached(archive, species) && !func_8000484C(archive, species - 1))
        return 0;
    return sc_native_icon_cached(archive, species);
}
#endif
