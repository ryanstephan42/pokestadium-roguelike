# Battle fragment 62

## Verification status

Static US source and matching assembly evidence is identified below. **Dynamic
HP/PP validation passed** with an actual Body Slam hit; measurements are below.
No runtime hooks or vanilla renames are introduced by this work. Typed views
are in `include/rl_battle.h`; they alias existing native types instead of
introducing potentially incompatible overlay struct definitions.
Run `bash tools/rltest/run_battle_layout.sh` for the native 32-bit ABI offset
and accessor regression checks (requires gcc multilib). These are not emulator
tests. The initial vanilla gate printed `build/pokestadium-us.z64: OK` and
`GATE PASSED`; roguelike/host/harness stages were absent and skipped at that time.

## Object and field map

Fragment 62 uses logical VRAM `0x84300000`. Logical overlay symbols are
**not physical RDRAM addresses**. Resolve them through the fragment map
(`src/memmap.c`, `Memmap_GetFragmentVaddr`); fragment map index is 51.
`Memmap_GetLoadedFragmentVaddr` performs the reverse mapping, not resolution.
Use the current build's symbols for a shifted ROM. Follow loaded pointers;
never blindly subtract `0x80000000` from a `0x843xxxxx` logical address.

| Object | US logical symbol | Evidence |
| --- | --- | --- |
| Two side pointers | `D_84390010`, `0x84390010` | `fragment62.h`, `fragment62_2EC3C0.c` |
| Live mon | `side + 0x68C` | `unk_D_800FCB18`, `src/3D140.h`, size `0x64` |
| Current party pointer | `side + 0x724` | `unk_D_800AE540_0004*`; records at `party + 0x1C`, stride `0x54` |
| Controller pointer | `D_84390240`, `0x84390240` | `unk_D_84390240.unk_00`, allocated by `func_843297AC` |
| Damage accumulator | `D_843C4DC4`, `0x843C4DC4` | `u16`, `fragment62_359F90.c` |
| Attacker / defender pointers | `D_843C5238` / `D_843C523C` | Per-action scratch, not permanent player/enemy |
| Critical flag | `D_843C4DA5`, `0x843C4DA5` | Per-action scratch |
| Resident setup | `D_800AE540`, `0x800AE540` | Two side descriptors at `+0x1194`, stride `0x28` |

The live mon offsets below are relative to `side + 0x68C`.

| Offset | Native field / type | Meaning and evidence |
| --- | --- | --- |
| `0x0B` | `unk_0B`, `u8` | Species, matching copy-in assembly |
| `0x0C` | `unk_0C`, `u16` | Current HP, saturating damage in `func_843703BC` |
| `0x0E/10/12` | `unk_0E/10/12`, `u16` | Pending HP after status/seed/recoil; NOT normal stats |
| `0x14` | `unk_14`, `u8` | Party-index copy at init |
| `0x15` | `unk_15`, `u8` | Major status: sleep low 3 bits, poison `0x08`, burn `0x10`, freeze `0x20`, paralysis `0x40` |
| `0x1C/1D` | `unk_16[6/7]`, `u8` | Types used in `func_84371974` |
| `0x1F` | `unk_1F[4]`, `u8` | Live move IDs, including temporary replacements |
| `0x24` | `unk_24`, `u16` | Packed DVs |
| `0x26` | `unk_26`, `u8` | Level, damage formula |
| `0x28` | `unk_28`, `u16` | Max HP, residual fractions and HP animation |
| `0x2A/2C/2E/30` | `u16` | Effective Attack/Defense/Speed/Special |
| `0x32` | `unk_32[4]`, `u8` | Packed PP: low 6 bits remaining, high 2 PP Ups |
| `0x38/3A/3C/3E` | `u16` | Unmodified Attack/Defense/Speed/Special |
| `0x5C..61` | `unk_5C[6]`, `u8` | Attack/Defense/Speed/Special/accuracy/evasion stages, neutral 7, range 1..13 |

There is no separate canonical persistent fainted boolean: **live HP zero is
fainted**. Side action state `unk_654.unk_2D` distinguishes replacement `0x11`
and exhausted party `0x12`; presentation advances these to other states.
Expanded party `unk_25` is a faint-order marker, cleared by `func_84300D44`
and assigned by `func_84300DC0` when animated HP reaches zero.

### Active indices and party representation

`unk_D_84390010_654.unk_08` is the current index (`side+0x65C`, `s32`);
`unk_18` is incoming index (`side+0x66C`, `u32`). `unk_2B`/`unk_2C`
are current/incoming trainer selectors (`side+0x67F/680`, `u8`).
`unk_0C` is move slot (`side+0x660`, `s32`).
`func_84308A04` (`fragment62_2F23A0.c`) commits incoming index/trainer
and current party pointer. Do not equate incoming and current selectors
during a switch. Standard single-trainer battles use player side 0 and enemy
side 1; multiplayer additionally requires the trainer selector.

Expanded party type `unk_func_80026268_arg0` (`src/29BA0.h`, size `0x54`):
species `+0x00`, current HP `+0x02`, status `+0x05`, original moves
`+0x09[4]`, packed original PP `+0x20[4]`, level `+0x24`, faint-order
`+0x25`, HP/Attack/Defense/Speed/Special `+0x26/28/2A/2C/2E`.

Compact input type `unk_D_800AE4E8_004_1_000_010_000` (`src/22630.h`,
size `0x21`) is expanded by `func_80021D9C`: species `+0x00`, big-endian
HP bytes `+0x01..02`, status `+0x04`, moves `+0x08..0B`, packed PP
`+0x1D..20`. `func_80022338` calculates level/stats through
`CalculateStatValue`; `func_80022734` then heals, clears status, and restores
PP while preserving PP Ups. Persisted values must be restored AFTER prep healing.

**Existing stat names conflict with engine use.** `src/22630.h` calls stat
IDs 2/3/4/5 Special/Speed/Defense/Attack. Native battle use is instead
Attack/Defense/Speed/Special: physical damage `func_843700F0`, ordering
`func_8436FC7C`, paralysis `func_84370B0C`, burn `func_84370B44`.
Roster stat experience order agrees (`oldnotes/utils/extractrostertbltbl.c`).
The species-table byte called `baseDefense` is used as base Speed for crits.
Vanilla ROM inspection at ROM `0x71B84 + species * 0x17 + 6` confirms the
five bytes are HP/Attack/Defense/Speed/Special: Bulbasaur (species 1)
`45/49/49/45/65`, Pikachu (25) `35/55/30/90/50`, Mew (151)
`100/100/100/100/100`. This is a static ROM check, not a live RAM observation.
The header's old stat names are therefore demonstrably inconsistent; no
global rename is needed for these independently named battle accessors.

## Lifecycle and execution evidence

All following fragment functions retain their address as their name.

| Function | Source under `src/fragments/62/` | Role |
| --- | --- | --- |
| `func_84301A2C` | `fragment62_2EA8E0.c` | Entry, BATL pool push, frame runner, teardown, returns 0 |
| `func_84301430` | same | Scene init and faint-order clear |
| `func_843010EC` / `func_84300E88` | same | Input/frame loop, side/controller update, draw, result handoff |
| `func_84307394` / `func_84307220` | `fragment62_2EC3C0.c` | Lifecycle dispatch; phase 3 calls starting-mon init |
| `func_8430D814` | `fragment62_2F74E0.c` | Both mon copy-in calls, animated HP init, per-action reset |
| `func_84370B7C` | `fragment62_359F90.c` | Party-to-live copy, status penalties, unmodified stats, stages |
| `func_84373570` | same | Switch-in reuses copy-in |
| `func_8432AEE4` / `func_8432ADD8` | `fragment62_313D60.c` | Controller dispatcher, ordinary battle calls `func_84328734` |
| `func_84320108` | `fragment62_309ED0.c` | Action order, invokes first action |
| `func_8437345C` | `fragment62_359F90.c` | ONE action, attacker/defender setup, attack and residual/faint calculations |
| `func_84372670` | same | Condition, PP debit, crit/damage/type/random/accuracy/effect pipeline |
| `func_8431FCCC` | `fragment62_309ED0.c` | Animates party HP toward live HP via two floats `D_843900A8` |

`func_84370B7C` and `func_84372670` are NON_MATCHING C bodies.
Matching assembly `asm/us/nonmatchings/fragments/62/fragment62_359F90/func_84370B7C.s`
confirms HP load from record+2/store side+0x698, status store side+0x6A1,
moves side+0x6AB..6AE, and packed PP stores side+0x6BE..6C1.
Its stage-init loop writes eight bytes, although only six are declared stages
and the last two are padding. The typed view exposes only the six proven stages.

### Damage and RNG

`func_843700F0` prepares power, Attack/Defense or Special, and level.
Types below `0x14` are physical; it applies screens and critical bypass.
`func_84370260` computes integer base damage, caps intermediate at 996,
then adds 2; special effects can bypass this formula.
`func_84371974` applies STAB/type effectiveness. `func_8436FC10` draws
bytes until >=217 and scales damage by byte/255.
`func_843703BC` applies damage to Substitute (`unk_4D & 0x10`,
HP `unk_59`) or saturates current HP at zero. Scratch mode `D_84390288`
suppresses normal HP commits.

`func_84371C18` computes crit threshold from base Speed byte, rule mode,
Focus Energy, and high-crit moves; compares `func_8436F6D8` and sets
`D_843C4DA5`. `func_84370790` scales accuracy by stage indices 4/5;
`func_843708CC` compares `func_8436F788`, rerolling 255, with exceptions.
`func_843708A0` marks a miss and clears damage.

These gameplay RNG streams are time-advanced LCG/LFSR combinations using
`osGetCount`, not merely `MathUtil_Random16`. AI has separate
`func_843767E4` (`fragment62_361050.c`). Run seed alone does not ensure
deterministic battle replay. Stage math `func_84381BD4` in that file uses
unmodified stat values; crits bypass normal stage-adjusted values.

### Residuals, faint, result, teardown

`func_843715C0` computes poison/burn pending HP `unk_0E`, maxHP/16
(minimum 1), multiplied by Toxic counter `unk_51` when applicable.
The Toxic multiplier gate is `unk_4E & 1`; `unk_4D & 0x80` marks Leech Seed.
`func_8437166C` computes seed pending HP `unk_10` and opposing healing.
`func_8437176C` runs after an action. Native cadence is per-action, not
automatically a modern whole-turn callback. `func_84326E84/84327720`
commit status/seed HP through animation; `func_8432734C` commits pending
recoil HP `unk_12`; `func_84327B90` commits another pending-status path.

`func_843732B8` checks current and pending HP zero;
`func_84373018/84372C40/84372D88/84372ED0` sum remaining team HP to
distinguish replacement from exhaustion. `func_84373160` checks status faint.

Controller `unk_40` (`controller+0x40`, `s32`) is 0 ongoing, 1 side0 win,
2 side1 win, 3 draw/fallback termination. Win assignments:
`func_84321F2C/84321F74/84322284`; surrender `func_8430E1CC`;
timeout `func_8430CE00` compares unfainted count then HP bands.
`func_84300E88` sets resident
`D_800AE540.unk_1194[winner].unk_1C = 1`.
Fragment 63 consumes these flags (`fragment63_394120.c`).
**Entry return is always 0**, not the result. `src/29BA0.c:func_80029BC0`
calls fragment 63 with argument 2 after battle.

`func_84301A2C` stops the frame runner, destroys rendering allocations,
and pops BATL pool. Capture data before rendering teardown/pool pop;
all live/controller pointers expire after pool release.

## Precise hook proposals (not inserted)

* Start: restore original selected records after prep healing and before
  `func_8430D814`'s copy-in calls. An after-init `rl_hook_battle_start()`
  belongs after both copies but before animated HP init; it must update both
  representations and recompute burn/paralysis effective stats. Never call
  battle-start again on the switch path.
* End: `rl_hook_battle_end(result)` after `func_800078D4` returns in
  `func_84301A2C`, before rendering teardown/BATL release. Copy settled live
  HP/status to current original record, then all selected original records
  into resident run state. Preserve selection-to-run-slot identity, including
  duplicate species. Derive faint from HP zero.
  `func_843706E8` debits original PP except transformed mons (`unk_4E & 8`);
  do NOT overwrite original moves/PP with transformed live values.
* Damage: `rl_hook_modify_damage(...)` immediately before appropriate
  commit in `func_843703BC`, passing attacker/defender/move/type/crit/damage.
  Explicitly gate misses, simulation mode and repeated calls. Decide fixed
  damage/Substitute scope; hooking only base formula misses special effects.
* Stat: after copy-in/status penalties and unmodified-stat capture in
  `func_84370B7C`, update effective and unmodified stats coherently. Audit
  stage recalculation and critical bypass. Global `CalculateStatValue`
  modification affects non-battle screens.
* Whole-turn: do not hook frame loop or assume `func_8437345C` is whole-turn.
  `func_8432079C` invokes the next action and sets controller `unk_34=1`;
  `func_843208B0` ends second-action progression and returns phase `unk_38=4`.
  Finish dynamic transition coverage, including switches/faints/charging moves,
  before choosing a once-per-turn `rl_hook_end_of_turn(...)` point.

All eventual calls must be guarded by ROGUELIKE and actual run-battle ownership.
Empty calls at unproven transitions would conceal missing behavioral evidence.

## Dynamic test recipe

Resolve the loaded side-pointer table and validate pointers. Record live HP at
`side+0x698` (big-endian u16), maxHP `side+0x6B4`, packed PP
`side+0x6BE..6C1`, and species `side+0x697`. Follow party pointer `side+0x724`
and active index `side+0x65C` to original HP/PP offsets above.

Select a known damaging move with PP against a non-immune opponent. Verify
selected PP low six bits fall by one, PP-Up bits unchanged, opposing HP falls
on a confirmed hit, maxHP unchanged, and party HP eventually converges to live
HP after animation. Account for core word-swapped RAM; never use raw host casts.
Also cover miss, switching, residuals, faint, each winner, and teardown before
runtime integration. Never commit ROMs, savestates, or game screenshots.

### Emulator-verified observations

Used W1 frontend commit `3265d4d`, system
`/usr/lib/libretro/mupen64plus_next_libretro.so`, Angrylion software RDP,
Parallel RSP fallback, dynamic recompiler, and word-swapped RDRAM helpers.
Vanilla ROM MD5: `ed1378bc12115f71209a77844965ba50`; SHA256:
`502f6082a6436012a8b61419435dec1388869a90ed870e87e2d7bee88f831519`.
Core SHA256:
`1e5674de118b8cb3621a16402ecdd3d509591700c35e98943e8bb32906342263`.

Poke Cup Poke Ball battle against Biker, rental Bulbasaur level 50 versus
Mankey level 50. These are loaded addresses for this run only:

| Measurement | Address | Before frame 11950 | After frame 12520 |
| --- | --- | --- | --- |
| Player live HP | `0x802850F8` | 132 | 121 |
| Opponent live HP | `0x80285B18` | 112 | 9 |
| Player packed PP slots | `0x8028511E` | 10, 10, 15, 25 | 10, 10, 14, 25 |
| Opponent packed PP slots | `0x80285B3E` | 25, 20, 10, 15 | 25, 20, 10, 14 |
| Player/opponent max HP | `side+0x6B4` | 132 / 112 | 132 / 112 |

Player move IDs were 73, 92, **34 (Body Slam)**, 75. At frame 11962 the
CPU action had reduced player HP and CPU slot-3 PP. At frame 12520 the
player Body Slam debited slot-2 PP and reduced opponent HP by 103; opponent
status became paralysis (`0x40`). Both original-party HP values converged
to live HP after animation. PP-Up bits remained unchanged (all zero in
this fixture; nonzero PP-Up persistence is not dynamically established).
No RAM writes were used to induce the hit or satisfy assertions.

Reproduce with a local fixture (never commit it):

```
python3 -m tools.rltest.battle_probe
python3 -m tools.rltest.verify_battle --state build/rl-battle-probe/moves.state
```

The interactive probe writes its current screen only under ignored `build/`.
Wait until the title settles (`wait 180`), press START, then use **B** for
native A and **Y** for native B in this core. Enter Stadium, Poke Cup,
Poke Ball, Battle, Rental Only. Choose six rentals, confirming each details
dialog; include Bulbasaur, Ivysaur and Squirtle. Confirm the team and opponent,
select Bulbasaur first with Y and the other two with `c left` / `c up`.
Confirm with START and B; wait for the battle menu, open Attack with B,
settle its animation, then `save moves.state`. The verification command
selects Body Slam using `c down`, observes 2100 frames, and fails explicitly
if the move misses or the fixture is incompatible. Native time-based RNG
means fresh runs need not reproduce identical damage.

Numerical event/ROM/core metadata is written to
`build/rl-battle-verification/result.json`. This test needs the local fixture;
it is not a fresh-boot CI navigation test. Full-turn special-case cadence,
nonzero PP Ups, switching, transformed PP, each result and teardown remain
static evidence/proposals, not emulator-verified claims. No unproven hook
was inserted.
