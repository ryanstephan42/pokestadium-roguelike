#ifndef RL_PAK_H
#define RL_PAK_H

#include "rl_save.h"

#define RL_PAK_METADATA_SIZE 1280
#define RL_PAK_GAME_CODE 0x524c3031U
#define RL_PAK_COMPANY_CODE 0x524c
#define RL_PAK_PAGES (RL_STORAGE_NOTE_SIZE / 256)

extern const RlU8 gRlPakName[16];
extern const RlU8 gRlPakExtension[4];

typedef struct RlPakLayout {
    RlU8 pages[RL_PAK_PAGES];
    unsigned note;
    unsigned free_pages, free_notes;
} RlPakLayout;

/* Read-only validation of a standard one-bank Pak; no repair or format. */
RlSaveStatus rl_pak_validate(const RlU8 metadata[RL_PAK_METADATA_SIZE],
                             RlPakLayout *layout);

#endif
