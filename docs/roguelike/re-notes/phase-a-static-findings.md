# Phase A static RE findings (unverified dynamically unless noted)

Collected from read-only investigation by W1/W2/W4. Verify with the emulator harness before relying on them.

## Overlay symbol resolution (W1)
- Fragment-space addresses 0x81000000-0x8FFFFFFF: `gFragments[index]` where `index = ((addr & 0x0ff00000) >> 20) - 0x10`.
  fragment62 symbols at 0x843xxxxx map to index 0x33 (51).
- Battle Now flow in `func_8002ADE8` runs fragments 63 -> 64 -> 62; main state stays 0x28 (STATE_BATTLE_NOW_1P),
  so battle detection must check fragment62 loaded and `D_84390010[0/1]` initialized, not state alone.

## Battle engine, fragment 62 (W2)
### Sides / live mons
- `D_84390010` (0x84390010) = two side pointers. Live mon = side+0x68C (type `unk_D_800FCB18`, src/3D140.h).
- Live mon offsets: species +0x0B, current HP +0x0C (u16), status +0x15, moves +0x1F[4], level +0x26, maxHP +0x28 (u16),
  packed PP +0x32[4] (mask 0x3F), stat stages +0x5C[6] (neutral 7).
  Absolute from side: HP side+0x698, PP side+0x6BE.
- Live Atk/Def/Spe/Spc +0x2A/+0x2C/+0x2E/+0x30; unmodified copies +0x38/+0x3A/+0x3C/+0x3E
  (func_843700F0 phys/special dispatch, func_8436FC7C speed order, func_84370B0C paralysis speed/4, func_84370B44 burn atk/2).
- Current party pointer side+0x724; active index side+0x65C. Party records party+0x1C + index*0x54:
  HP +0x02, status +0x05, moves +0x09, packed PP +0x20. Current/incoming indices unk654.unk08/unk18; trainer selectors unk2B/unk2C.
- Expanded party stat offsets 0x26/28/2A/2C/2E = HP/Atk/Def/Spe/Spc by combat use (src/22630.h STAT_* names conflict; do not rename without confirmation).

### Lifecycle
- Copy-in: `func_84370B7C` (NON_MATCHING, fragment62_359F90.c); both started via `func_8430D814` (2F74E0); switches reuse copy-in via `func_84373570`.
- Prep: CalculateStatValue + func_80022338 rebuild stats; func_80022734 heals/restores PP/clears status -> persistence must load AFTER prep healing.
- Safest restore: before native copy-in; an after-copy hook must recompute burn/paralysis penalties.
- Live HP is authoritative; party HP is animated by func_8431FCCC (fragment62_309ED0.c) — don't copy mid-animation.
- func_843706E8 debits PP on live and original except transformed mons: preserve original moves/PP at end.
- End hook: after func_800078D4 frame runner, before main_pool_pop_state('BATL') in func_84301A2C. Overlay pointers invalid after.
- Result: `D_84390240->unk00 + 0x40`: 0 ongoing, 1 side0 wins, 2 side1 wins, 3 draw. func_84300E88 persists win flags to
  D_800AE540.unk1194[side].unk1C. Fragment 62 entry always returns 0.
- Frame: func_843010EC -> 84300E88 -> 8432AEE4 -> 8432ADD8 -> ordinary battle 84328734.
  Winner funcs 84321F2C/84321F74; surrender 8430E1CC; timeout 8430CE00.

### Combat
- Damage: func_843700F0 (stages/power/atk/def/level), func_84370260 base formula (cap 996, +2), func_84371974 STAB/type,
  func_8436FC10 random 217..255, func_843703BC saturating HP/substitute commit (D_843C4DC4 u16 accumulator).
- Crit func_84371C18 sets D_843C4DA5 after func_8436F6D8 RNG compare. Accuracy func_84370790 stage scaling, func_843708CC compare via func_8436F788.
- Gameplay RNG: time-advanced LCG/LFSR streams (not only MathUtil_Random16). AI separate: func_843767E4.
- func_8437345C = one action; calls func_84372670 pipeline (NON_MATCHING) and func_8437176C residuals.
- Residual poison/burn func_843715C0: pendingHP +0x0E = maxHP/16 (min 1, Toxic unk51 multiplier); seed func_8437166C pendingHP +0x10.
  Presentation funcs 84326E84/84327720 commit pending HP. Recoil commit func_8432734C (fragment62_309ED0.c).
- Faint = live HP 0. func_843732B8/84373018/84372C40/84372D88/84372ED0: replacement state 0x11 vs exhausted 0x12. Party unk25 = faint-order marker.
- Whole-turn transition still needs tracing around 84320C38/843208B0 + unk34 action counter.
- Hook cautions: damage hook at final commit must gate misses/scratch D_84390288 and decide fixed-damage/Substitute scope; stat hook must preserve unmodified fields.

## UI / text (W4)
- Native font: src/1CF30.c `func_8001E94C(0x1C,0)` loads fonts; begin `func_8001F3F4`, face `func_8001EBE0(8,0)`, color `func_8001F324`,
  printf draw `func_8001F1E8` (256-byte buffer), end `func_8001F444`. Safe raw text: loop bytes through `func_8001ECB4`, handle newline.
- Encoding: ASCII + E9 é, A9 ♂, BE ♀, A5 ¥, B7 ·, D7 ×.
- Window/texture: in 1CF30 — `func_8001F504` self-contained rect primitive; `func_8001C6AC` RGBA16 texture.
  Fragment 38: 82300020 animated cursor corners (common_menu2 seg2), 82300214 border, 8230047C panel; D_8006F518/630 texture state begin/end.
- Mon icons 40x40 RGBA16: 1AB70 func_8001B058/8001B088 use hardcoded ROM 0x820000 — mod must use linker pokemon_icons_ROM_START/END with
  func_800044F4 and func_8000484C(species-1), arg3=0 caching. Declare symbols locally. Cached textures are scene-pool-owned through DL execution.
- Scene skeleton (fragment 37, 82202DC4): push pool, func_80005E40(0x10000,0), func_80007444(1,0,2,0,2,1) 640x480, load fonts/assets,
  func_80007678 activate; per frame: controller read/update, func_800079C4 + draw + func_80007778 submit; exit func_800076C0/func_80005EAC/pop.
- Sounds func_80048B90: 1 move, 2 A, 3 B, 0x1D disabled.
