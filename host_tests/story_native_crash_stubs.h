#ifndef STORY_NATIVE_CRASH_STUBS_H
#define STORY_NATIVE_CRASH_STUBS_H
#define _COMMON_H
#define _SRC_DP_INTRO_H_
#define _CRASH_SCREEN_H_
#include <stddef.h>
typedef unsigned short u16;
typedef unsigned int u32;
typedef struct ScDpToken { void *control; u32 generation; } ScDpToken;
typedef struct ScDpFrame {
    unsigned char unk_00, unk_01, unk_02, unk_03;
    u32 unk_04, unk_08;
    void *unk_0C;
} ScDpFrame;
typedef struct ScDpSnapshot {
    ScDpFrame frames[3];
    void *fallback, *vi_current, *vi_next, *crash_frame;
    int waiting, requests, completions, events;
    int rsp_state, rdp_state, task_completions;
} ScDpSnapshot;
typedef struct CrashScreen {
    u16 *frameBuf;
    u16 width, height;
    int queue;
} CrashScreen;
extern CrashScreen gCrashScreen;
extern volatile u32 sc_crash_active;
extern u32 osMemSize;
u32 __osDisableInt(void);
void __osRestoreInt(u32);
int sc_dp_snapshot(const ScDpToken *, ScDpSnapshot *);
int sc_dp_quiescent(const ScDpToken *);
void crash_screen_set_draw_info(u16 *, u16, u16);
#endif
