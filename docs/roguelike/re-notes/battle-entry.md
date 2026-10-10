# Stadium 1 US battle entry, teams and roster archive (W3)

## Scope and provenance

These findings refer to the US Stadium 1 ROM with MD5
`ed1378bc12115f71209a77844965ba50`. The compact 24-byte roster records in
`oldnotes/utils/extractrostertbltbl.c` / `oldnotes/stadiumgs/` are Stadium 2
notes, **not** this game's format. Stadium 1 uses expanded 0x54-byte mons.
Archive counts, labels, species, levels and moves below were checked against
the local baserom using `tools/rl_extract_rosters.py`. Native live battle fields
are documented separately in [battle.md](battle.md).

## Entry and return flow

`Game_Thread` in `src/29BA0.c` dispatches `gCurrentGameState`, then records the
completed state in `gLastGameState`. Normal battles use:

```text
mode-specific preparation menus
  -> fragment63(0 or 1): VS introduction and opponent roster loading
  -> fragment64: choose battle mons, heal native roster, copy selection
  -> push FIGT/STAD -> load fragment34 -> fragment62(0, &D_800AE540)
  -> pop FIGT/STAD -> fragment63(2): results / rematch / progression
  -> repeat or return to the enclosing menu
```

| Entry | Setup / menus | Return |
|---|---|---|
| Stadium, `func_80029BC0` | 59 cup selection -> 60 class/round -> 54 rules/rental/registration -> 61 party selection | Stadium / area / title according to results |
| Free Battle, `func_8002A06C` | 58 rules -> 54 -> 61 | Area or rules/menu retry |
| Castle, `func_8002AAA8` | 54 -> 65 gym selection -> 61 -> 63(1) | Area; fragment40 for Castle clear, flags below |
| Mewtwo, `func_8002A400` | 54 -> 61 | Area; unlock/clear flags |
| Battle Now, `func_8002ADE8` | `func_8002C394`: two predefined teams -> 63(0) -> 64 -> battle -> 63(2) | `STATE_MENU_SELECT` |
| Event, `func_80029884` / `func_8002AF38` | fragment38 builds custom teams, returns `STATE_BATTLE_FROM_EVENT`; 63(0) -> 64 -> battle -> 63(2) | `STATE_EVENT_BATTLE` |
| Fast, `func_8002B180` | `func_8002BC64` GB Pak teams; skips intro/selection/results | Title |

Fragment54 (including `func_82B00E84` and entry) is a menu/transition overlay; it does
not itself run the battle engine. Fragment61 enumerates rentals, registered
teams and GB Pak mons. Fragment64 commits the actual picked roster.
Fragment38's `func_82304C7C` / `func_82304D30` are existing custom event team
builders, not pointers into the ordinary trainer archive.

## Resident context immediately before battle

`D_800AE540` (`unk_D_800AE540`, `src/29BA0.h`) is resident main-segment BSS:

| Offset | Meaning |
|---|---|
| 0x0000/1/2/3 | mode / rules index / class-or-gym / battle number |
| 0x0004 | four 0x21C player records |
| 0x0874 | four 0x248 full roster / trainer metadata records |
| 0x1194 | two 0x28 battle sides |
| 0x11E4 | trainer-history selectors, initialized to 0xFF |
| 0x11EC | Free Battle AI difficulty |
| 0x11ED/EE/EF | time-limit configuration |
| 0x11F0 | Rival variant selector |
| 0x11F2 | Round 2: archive file offset +0x1F |
| 0x11F5/6 | entry / result navigation flags |

`func_8002B5EC(mode, rules, sub)` resets the context.
`func_8002B700(controller, trainer_id, name, class)` allocates a player
(controller -1 -> CPU). The implementation ignores its trainer-id argument:
set `player->unk_018` explicitly for CPU AI. Type `unk_000` is 1 for a human,
3 for CPU; bit 2 is the CPU test.
`func_8002B840(side, player)` attaches the player to the side.
`func_8002B888(player, mon)` appends to both:

* `player->unk_01C[6]`, count `unk_002`: actual battle team.
* `player->unk_214->unk_028`, count `unk_002`: full team before selection.
  Its decomp declaration says `[1]` but the backing 0x248 record holds six.

Native nickname is at mon+0x30, OT at +0x3B. Species+0 is National Dex
1..151; +1 is the GB internal species ID (`func_80022A38`). HP+2, status+5,
types+6/7, moves+9[4], EXP+0x10, stat EXP+0x14[5], packed DVs+0x1E,
packed PP+0x20[4], level+0x24, calculated stats+0x26[5].

`func_8002B924` constructs a baseline record; it does not fill moves.
`func_800224B8` supplies level EXP; `func_80022338` recalculates stats.
The stat table `D_80070F84` is **species-indexed**, while `D_80070FA0` for
types is species-minus-one-indexed. The former's field/STAT_* names are
misleading: the actual output order is HP/Atk/Def/Spe/Spc. Keep native packed
DVs unchanged; see W2 evidence before renaming anything.

## Rules and team-selection policy

There is no single verified struct containing arbitrary level caps, selection
count and clauses. `D_800AE540.unk_0001` is the shared native rules selector;
registration limits, team selection and combat clauses are separate logic.

`func_8420ACA8` / `func_8420AC00`, fragment61_2DFB60.c, filter GB teams:

| Rules | Levels | Species eligibility bit in `D_8006FEE8[species][8]` |
|---|---|---|
| 0,6,7,8 | 1..100 | 0x20 |
| 1,2,3 | 50..55 | 1 |
| 4 (Petit) | 25..30 | 8 |
| 5 (Pika) | 15..20 | 0x10 |

`func_84206A68` in fragment61_2DD710.c checks the three lowest eligible levels:
rules3 total <=155, rules4 <=80, rules5 <=50. Fragment64 selection copies picked
mons to `unk_01C`; `func_84802F7C` first calls `func_80022734` to fully heal and
clear status. This is deliberately skipped for roguelike persistence.
Fragment62 `func_8430128C` normally picks three CPU mons, except rules8 or
mode0/rules0 which use the whole team. Its AI reads `unk_018 & 0xFF` (mode0
instead reads `unk_11EC`). The roguelike supplies already selected battle
teams and bypasses this repick only while `gRlBattleActive`.

Native sleep/freeze/self-KO clauses are not represented by independent
user-configurable fields in this prototype. The launch API preserves the
native rules mode, does not claim to enforce registration level/species/total
limits on custom roguelike teams, and does not expose unsupported clause
toggles. The caller selects mons and supplies the intended rules index.

## Arena, camera and audio

Fragment62_2EA8E0.c `func_84301430` loads `stadium_models` entry
`D_84384350[mode]`, or `[rules]` for event mode0xA / special mode0x10.
The table starts `{0,1,2,3,4,5,6,7,0x11,1}`; index7 adds the gym sub-index.
Thus event mode keeps custom teams but can use cup/Castle/Mewtwo arenas.
Camera selection mirrors this in `D_843859E0`, fragment62_304060.c.
Battle launch initializes audio with the same `func_800367A0(0xA0,0xA,0)`
as Fast Battle; music/presentation remain native, not a separate custom
music selector.

## Winner and return

Fragment62 always returns 0, **not the winner**. `func_84300E88` writes
side `unk_1C`: 1 means that side won; equal flags imply draw.
W2's live controller result is 0 ongoing, 1 side0, 2 side1, 3 draw.
Do not retain overlay pointers after the BATL pool is popped.

After the frame runner ends, the guarded fragment62 hook copies authoritative
active live HP/status back to the original party record using W2's typed
accessors. Original moves/PP stay untouched (important for Transform).
`rl_start_battle` then reads resident party records, maps the player's selected
mons back to original input indices, and returns the chosen game state.

Native fragment63 results set `unk_11F6`: 1 exit, 0x20 back to rules,
0x40 title, 0x80 restart, 0x100/200/400 Castle progression/clear variants,
0x800 Castle retry, 0x1000 Surfing Pikachu gift, 2/8/0x10 unlock effects.
The roguelike skips this mode-specific progression/results menu.

## ROM roster and rental tables

`yamls/us/rom.yaml`: `late_assets_3`, ROM **0x898000** ("Trainer data").
`func_8002C128` opens it via `func_800044F4`; it only replaces opponents
for modes1..8, so event mode0xA preserves custom teams even with the VS intro.

Archive header: BE `u16 flags, u16, u32, u32 total_size, u32 file_count`;
US has 62 files. Each 16-byte entry holds relative offset, size, two words.
Actual roster files are uncompressed; the extractor also supports native
PERS-SZP/Yay0, relocation pairs and BSS zero-fill.

Trainer file: `u32 count`, then `count * 0x230` records. Record offsets:
name+0, class+0xC, trainer id+0x34 (`portrait<<8 | AI`), mon count+0x36,
six 0x54 mons starting at +0x38. Rental file: `u32 count`, then 0x54 mons.
Trailing alignment bytes are not records.

| R1 files | Contents / native selection |
|---|---|
| 0 / 1 | Petit / Pika (modes4/5), eight trainers each |
| 2..5 | Prime Pokeball/Great/Ultra/Master (mode6, sub0..3) |
| 6 / 7 | modes1/2 aliases of Pokeball/Great Poké Cup data |
| 8..11 | Poké Pokeball/Great/Ultra/Master (mode3, sub0..3) |
| 12..19 | Brock, Misty, Surge, Erika, Koga, Sabrina, Blaine, Giovanni gyms; four trainers per gym |
| 20 | Lorelei, Bruno, Agatha, Lance |
| 21 | Seven Rival variants (selected by `unk_11F0`) |
| 22 | Mewtwo, one mon lv100 |
| 23 / 24 | Petit rentals (45 lv25) / Pika rentals (91 lv15) |
| 25 | Prime/Mewtwo rentals (149 lv100; R2 150 including Mew) |
| 26..28 | Poké/Free Battle rentals (149 lv50), rules0/1,2,3 |
| 29 | Battle Now BLUE/RED; R2 GREEN/YELLOW |
| 30 | Castle rentals (149 lv50) |

Round2 is file+31, including aliased rental files. The extractor emits 48
trainer sets and 14 rental sets with unique IDs, preserving archive order.
It does not assume alias files have distinct data.

## Generated data and API use

`make rl-rosters` regenerates `build/rl_gen/rl_rosters.{json,h}` from the
local baserom; never commit these generated files. Define `RL_ROSTERS_DEFINE`
in exactly one translation unit to instantiate the C tables; others include
the header for declarations/IDs. `RlGenMon` stores species, level, moves,
DVs and stat EXP; consumers use the native adapter to recalculate scaled stats.
The roguelike build generates the same files under `build_rl/rl_gen/`.
`rl_prepare_mon(mon,full_heal)` fills base stats, computed `current_stats`,
growth, HP and PP limits
after generation/scaling; full_heal=0 preserves state, 1 restores HP/PP/status.
`rl_load_rental_pool` and `rl_load_trainer` in `rl_rosters.h` materialize generated
sets as ready-to-use canonical `RlMon` arrays, including native base stats and
PP limits. The trainer helper also returns the archived AI byte. The generated
table definitions live in one roguelike-only translation unit, `rl_rosters.c`.

`src/roguelike/rl_battle.h` includes W5's canonical `rl_core.h`.
`rl_start_battle(party,n,enemy,m,rules,return_state,out)` is synchronous on
the game thread. `use_state=1` preserves player HP/PP/status and excludes
fainted mons. Zero DVs/stat EXP are literal. `player_count=0` uses all usable
mons; otherwise the caller's first N usable mons are selected. Enemy teams
are supplied already selected, fresh/full healed. Results contain original
player indices, not species-based matching.

Both stat arrays use HP/Atk/Def/Spe/Spc, despite the native field/enum names.
Host adapter tests link the actual `GetPokemonDV` from `src/22630.c` and
check asymmetric `0x1234`: HP10, Attack1, Defense2, Speed3, Special4.

Runtime proof: the CPU-vs-CPU fixture completes two consecutive battles with
three actual KOs each, native winner flags, and Title state2 return. Player
The damage fixture gives each enemy Quick Attack98, whose priority allows real
native enemy attacks before Psychic KOs. It requires HP loss in each battle
and enemy PP debit, then compares battle2 initial HP/PP with the actual battle1
result. The observed HP sequence is 72 -> 69 -> 64, Psychic PP9 -> 6 -> 3,
with paralysis0x40 and relic2 retained. Damage/misses remain native RNG-driven;
the assertions fail explicitly if no damage occurs rather than silently
accepting an unchanged-HP run.
The bench duplicate retains HP73/PP10/status0x40/relic3, and selected original
indices remain [1,2]. Opponent HP0/fainted flags are captured after each battle.
Run `python3 -m tools.rltest.test_custom_battle`; its ignored JSON evidence
contains both live initial states and complete persisted results.

Two required native setup repairs explain the former hangs. The direct event
route must still call `func_843831A0` to initialize AI profiles, team caches and
side pointers. Only its subsequent repicking is bypassed for a custom team;
rule argument0 uses actual team counts rather than six-roster/three-selected
assumptions. The fixture uses verified native profile11, not the earlier
unverified profile64.

Animation geometry command8 supplied callback0x81000130, pointing into the
original common import archive (fragment0). Its jump instruction targeted the
exact vanilla start of `func_8434D890`. After fragment62 growth that original
address landed inside `func_8434D5EC`, so animation loader OSThread7 never
finished. The typed archive R_MIPS_26 repair maps that original function start
to its final linked symbol, then the normal native fragment relocation maps
the virtual target to loaded RAM. Both archive load routes, `func_800043BC`
and `func_80019128`, require the repair. No arbitrary asset-word scan is used.

Invalid requests return -1 with `osSyncPrintf` and leave game state/context
unchanged. `gRlBattleActive`, `gRlBattleCount`, `gRlLastBattleResult` are
resident instrumentation for W1; result codes are 0 loss, 1 win, 2 draw.
State0x31 is a guarded CPU-vs-CPU fixture, distinct from W0's state0x30:
three lv100 Mewtwo/Psychic (original slot0 fainted and excluded, slot1/2
persisted HP72/73, PP9/10, paralysis) vs three lv5 bug mons/Quick Attack, returning
to Title. Enemies use Quick Attack rather than the former Tackle fixture,
so the scenario requires actual damage as well as completion. Duplicate species
exercise original-index identity, not matching by
species. The scenario asserts input state was not healed, win/result counts,
original indices, post-battle HP/PP/status/relic/fainting, consecutive-battle
carry-in and chosen-state return. Only launch requests are injected; no action,
HP, winner or battle phase is forced.
Set `gRlBattleTestRequest=1` and exit the current native overlay (e.g. START
from Title) to dispatch it safely; writing `gCurrentGameState` while an overlay
is running may be overwritten by that overlay's return.
Request2 uses the previous result's original-index mapping to restore selected
mons to the same three-slot party before launching again.
