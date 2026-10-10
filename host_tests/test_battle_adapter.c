#include <string.h>
#define ROGUELIKE
#include "src/roguelike/rl_battle.c"

#undef assert
#define assert(condition) do { if (!(condition)) return __LINE__; } while (0)

PokemonStats D_80070F84[152];
unk_D_80072B00 D_80072B00[165];

u32 func_800224B8(s32 species, u16 level) {
    return level * level * level;
}

void func_80022338(unk_func_80026268_arg0* mon) {
    mon->unk_24 = 50;
    mon->unk_26 = 200;
}

unk_func_80026268_arg0* func_8002B924(unk_func_80026268_arg0* mon, u8 species, s16 level) {
    mon->unk_00.unk_00 = species;
    mon->unk_10 = func_800224B8(species, level);
    mon->unk_26 = 200;
    return mon;
}

u8 func_80022A38(s32 species) {
    return 0x83;
}

void func_800228B0(unk_func_80026268_arg0* mon) {
}

int main(void) {
    RlMon source;
    RlMon returned;
    unk_func_80026268_arg0 native;

    memset(&source, 0, sizeof(source));
    source.species = 25;
    source.level = 50;
    source.moves[0] = 85;
    source.moves[1] = 45;
    source.max_pp[0] = 24;
    source.max_pp[1] = 61;
    source.pp[0] = 3;
    source.pp[1] = 9;
    source.cur_hp = 71;
    source.status = 0x40;
    source.exp = 125001;
    D_80072B00[84].unk_05 = 15;
    D_80072B00[44].unk_05 = 40;
    strcpy(source.nickname, "CUSTOM");
    assert(rl_valid_team(&source, 1));
    rl_to_native(&source, &native, 1);
    assert(native.unk_00.unk_01 == 0x83);
    assert(native.unk_02 == 71);
    assert(native.unk_05 == 0x40);
    assert(native.unk_1E == 0);
    assert(native.unk_14 == 0);
    assert(native.unk_20[0] == 0xC3);
    assert(native.unk_20[1] == 0xC9);
    assert(native.unk_20[2] == 0);
    assert(native.unk_10 == 125001);
    assert(strcmp((char*)native.unk_30, "CUSTOM") == 0);
    memset(&returned, 0, sizeof(returned));
    rl_from_native(&native, &returned);
    assert(returned.pp[0] == 3 && returned.max_pp[0] == 24);
    assert(returned.pp[1] == 9 && returned.max_pp[1] == 61);
    assert(returned.cur_hp == 71 && returned.max_hp == 200);
    assert(returned.status == 0x40 && !returned.fainted);
    native.unk_02 = 0;
    rl_from_native(&native, &returned);
    assert(returned.fainted);
    source.cur_hp = 300;
    source.pp[0] = 255;
    rl_to_native(&source, &native, 1);
    assert(native.unk_02 == 200);
    assert((native.unk_20[0] & 0x3F) == 24);
    rl_to_native(&source, &native, 0);
    assert(native.unk_02 == 200 && native.unk_05 == 0);
    assert((native.unk_20[1] & 0x3F) == 61);
    source.moves[0] = 166;
    assert(!rl_valid_team(&source, 1));
    assert(!rl_valid_team(&source, 7));
    assert(!rl_valid_team(NULL, 1));
    return 0;
}
