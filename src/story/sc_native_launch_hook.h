#ifndef SC_NATIVE_LAUNCH_HOOK_H
#define SC_NATIVE_LAUNCH_HOOK_H
#if defined(STORY_NATIVE_UI) && defined(STORY_SERVICE_CANDIDATE)

/* Resident story launch hook. NULL outside an installed story operation, so
   vanilla/ordinary RL launches are unchanged. The installer owns the callback
   mapping and must keep it admitted until uninstall. arm runs before the RLBT
   pool push and refuses the launch on non-OK; drain runs after the RLBT pop with
   the raw per-side alive flags; abort runs when native battle initialization
   fails after arm. */
typedef struct ScNativeLaunchHook {
    void *context;
    int (*arm)(void *context);
    void (*drain)(void *context, unsigned player_alive, unsigned enemy_alive);
    void (*abort)(void *context);
} ScNativeLaunchHook;

extern ScNativeLaunchHook *gScNativeLaunchHook;

#endif
#endif
