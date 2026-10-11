#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../src/story/sc_native_ui.h"
#include "../src/story/sc_native_resources.h"
#include "../src/story/sc_native_icons.h"
#include "../src/controller.h"

int sc_pak_probe_load(unsigned, const ScCatalog *, ScSnapshot *);
int sc_pak_probe_store(unsigned, const ScCatalog *, const ScSnapshot *, int);

static ScNativePak pak, other;
static ScNativeUi native;
static ScUi ui;
static ScUiDriver driver;
static ScUi unrelated_ui;
static ScUiDriver unrelated_driver;
static ScUiBattleRequest request;
static RlU8 bytes[32768], original[32768];
static unsigned reads, writes, allocated, locks, unlocks, logs, frames;
static int error, fail_at, plug_error, alloc_error, ui_error, scope_error;
static MainPool pool;
static MainPoolState states[4];
static unsigned depth;
typedef struct Allocation { void *data; AllocateFunc cleanup; u32 arg; unsigned depth; } Allocation;
static Allocation allocations[64];
static unsigned allocation_count, allocation_calls;
static int fail_allocation, missing_archive, short_archive, missing_file;
static int launch_status = SC_UNAVAILABLE;
static BinArchive *icons;
static char **moves, **species;
unk_D_800AC870 *D_800AC870;
u8 fonts_ROM_START[1], fonts_ROM_END[1], textdata_ROM_START[1];
u8 pokemon_icons_ROM_START[1], pokemon_icons_ROM_END[1];
OSMesgQueue gSIEventMesgQueue;
u8 __osPfsInodeCacheBank;
static Controller controller;
Controller *gPlayer1Controller = &controller;

static void be16(RlU8 *p, unsigned n) { p[0] = (RlU8)(n >> 8); p[1] = (RlU8)n; }
static void inode_sum(void) {
    unsigned i, sum = 0;
    for (i = 266; i < 512; i++) sum += bytes[i];
    bytes[257] = (RlU8)sum;
    memcpy(bytes + 512, bytes + 256, 256);
}
static void fixture(void) {
    unsigned i, sum = 0, inverse = 0;
    memset(bytes, 0, sizeof(bytes));
    bytes[57] = bytes[58] = 1;
    for (i = 32; i < 60; i += 2) {
        unsigned n = ((unsigned)bytes[i] << 8) | bytes[i + 1];
        sum += n; inverse += (~n) & 65535U;
    }
    be16(bytes + 60, sum); be16(bytes + 62, inverse);
    memcpy(bytes + 96, bytes + 32, 32);
    memcpy(bytes + 128, bytes + 32, 32);
    memcpy(bytes + 192, bytes + 32, 32);
    for (i = 5; i < 128; i++) be16(bytes + 256 + 2 * i, 3);
    inode_sum();
    reads = writes = allocated = 0;
    error = fail_at = plug_error = alloc_error = 0;
}
static void reset_binding(void) {
    assert(sc_native_pak_release(&pak) == SC_SAVE_OK);
    assert(sc_native_pak_bind(&pak, 0) == SC_SAVE_OK);
}
void Cont_BlockEepromQueue(void) { assert(locks == unlocks); locks++; }
void Cont_NoBlockEepromQueue(void) { assert(locks == unlocks + 1); unlocks++; }
s32 osPfsIsPlug(OSMesgQueue *queue, u8 *bits) {
    assert(queue == &gSIEventMesgQueue && locks == unlocks + 1);
    *bits = 1; return plug_error;
}
s32 __osContRamRead(OSMesgQueue *queue, int channel, u16 block, u8 *out) {
    assert(queue == &gSIEventMesgQueue && channel == 0 && block < 1024);
    assert(locks == unlocks + 1);
    reads++;
    if (fail_at && reads + writes == (unsigned)fail_at) return error;
    memcpy(out, bytes + block * 32, 32); return 0;
}
s32 __osContRamWrite(OSMesgQueue *queue, int channel, u16 block, u8 *in, int force) {
    assert(queue == &gSIEventMesgQueue && channel == 0 && block < 1024 && !force);
    assert(locks == unlocks + 1 && (block == 24 || (block >= 40 && block < 88)));
    writes++;
    if (fail_at && reads + writes == (unsigned)fail_at) return error;
    memcpy(bytes + block * 32, in, 32); return 0;
}
s32 rlPfsAllocateFile(OSPfs *pfs, u16 company, u32 game, u8 *name, u8 *extension,
                      int size, s32 *note) {
    unsigned i;
    allocated++;
    assert(pfs->channel == 0 && company == SC_SAVE_COMPANY && game == SC_SAVE_GAME);
    assert(size == SC_SAVE_NOTE_SIZE && !memcmp(name, sc_save_name, 16));
    assert(!memcmp(extension, sc_save_extension, 4) && __osPfsInodeCacheBank == 250);
    if (alloc_error) return alloc_error;
    bytes[768] = (RlU8)(game >> 24); bytes[769] = (RlU8)(game >> 16);
    bytes[770] = (RlU8)(game >> 8); bytes[771] = (RlU8)game;
    be16(bytes + 772, company); be16(bytes + 774, 5);
    memcpy(bytes + 780, extension, 4); memcpy(bytes + 784, name, 16);
    for (i = 5; i < 11; i++) be16(bytes + 256 + 2 * i, i == 10 ? 1 : i + 1);
    inode_sum(); *note = 0; return 0;
}
s32 HAL_Printf(s16 x, s16 y, const char *fmt, ...) {
    (void)x; (void)y; assert(fmt); logs++; return 0;
}
MainPool *main_pool_get_pool(void) { return &pool; }
u32 main_pool_push_state(u32 tag) {
    assert(!tag);
    if (!scope_error) {
        assert(depth < 4);
        states[depth].prev = pool.mainState;
        pool.mainState = &states[depth++];
    }
    return 0;
}
u32 main_pool_pop_state(u32 tag) {
    assert(!tag && depth);
    while (allocation_count && allocations[allocation_count - 1].depth == depth) {
        Allocation *a = &allocations[--allocation_count];
        if (a->cleanup) a->cleanup(a->data, a->arg);
        free(a->data);
    }
    pool.mainState = pool.mainState->prev; depth--; return 0;
}
void *main_pool_alloc_with_func(u32 size, s32 side, s32 arg, AllocateFunc cleanup) {
    Allocation *a;
    assert(!side && depth && allocation_count < 64);
    allocation_calls++;
    if (fail_allocation && allocation_calls == (unsigned)fail_allocation) return NULL;
    a = &allocations[allocation_count++];
    a->data = calloc(1, size); assert(a->data);
    a->arg = arg; a->cleanup = cleanup; a->depth = depth;
    return a->data;
}
void *main_pool_alloc(u32 size, s32 side) { return main_pool_alloc_with_func(size, side, 0, NULL); }
void main_pool_set_func(void *data, s32 arg, AllocateFunc cleanup) {
    unsigned i;
    for (i = 0; i < allocation_count; i++) if (allocations[i].data == data) {
        allocations[i].arg = arg; allocations[i].cleanup = cleanup; return;
    }
    assert(0);
}
void func_8001E7C0(s32 block, s32 arg) { (void)block; (void)arg; D_800AC870 = NULL; }
void func_8001E7D4(s32 block, s32 arg) {
    (void)block;
    if (D_800AC870) {
        D_800AC870->unk_00[arg].unk_04 = NULL;
        D_800AC870->unk_00[arg].unk_08 = NULL;
    }
}
BinArchive *func_800044F4(u8 *start, u8 *end, s32 mode, s32 side) {
    unsigned kind = start == fonts_ROM_START ? 1 : start == pokemon_icons_ROM_START ? 2 : 3;
    BinArchive *archive;
    (void)end; (void)mode;
    if (missing_archive == (int)kind) return NULL;
    archive = main_pool_alloc(sizeof(*archive) + (kind == 2 ? 151 * sizeof(BinArchiveFile) : 0), 0);
    if (archive) {
        archive->kind = kind;
        archive->unk_00 = side ? 6 : 2;
        archive->num_files = short_archive == (int)kind ? 1 : kind == 1 ? 6 : kind == 2 ? 151 : 42;
        if (kind == 2) {
            unsigned i;
            BinArchiveFile *files = (BinArchiveFile *)((u8 *)archive + sizeof(*archive));
            for (i = 0; i < 151; i++) files[i].size = 3200;
        }
    }
    return archive;
}
void *func_8000484C(BinArchive *archive, s32 file) {
    void *data;
    assert(archive && file >= 0 && file < archive->num_files);
    if (missing_file == file) return NULL;
    data = main_pool_alloc(archive->kind == 1 ? sizeof(ret_func_8000484C) :
                          archive->kind == 2 ? 3200 : 165 * sizeof(char *), 0);
    if (archive->kind == 2) {
        BinArchiveFile *files = (BinArchiveFile *)((u8 *)archive + sizeof(*archive));
        files[file].unk_08 = data;
    }
    return data;
}
s32 rl_ui_init(void) {
    return ui_error || !sc_native_resources_acquire(&icons, &moves, &species) ? RL_UI_INVALID : RL_UI_NONE;
}
void sc_scene_draw(const ScUi *controller_ui) { assert(controller_ui == &ui); frames++; }
int rl_ui_story_icon_cached(unsigned mon) { return sc_native_icon_cached(icons, mon); }
int rl_ui_story_icon_prefetch(unsigned mon) { return sc_native_icon_prefetch(icons, mon); }
static int launch(void *p, const ScUiBattleRequest *r) {
    (void)p; assert(r->operation_id);
    assert(rl_ui_story_icon_cached(r->party[0].species));
    return launch_status;
}
static void draw(void *p, const ScUi *u) { assert(sc_native_ui_draw(p, u) == SC_OK); }
static void leave(void *p) { (void)p; }

static int open_note(int create) {
    return pak.device.open(pak.device.context, SC_SAVE_COMPANY, SC_SAVE_GAME,
                           sc_save_name, sc_save_extension, SC_SAVE_NOTE_SIZE, create);
}
static void change_id(void) {
    unsigned i, sum = 0, inverse = 0;
    bytes[32]++;
    for (i = 32; i < 60; i += 2) {
        unsigned n = ((unsigned)bytes[i] << 8) | bytes[i + 1];
        sum += n; inverse += (~n) & 65535U;
    }
    be16(bytes + 60, sum); be16(bytes + 62, inverse);
    memcpy(bytes + 96, bytes + 32, 32);
    memcpy(bytes + 128, bytes + 32, 32);
    memcpy(bytes + 192, bytes + 32, 32);
}
static void backend(void) {
    unsigned i, total;
    RlU8 block[32];
    int status;
    ScSnapshot save, restored;
    ScOptions options = {0, SC_STANDARD, SC_STARTER_TRIO, 0};
    fixture();
    assert(sc_native_pak_bind(&pak, 4) == SC_SAVE_INVALID);
    assert(sc_native_pak_bind(&pak, 0) == SC_SAVE_OK);
    assert(sc_native_pak_bind(&pak, 0) == SC_SAVE_INVALID);
    assert(open_note(0) == SC_SAVE_INVALID);
    assert(sc_native_pak_begin(&pak) == SC_SAVE_OK);
    assert(sc_native_pak_bind(&other, 1) == SC_SAVE_OK);
    assert(sc_native_pak_begin(&other) == SC_SAVE_INVALID);
    assert(sc_native_pak_release(&pak) == SC_SAVE_INVALID);
    assert(open_note(0) == SC_SAVE_NOT_FOUND && !writes);
    assert(open_note(1) == SC_SAVE_OK && allocated == 1 && writes == 49);
    memcpy(original, bytes, sizeof(bytes));
    assert(open_note(1) == SC_SAVE_OK && allocated == 1);
    memset(block, 42, sizeof(block));
    assert(pak.device.write(&pak, 0, block, 31) == SC_SAVE_INVALID);
    assert(pak.device.read(&pak, SC_SAVE_NOTE_SIZE, block, 32) == SC_SAVE_INVALID);
    assert(pak.device.write(&pak, 0, block, 32) == SC_SAVE_OK);
    memset(block, 0, sizeof(block));
    assert(pak.device.read(&pak, 0, block, 32) == SC_SAVE_OK && block[0] == 42);
    bytes[32]++;
    assert(pak.device.read(&pak, 0, block, 32) == SC_SAVE_DEVICE_CHANGED);
    bytes[32]--;
    assert(sc_native_pak_end(&pak) == SC_SAVE_OK);
    assert(sc_native_pak_end(&pak) == SC_SAVE_INVALID);
    assert(pak.device.read(&pak, 0, block, 32) == SC_SAVE_INVALID);
    change_id();
    assert(sc_native_pak_begin(&pak) == SC_SAVE_OK);
    assert(open_note(1) == SC_SAVE_DEVICE_CHANGED);
    assert(sc_native_pak_end(&pak) == SC_SAVE_OK);
    for (i = 1; i <= 40; i++) {
        memcpy(bytes, original, sizeof(bytes)); reads = writes = 0;
        fail_at = (int)i; error = PFS_ERR_NEW_PACK;
        assert(sc_native_pak_begin(&pak) == SC_SAVE_OK);
        assert(open_note(0) == SC_SAVE_DEVICE_CHANGED && !writes);
        assert(sc_native_pak_end(&pak) == SC_SAVE_OK);
    }
    for (i = 0; i < 6; i++) {
        static const int errors[] = {PFS_ERR_NOPACK, PFS_DATA_FULL, PFS_ERR_DEVICE,
                                    PFS_ERR_INCONSISTENT, PFS_ERR_BAD_DATA, 99};
        static const int statuses[] = {SC_SAVE_NO_DEVICE, SC_SAVE_FULL, SC_SAVE_BAD_DATA,
                                      SC_SAVE_BAD_DATA, SC_SAVE_BAD_DATA, SC_SAVE_IO};
        fixture(); reset_binding(); plug_error = errors[i];
        assert(sc_native_pak_begin(&pak) == SC_SAVE_OK);
        status = open_note(1);
        assert(status == statuses[i] && !writes && !allocated);
        assert(sc_native_pak_end(&pak) == SC_SAVE_OK);
    }
    fixture(); reset_binding(); alloc_error = PFS_DIR_FULL;
    assert(sc_native_pak_begin(&pak) == SC_SAVE_OK);
    assert(open_note(1) == SC_SAVE_FULL && !writes);
    assert(sc_native_pak_end(&pak) == SC_SAVE_OK);
    fixture(); reset_binding();
    assert(sc_native_pak_begin(&pak) == SC_SAVE_OK && open_note(1) == SC_SAVE_OK);
    total = reads + writes;
    assert(sc_native_pak_end(&pak) == SC_SAVE_OK);
    for (i = 1; i <= total; i++) {
        fixture(); reset_binding(); fail_at = (int)i; error = 99;
        assert(sc_native_pak_begin(&pak) == SC_SAVE_OK);
        assert(open_note(1) == SC_SAVE_IO);
        assert(sc_native_pak_end(&pak) == SC_SAVE_OK);
    }
    fixture(); reset_binding();
    bytes[800] = 1; bytes[801] = 2; bytes[802] = 3; bytes[803] = 4;
    be16(bytes + 804, 0x1234); be16(bytes + 806, 11);
    be16(bytes + 256 + 22, 1); inode_sum();
    memset(bytes + 11 * 256, 0xA5, 256);
    memcpy(original, bytes, sizeof(bytes));
    assert(sc_new(&save.state, &sc_catalog, &options, 123) == SC_OK);
    save.interrupted = 0; save.wire_version = 0;
    assert(sc_native_pak_begin(&pak) == SC_SAVE_OK);
    assert(sc_storage_store(&pak.device, &sc_catalog, &save, 1) == SC_SAVE_OK);
    assert(sc_native_pak_end(&pak) == SC_SAVE_OK);
    assert(sc_native_pak_begin(&pak) == SC_SAVE_OK);
    assert(sc_storage_load(&pak.device, &sc_catalog, &restored) == SC_SAVE_OK);
    assert(!memcmp(&save.state, &restored.state, sizeof(save.state)));
    assert(sc_native_pak_end(&pak) == SC_SAVE_OK);
    assert(sc_pak_probe_load(0, &sc_catalog, &restored) == SC_SAVE_OK);
    assert(!memcmp(&save.state, &restored.state, sizeof(save.state)));
    assert(sc_pak_probe_store(0, &sc_catalog, &save, 0) == SC_SAVE_OK);
    assert(sc_pak_probe_load(4, &sc_catalog, &restored) == SC_SAVE_INVALID);
    assert(!memcmp(bytes + 800, original + 800, 32));
    assert(!memcmp(bytes + 11 * 256, original + 11 * 256, 256));
    fixture();
    assert(sc_native_pak_begin(&pak) == SC_SAVE_OK);
    assert(open_note(1) == SC_SAVE_BAD_DATA && !allocated && !writes);
    assert(sc_native_pak_end(&pak) == SC_SAVE_OK);
    fixture(); bytes[257]++;
    assert(sc_native_pak_begin(&pak) == SC_SAVE_OK);
    assert(open_note(1) == SC_SAVE_BAD_DATA && !writes && !allocated);
    assert(sc_native_pak_end(&pak) == SC_SAVE_OK);
    assert(sc_native_pak_release(&other) == SC_SAVE_OK);
    other.binding_id = 0xFFFFFFFFU;
    assert(sc_native_pak_bind(&other, 1) == SC_SAVE_INVALID && !other.bound);
}

static void graphics_failures(void) {
    unsigned i, total;
    unk_D_800AC870 borrowed, before;
    BinArchive font_archive;
    fixture(); reset_binding();
    missing_file = -1;
    allocation_calls = 0;
    assert(sc_native_ui_acquire(&native, &pak) == SC_OK);
    total = allocation_calls;
    assert(total == 13);
    assert(sc_native_ui_release(&native, NULL, 1) == SC_OK);
    assert(!D_800AC870 && !icons && !moves && !species && !allocation_count);
    for (i = 1; i <= total; i++) {
        allocation_calls = 0; fail_allocation = (int)i;
        assert(sc_native_ui_acquire(&native, &pak) == SC_UNAVAILABLE);
        assert(!depth && !allocation_count && !D_800AC870 && !icons && !moves && !species);
    }
    fail_allocation = 0;
    for (i = 1; i <= 3; i++) {
        missing_archive = (int)i;
        assert(sc_native_ui_acquire(&native, &pak) == SC_UNAVAILABLE);
        missing_archive = 0; short_archive = (int)i;
        assert(sc_native_ui_acquire(&native, &pak) == SC_UNAVAILABLE);
        short_archive = 0;
        assert(!D_800AC870 && !icons && !moves && !species && !allocation_count);
    }
    for (i = 0; i < 5; i++) {
        static const int files[5] = {2, 3, 4, 0x25, 0x24};
        missing_file = files[i];
        assert(sc_native_ui_acquire(&native, &pak) == SC_UNAVAILABLE);
        assert(!D_800AC870 && !icons && !moves && !species && !allocation_count);
    }
    printf("Checked native resources: %u allocation failures, 6 archive failures, 5 file failures; OK\n", total);
    missing_file = -1;
    memset(&borrowed, 0, sizeof(borrowed));
    font_archive.num_files = 6; font_archive.kind = 1; font_archive.unk_00 = 4;
    borrowed.unk_48 = &font_archive; borrowed.unk_50 = 1;
    before = borrowed; D_800AC870 = &borrowed;
    assert(sc_native_ui_acquire(&native, &pak) == SC_OK);
    assert(sc_native_ui_release(&native, NULL, 1) == SC_OK);
    assert(D_800AC870 == &borrowed && !memcmp(&borrowed, &before, sizeof(before)));
    D_800AC870 = NULL;
    font_archive.unk_00 = 1;
    D_800AC870 = &borrowed;
    assert(sc_native_ui_acquire(&native, &pak) == SC_UNAVAILABLE);
    assert(D_800AC870 == &borrowed && !memcmp(&borrowed, &before, sizeof(before)));
    D_800AC870 = NULL;
}

static int native_choose(unsigned id) {
    unsigned i;
    for (i = 0; i < (unsigned)ui.list.count; i++) if (ui.ids[i] == id) break;
    assert(i < (unsigned)ui.list.count);
    while (ui.list.cursor != (int)i) {
        controller.buttonPressed = ui.list.cursor < (int)i ? RL_UI_DOWN : RL_UI_UP;
        assert(sc_native_ui_input(&native, &driver) == SC_OK);
    }
    controller.buttonPressed = RL_UI_A;
    return sc_native_ui_input(&native, &driver);
}

static void child_learning(void) {
    ScUiDriverCalls calls;
    ScTemplate starter;
    ScBattleSnapshot party[6];
    BinArchiveFile *files;
    void *pixels;
    unsigned region = 0, encounter, i, before_alloc, before_reads, before_writes;
    MainPoolState unrelated_scope, *child;
    fixture();
    assert(sc_native_pak_bind(&pak, 0) == SC_SAVE_OK);
    assert(sc_native_ui_acquire(&native, &pak) == SC_OK);
    assert(sc_native_ui_prepare(&native, &ui, &sc_catalog, 123) == SC_OK);
    memset(&driver, 0, sizeof(driver));
    calls.context = &native; calls.launch = launch; calls.draw = draw; calls.leave = leave;
    assert(sc_ui_driver_bind(&driver, &ui, &request, &calls) == SC_OK);
    assert(native_choose(1) == SC_OK);
    assert(native_choose(3) == SC_OK && native_choose(5) == SC_OK && native_choose(1) == SC_OK);
    memset(&starter, 0, sizeof(starter)); starter.species = 1; starter.level = 6;
    assert(sc_make_mon(&sc_catalog, &starter, 0x8889, &ui.state.party[0]) == SC_OK);
    ui.state.party[0].exp = sc_xp(7, ui.state.party[0].growth) - 1;
    for (i = 0; i < (unsigned)ui.list.count; i++)
        if (!strcmp(ui.labels[i], "OaksLab")) region = ui.ids[i];
    for (encounter = 0; encounter < sc_catalog.count; encounter++)
        if (!strcmp(sc_catalog.encounters[encounter].id, "OaksLab/rival-lab")) break;
    assert(region && native_choose(region) == SC_OK && native_choose(encounter) == SC_OK);
    controller.buttonPressed = RL_UI_DOWN;
    assert(sc_native_ui_input(&native, &driver) == SC_OK && ui.list.cursor == 1);
    files = (BinArchiveFile *)((u8 *)icons + sizeof(*icons));
    pixels = files[0].unk_08; files[0].unk_08 = NULL; missing_file = 0;
    controller.buttonPressed = RL_UI_A;
    assert(sc_native_ui_input(&native, &driver) == SC_UNAVAILABLE);
    assert(!driver.launched && !ui.request_ready && ui.screen == SC_UI_CONFIRM);
    files[0].unk_08 = pixels; missing_file = -1;
    launch_status = SC_OK;
    assert(native_choose(1) == SC_OK && driver.launched);
    assert(rl_ui_story_icon_cached(1));
    main_pool_push_state(0); child = pool.mainState;
    before_alloc = allocation_calls; before_reads = reads; before_writes = writes;
    memset(party, 0, sizeof(party)); party[0].hp = ui.state.party[0].cur_hp;
    for (i = 0; i < 4; i++) party[0].pp[i] = ui.state.party[0].pp[i];
    assert(sc_ui_driver_observe(&driver, request.operation_id, party, 0, 0, 0) == SC_OK);
    assert(ui.screen == SC_UI_LEARN && sc_native_ui_draw(&native, &ui) == SC_OK);
    files = (BinArchiveFile *)((u8 *)icons + sizeof(*icons));
    pixels = files[0].unk_08; files[0].unk_08 = NULL;
    assert(sc_native_ui_draw(&native, &ui) == SC_UNAVAILABLE);
    controller.buttonPressed = RL_UI_A;
    assert(sc_native_ui_input(&native, &driver) == SC_UNAVAILABLE && ui.screen == SC_UI_LEARN);
    files[0].unk_08 = pixels;
    unrelated_scope.prev = NULL; pool.mainState = &unrelated_scope;
    assert(sc_native_ui_draw(&native, &ui) == SC_UNAVAILABLE);
    assert(sc_native_ui_input(&native, &driver) == SC_UNAVAILABLE);
    pool.mainState = child;
    assert(sc_native_ui_finish(&native, &driver, request.operation_id, SC_WIN) == SC_UNAVAILABLE);
    assert(sc_native_ui_release(&native, &driver, 1) == SC_INVALID);
    assert(native_choose(4) == SC_OK && native_choose(1) == SC_OK && ui.state.phase == SC_BATTLE);
    assert(sc_native_ui_draw(&native, &ui) == SC_OK);
    assert(allocation_calls == before_alloc && reads == before_reads && writes == before_writes);
    main_pool_pop_state(0);
    assert(sc_native_ui_finish(&native, &driver, request.operation_id, SC_WIN) == SC_OK);
    assert(native_choose(2) == SC_OK && native_choose(1) == SC_OK && ui.screen == SC_UI_REGIONS);
    assert(native_choose(3) == SC_OK && native_choose(2) == SC_OK && driver.exit_requested);
    assert(sc_native_ui_release(&native, &driver, 1) == SC_OK);
    assert(sc_native_pak_release(&pak) == SC_SAVE_OK && !allocation_count);
    puts("Child-scope learning: actual progression fixture, cached icons, no allocations/Pak I/O, ancestry and drain guards; OK");
}

int main(void) {
    ScUiDriverCalls calls;
    backend();
    graphics_failures();
    fixture(); reset_binding();
    scope_error = 1;
    assert(sc_native_ui_acquire(&native, &pak) == SC_UNAVAILABLE && !depth);
    scope_error = 0; ui_error = 1;
    assert(sc_native_ui_acquire(&native, &pak) == SC_UNAVAILABLE && !depth);
    ui_error = 0;
    assert(sc_native_ui_acquire(&native, &pak) == SC_OK && depth == 1);
    assert(sc_native_ui_acquire(&native, &pak) == SC_INVALID);
    assert(sc_native_ui_prepare(&native, &ui, &sc_catalog, 123) == SC_OK);
    assert(sc_native_ui_prepare(&native, &ui, &sc_catalog, 999) == SC_UNAVAILABLE);
    calls.context = &native; calls.launch = launch; calls.draw = draw; calls.leave = leave;
    assert(sc_ui_driver_bind(&driver, &ui, &request, &calls) == SC_OK);
    assert(sc_ui_init(&unrelated_ui, &sc_catalog, NULL, 456) == SC_OK);
    unrelated_driver = driver;
    unrelated_driver.ui = &unrelated_ui;
    unrelated_driver.exit_requested = 1;
    assert(sc_native_ui_release(&native, &unrelated_driver, 1) == SC_INVALID);
    assert(native.prepared == &ui && native.acquired && depth == 1 && !driver.exit_requested);
    unrelated_driver.ui = &ui;
    ui.device = NULL;
    assert(sc_native_ui_release(&native, &unrelated_driver, 1) == SC_INVALID && depth == 1);
    ui.device = &pak.device;
    controller.buttonDown = RL_UI_A; controller.buttonPressed = 0;
    assert(sc_native_ui_input(&native, &driver) == SC_OK && ui.screen == SC_UI_MENU);
    controller.buttonPressed = RL_UI_DOWN;
    assert(sc_native_ui_input(&native, &driver) == SC_OK && ui.list.cursor == 1);
    unrelated_driver = driver;
    unrelated_driver.exit_requested = 1;
    assert(sc_native_ui_release(&native, &unrelated_driver, 1) == SC_INVALID);
    assert(native.driver == &driver && native.prepared == &ui && depth == 1);
    pak.binding_id++;
    assert(sc_native_ui_input(&native, &driver) == SC_UNAVAILABLE);
    assert(sc_native_ui_release(&native, &driver, 1) == SC_INVALID);
    assert(native.driver == &driver && native.prepared == &ui && depth == 1);
    pak.binding_id--;
    assert(sc_native_ui_finish(&native, &driver, 1, SC_WIN) == SC_INVALID);
    assert(sc_native_ui_release(&native, &driver, 0) == SC_PENDING && depth == 1);
    assert(sc_native_ui_release(&native, &driver, 1) == SC_PENDING && depth == 1);
    assert(sc_native_pak_begin(&pak) == SC_SAVE_OK);
    assert(sc_native_ui_release(&native, &driver, 1) == SC_INVALID && depth == 1);
    assert(sc_native_pak_end(&pak) == SC_SAVE_OK);
    main_pool_push_state(0);
    assert(sc_native_ui_draw(&native, &ui) == SC_UNAVAILABLE);
    assert(sc_native_ui_release(&native, &driver, 1) == SC_INVALID);
    main_pool_pop_state(0);
    controller.buttonPressed = RL_UI_DOWN;
    assert(sc_native_ui_input(&native, &driver) == SC_OK && ui.list.cursor == 2);
    controller.buttonPressed = RL_UI_A;
    assert(sc_native_ui_input(&native, &driver) == SC_OK && driver.exit_requested);
    assert(sc_native_ui_release(&native, &driver, 1) == SC_OK && !depth);
    assert(sc_native_ui_release(&native, &driver, 1) == SC_INVALID && !depth);
    assert(sc_native_ui_draw(&native, &ui) == SC_UNAVAILABLE);
    assert(sc_native_pak_release(&pak) == SC_SAVE_OK);
    assert(locks == unlocks && frames == 3 && logs);
    child_learning();
    puts("Story native resource adapter: real backend with synthetic OS/Pak/font outcomes; OK");
    return 0;
}
