#include <stdio.h>
#include "src/story/sc_cold_load.c"

static unsigned available, pushes, pops, loads, depth, failures;
static int load_failure, entry_failure, recurse;
static u8 rom[256];
volatile ScProbeMailbox gScProbe;
int gScProbeActive;

#define check(value) do { if (!(value)) { failures = __LINE__; return -1; } } while (0)

void osSyncPrintf(const char *format, ...) { (void)format; }
u32 main_pool_get_available(void) { return available; }
u32 main_pool_push_state(u32 tag) {
    (void)tag; pushes++; depth++;
    return available;
}
u32 main_pool_pop_state(u32 tag) {
    (void)tag; pops++; depth--;
    return available;
}

static s32 entry(s32 unused, s32 pointer) {
    ScColdRequest *request = (ScColdRequest *)pointer;
    (void)unused;
    check(depth == 1 && calling);
    if (recurse) check(cold_invoke(request, rom, rom + sizeof(rom), 128) == 0x100 + SC_INVALID);
    return entry_failure ? 0x100 + SC_INVALID : SC_SAVE_OK;
}
ret_func_80004454 func_80004454(s32 id, u8 *start, u8 *end) {
    (void)start; (void)end;
    loads++;
    if (id != 0xe9) failures = __LINE__;
    return load_failure ? NULL : (ret_func_80004454)entry;
}

int main(void) {
    ScColdRequest request = {0};
    unsigned required = 256 + ALIGN16(0x18) + ALIGN16(sizeof(MainPoolState)) +
        sizeof(MainPoolBlock) * 2;
    available = required - 1;
    check(cold_invoke(&request, rom, rom + 256, 128) == 0x100 + SC_INVALID);
    check(!loads && !pushes && !pops && !calling);
    available = required;
    recurse = 1;
    check(cold_invoke(&request, rom, rom + 256, 128) == SC_SAVE_OK);
    check(loads == 1 && pushes == 1 && pops == 1 && !depth && !calling);
    load_failure = 1;
    check(cold_invoke(&request, rom, rom + 256, 128) == 0x100 + SC_INVALID);
    check(pushes == pops && !depth && !calling);
    load_failure = 0; entry_failure = 1;
    check(cold_invoke(&request, rom, rom + 256, 128) == 0x100 + SC_INVALID);
    check(pushes == pops && !depth && !calling);
    entry_failure = 0; recurse = 0;
    check(cold_invoke(&request, rom, rom + 256, 128) == SC_SAVE_OK);
    check(pushes == pops && !depth && !calling && !failures);
    check(!gScProbe.cold_busy && !gScProbe.cold_address &&
          gScProbe.cold_before == gScProbe.cold_after);
    gScProbeActive = 1;
    check(cold_invoke(&request, rom, rom + 256, 128) == 0x100 + SC_INVALID);
    check(!calling && !depth);
    puts("Story cold loader threshold/ownership/reentrancy/cleanup: OK (host only)");
    return 0;
}
