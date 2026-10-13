#ifndef SC_NATIVE_PAK_H
#define SC_NATIVE_PAK_H

#if defined(STORY_NATIVE_PROBE) || defined(STORY_NATIVE_UI)
#include "global.h"
#include "src/roguelike/rl_pak.h"
#include "sc_save.h"

typedef struct ScNativePak {
    unsigned controller;
    RlU8 metadata[RL_PAK_METADATA_SIZE];
    RlU8 device_id[32];
    RlPakLayout layout;
    OSPfs pfs;
    ScSaveDevice device;
    RlU32 binding_id;
    int bound, session, opened, identified, note_seen;
} ScNativePak;

/* Zeroed caller-owned backing; one fixed controller for its entire lifetime. */
int sc_native_pak_bind(ScNativePak *pak, unsigned controller);
int sc_native_pak_begin(ScNativePak *pak);
int sc_native_pak_end(ScNativePak *pak);
int sc_native_pak_release(ScNativePak *pak);
#endif

#endif
