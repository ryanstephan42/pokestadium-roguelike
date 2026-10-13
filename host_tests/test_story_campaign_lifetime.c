/* Refused-open campaign cleanup through the actual stack: resident installer
   (sc_campaign.c), persistent-image adapter (sc_native_campaign.c), loader
   (sc_persist.c), arena, actual loop, lifetime, memory, crash, DP retirement
   and actual sc_native_display_acquire (exact checked workspace/context
   bodies). Labelled HOST seams: persist native backend (map/load/enter),
   OS/VI/pool fixtures from the display/retirement runners, and Pak/UI/driver,
   operation and terminal entry points, which this refused-open path never
   reaches (they assert). */
#define SC_LIFETIME_FIXTURE_MAIN lifetime_fixture_main
#include "test_story_native_lifetime.c"
#include "src/story/sc_native_campaign.h"

const ScCatalog sc_catalog;
ScNativeLaunchHook *gScNativeLaunchHook;

/* ---- HOST seam: persist native backend ---- */
static int mapped;
static unsigned loads, unmaps, entries;
int sc_persist_native_layout(ScPersistLayout *l) {
    ScPersistLayout v = {0x1000, 0x13a0, 0x100, 0x200, 0x300, 0x3c0};
    *l = v; return 1;
}
int sc_persist_native_unmapped(void) { return !mapped; }
int sc_persist_native_load(unsigned char *image, const ScPersistLayout *l, ScMaU32 n) {
    (void)l; ++loads; memset(image, 0, n); mapped = 1; return 1;
}
int sc_persist_native_quiescent(void) { return 1; }
int sc_persist_native_pool_clear(const unsigned char *image, ScMaU32 n) {
    (void)image; (void)n; return 1;
}
int sc_persist_native_unmap(unsigned char *image, ScMaU32 n) {
    (void)image; (void)n; ++unmaps; mapped = 0; return 1;
}
int sc_persist_native_enter(unsigned char *image, int arg0, unsigned arg1) {
    assert(mapped && image == sc_persist_state()->image && arg0 == SC_CAMPAIGN_ENTRY);
    ++entries;
    return sc_campaign_dispatch(arg1);
}

/* ---- HOST seams never reached once display acquisition is refused ---- */
#define UNREACHED(what) assert(!what " reached after refused display acquisition")
int sc_ui_driver_detach_battle(ScUiDriver *driver) {
    (void)driver; UNREACHED("battle detach"); return SC_INVALID;
}
int sc_native_pak_begin(ScNativePak *p) {
    (void)p; UNREACHED("Pak begin"); return SC_SAVE_IO;
}
int sc_native_pak_end(ScNativePak *p) {
    (void)p; UNREACHED("Pak end"); return SC_SAVE_IO;
}
int sc_ui_resume_postbattle(ScUi *ui, const ScUiBattleRequest *request,
                            const ScState *state, const RlLaunchResult *result,
                            const ScKoLedger *ledger, unsigned outcome) {
    (void)ui; (void)request; (void)state; (void)result; (void)ledger; (void)outcome;
    UNREACHED("postbattle resume"); return SC_INVALID;
}
int sc_native_pak_bind(ScNativePak *p, unsigned c) { (void)p; (void)c; UNREACHED("Pak bind"); return SC_SAVE_IO; }
int sc_native_ui_acquire(ScNativeUi *u, ScNativePak *p, ScMaArena *a) { (void)u; (void)p; (void)a; UNREACHED("UI acquire"); return SC_INVALID; }
int sc_native_ui_prepare(ScNativeUi *u, ScUi *ui, const ScCatalog *c, RlU32 s) {
    (void)u; (void)ui; (void)c; (void)s; UNREACHED("UI prepare"); return SC_INVALID;
}
int sc_ui_driver_bind(ScUiDriver *d, ScUi *u, ScUiBattleRequest *r, const ScUiDriverCalls *c) {
    (void)d; (void)u; (void)r; (void)c; UNREACHED("driver bind"); return SC_INVALID;
}
int sc_ui_driver_observe(ScUiDriver *d, RlU32 id, const ScBattleSnapshot p[6], unsigned a,
                         unsigned e, unsigned h) {
    (void)d; (void)id; (void)p; (void)a; (void)e; (void)h; UNREACHED("observe"); return SC_INVALID;
}
int sc_native_ui_draw(ScNativeUi *u, const ScUi *ui) { (void)u; (void)ui; UNREACHED("draw"); return SC_INVALID; }
int sc_native_ui_input(ScNativeUi *u, ScUiDriver *d) { (void)u; (void)d; UNREACHED("input"); return SC_INVALID; }
int sc_native_ui_finish(ScNativeUi *u, ScUiDriver *d, RlU32 id, unsigned o) {
    (void)u; (void)d; (void)id; (void)o; UNREACHED("finish"); return SC_INVALID;
}
int sc_native_operation_bind(ScNativeOperation *o, ScNativeLifetime *l) {
    (void)o; (void)l; UNREACHED("operation bind"); return SC_INVALID;
}
int sc_native_operation_submit(ScNativeOperation *o, const ScNativeCampaignBacking *b,
                                const ScUiBattleRequest *r) {
    (void)o; (void)b; (void)r; UNREACHED("submit"); return SC_INVALID;
}
int sc_native_operation_poll(ScNativeOperation *o, const ScNativeCampaignBacking *b,
                              ScNativeCampaignEvent *e) {
    (void)o; (void)b; (void)e; UNREACHED("poll"); return SC_INVALID;
}
int sc_native_operation_resume(ScNativeOperation *o, const ScNativeCampaignBacking *b,
                                RlU32 id, RlU32 g, const ScState *s, const ScCombat *c) {
    (void)o; (void)b; (void)id; (void)g; (void)s; (void)c; UNREACHED("resume"); return SC_INVALID;
}
int sc_native_operation_observe(ScNativeOperation *o, const ScNativeCampaignBacking *b,
                                const ScBattleSnapshot p[6], unsigned a, unsigned e, unsigned h) {
    (void)o; (void)b; (void)p; (void)a; (void)e; (void)h; UNREACHED("observe"); return SC_INVALID;
}
int sc_native_terminal_install(ScNativeTerminalBinding *b, ScNativeTerminal *t,
                               ScNativeOperation *o, const ScNativeCampaignBacking *k) {
    (void)b; (void)t; (void)o; (void)k; UNREACHED("install"); return SC_INVALID;
}
int sc_native_terminal_uninstall(ScNativeTerminalBinding *b) { (void)b; UNREACHED("uninstall"); return SC_INVALID; }
int sc_native_terminal_drain(ScNativeTerminal *t, ScNativeOperation *o,
                             const ScNativeCampaignBacking *b, unsigned p, unsigned e) {
    (void)t; (void)o; (void)b; (void)p; (void)e; UNREACHED("drain"); return SC_INVALID;
}

static ScCampaignControl *ctl(void) { return (ScCampaignControl *)sc_campaign_control(); }

/* Refused display acquisition after the lifetime admitted its DATA lease:
   OPEN reports the refusal, the installer unpublishes (CLOSING) and close
   releases the never-published backing, unpins and unmaps. */
static void refused_display(ScMaArena *arena, unsigned at, int scope) {
    ScNativeLifetime *owner = &ctl()->lifetime;
    unsigned before = unmaps;
    allocations = 0; fail_at = at; fail_scope = scope;
    assert(sc_campaign_open(arena, 77) == SC_UNAVAILABLE);
    fail_at = 0; fail_scope = 0;
    assert(sc_campaign_phase() == SC_CAMPAIGN_CLOSING && !sc_campaign_active());
    assert(owner->live && owner->memory.live && owner->memory.lease.ticket);
    assert(owner->loop.acquired && !owner->loop.prepared && !owner->loop.display_used);
    assert(!owner->loop.display_acquired && !owner->display.acquired && !sDisplay);
    assert(!owner->display.scope && !owner->display.screen && !depth && !used);
    assert(!owner->retirement.acquired && mapped);
    assert(sc_persist_state()->pins[SC_PERSIST_PIN_LIFETIME] == 1);
    assert(sc_persist_state()->phase != SC_PERSIST_PUBLISHED);
    assert(sc_campaign_close() == SC_OK && sc_campaign_phase() == SC_CAMPAIGN_IDLE);
    assert(!owner->live && !owner->memory.live && !owner->memory.storage);
    assert(!owner->loop.acquired && !owner->retirement.acquired && !owner->retirement.token.control);
    assert(!mapped && unmaps == before + 1 && !sc_persist_holds_fragment());
    assert(!sc_persist_state()->pins[SC_PERSIST_PIN_LIFETIME] && !submitted);
}

/* The unpublished release is confined to a never-displayed lifetime: any
   presented-display evidence or a crash keeps the ordinary retirement path
   (PENDING/UNAVAILABLE, nothing freed). */
static void guarded(ScMaArena *arena) {
    ScNativeLifetime *owner = &ctl()->lifetime;
    const ScNativeCampaignBacking *b = &owner->loop.backing;
    allocations = 0; fail_at = 1;
    assert(sc_campaign_open(arena, 77) == SC_UNAVAILABLE);
    fail_at = 0;
    owner->loop.display_used = 1;
    assert(sc_native_lifetime_release(owner, b) == SC_PENDING && owner->memory.live);
    owner->loop.display_used = 0;
    owner->display.acquired = 1;
    assert(sc_native_lifetime_release(owner, b) == SC_PENDING && owner->memory.live);
    owner->display.acquired = 0;
    owner->display.screen = (unk_func_80007444 *)1;
    assert(sc_native_lifetime_release(owner, b) == SC_INVALID && owner->memory.live);
    owner->display.screen = NULL;
    sc_crash_active = 1;
    assert(sc_native_lifetime_release(owner, b) == SC_UNAVAILABLE && owner->memory.live);
    sc_crash_active = 0;
    assert(owner->live && owner->memory.lease.ticket && !interrupt_depth);
    assert(sc_campaign_close() == SC_OK && !owner->memory.live && !mapped);
}

/* A crash during refused-open cleanup fail-stops: mapping, LIFETIME pin and
   DATA lease are all retained. */
static void crash_retains(ScMaArena *arena) {
    ScNativeLifetime *owner = &ctl()->lifetime;
    allocations = 0; fail_scope = 1;
    assert(sc_campaign_open(arena, 77) == SC_UNAVAILABLE);
    fail_scope = 0;
    sc_crash_active = 1;
    assert(sc_campaign_close() != SC_OK && sc_campaign_phase() == SC_CAMPAIGN_FAILED);
    assert(owner->live && owner->memory.live && owner->memory.lease.ticket && mapped);
    assert(sc_persist_state()->pins[SC_PERSIST_PIN_LIFETIME] == 1);
    assert(sc_campaign_close() == SC_INVALID && mapped && owner->memory.live);
}

int main(void) {
    ScMaArena arena = {0};
    unk_D_80068BB0 fallback;
    unk_func_80007444 borrowed;
    Gfx commands[8];
    RlU8 *pixels;
    unsigned at;
    pixels = mmap((void *)0x80400000U, 0x3da7e0U, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    assert(pixels == (void *)0x80400000U);
    memset(&D_80083CA0, 0, sizeof(D_80083CA0));
    func_800062E4(&fallback, 0, IMAGE_SIZE_BITS_16b, 640, 1, (u32)0x80010000U);
    D_80083CA0.unk_9E0 = &fallback;
    D_80083CA0.unk_A8A = 1;
    memset(&borrowed, 0, sizeof(borrowed));
    memset(&D_800A7428, 0, sizeof(D_800A7428));
    gDisplayListHead = commands; current_screen = &borrowed;
    D_80068BB0 = D_80083CA0.unk_9E0; D_800A7440.x1 = 7;
    assert(sc_ma_init(&arena, osMemSize, pixels, 0x3da7e0U));
    refused_display(&arena, 0, 1);
    for (at = 1; at <= 4; at++) refused_display(&arena, at, 0);
    guarded(&arena);
    assert(loads == 6 && unmaps == 6);
    crash_retains(&arena);
    puts("CAMPAIGN REFUSED-DISPLAY CLEANUP HOST PASSED: actual installer/adapter/persist/loop/lifetime/memory/display; "
         "scope and 4 allocation refusals release unpublished backing; presented/crash evidence retains");
    return 0;
}
