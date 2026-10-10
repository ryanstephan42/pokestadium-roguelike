# Pokémon Stadium: Roguelike — Feature & Implementation Plan

## 0. Ground truth (from recon)

| Fact | Evidence | Implication |
|---|---|---|
| Decomp is 93% C, builds matching ROM (`ed1378bc…`) | `progress.py`, `make` | We can mod in C, not asm patches |
| ROM layout is `__romPos`-relative; fragments are relocatable overlays with own reloc tables | `build/pokestadium-us.ld`, `src/memmap.c` | Non-matching builds can grow fragments; hardcoded ROM offsets in residual asm/data are the main shift risk |
| `NON_MATCHING=1 COMPARE=0` supported | `Makefile` | Mod build = `make ROGUELIKE=1 NON_MATCHING=1 COMPARE=0` |
| Game state machine: `gCurrentGameState` dispatcher | `src/29BA0.c:1338-1411`, `include/variables.h` | Add `STATE_ROGUELIKE` + dispatcher case |
| Battle system = fragment 62 (vram 0x84300000, 95% C, bss 0x37D20) | yaml:2225 | Hooks for HP/PP persistence + results live here |
| Battle prep/rules = frag 63; pre-battle menu = frag 64; rental/rules/registration = frag 54; event battle loader yaml:1643; GLC clear/gift yaml:1691 | yaml comments | Reuse for party injection & rule sets |
| Compact Pokémon record + stat calc (`func_80022338`, `CalculateStatValue`, `GetPokemonDV`) | `src/22630.[ch]`, `src/232C0.c` | Build/scale any mon in code |
| Roster/rental table formats documented | `oldnotes/utils/extractrostertbltbl.c`, `oldnotes/stadiumgs/rosters,rentals` | Enemy & recruit pools from real game data |
| RNG = `guRandom()` via `MathUtil_Random16` | `src/math_util.c:66` | Add separate seeded run RNG (xorshift32) |
| Input: `gPlayer1Controller->buttonPressed` | `src/controller.c` | Menu code is trivial |
| Menu patterns: frag 37/38 cursor tables; `HAL_Printf` text queue; `12D80.c` rect/texture draw | agents | New screens = new fragment modeled on frag37 |
| EEPROM r/w wrappers (8-byte blocks) + Controller Pak support | `controller.c:138-215`, frag48 | Meta-progression save |
| RetroArch + `mupen64plus_next` / `parallel_n64` libretro cores installed | `~/.config/retroarch/cores` | Headless automated testing harness is possible |

Lessons carried from Mina the Hollower: lean on structured exports/symbol maps over guesswork, keep
binaries/assets out of git, use an emulator run as ground truth, and keep generated output out of repo.

## 1. Game design

### Core loop
`Menu Select → "Roguelike Run" → Seed/Starter Draft → Act Map → node → (battle | event) → reward → … → Boss → next Act → Final Boss → Run Summary → Meta unlocks`

### Run structure
- **3 Acts + Finale**, themed on Stadium content:
  1. **Poké Cup Act** (L30–40 enemies, Stadium trainers) → boss: Gym Leader (random of 8, GLC roster)
  2. **Prime Cup Act** (L45–60) → boss: 2nd Gym Leader + Elite Four member
  3. **Elite Act** (L60–75) → boss: Elite Four member → Rival (Gym Leader Castle final)
  4. **Finale**: Mewtwo (VS Mewtwo team) — ascension unlock on clear.
- **Branching node map per act** (Slay-the-Spire style DAG, 7 rows × 2–4 lanes, generated from run seed):
  - ⚔ Trainer battle (common), ☠ Elite trainer (harder, better reward), ✚ Pokémon Center (heal 50% or revive one),
    🛒 Poké Mart (shop), ? Event, ◎ Recruit (draft a mon), ★ Boss.
- **Battle format**: Stadium rules — you bring up to 6, choose 3 per battle (configurable: "Full 6v6" at ascension).

### Party & persistence (the roguelike core)
- Start: draft **3 of 6** random rentals (from Poké Cup rental pool), plus one free reroll.
- **HP, PP, status, and fainting persist between battles.** Fainted mons stay fainted until revived (Center/Revive item).
- **Permadeath toggle** (Nuzlocke): fainted mons are released. Default off; on at Ascension ≥3.
- Party cap 6. Leveling: XP awarded per win → levels between battles (rebuilt via `func_80022338`).
- Run lost when all party members are fainted (or you lose a boss battle).

### Rewards (pick 1 of 3 after each battle)
- **Recruit** a mon from the defeated trainer's team (at their level).
- **Move tutor/TM**: replace a move from a learnset-legal pool.
- **Rare Candy ×N**, **Vitamin** (stat-exp boost), **Held-effect "Relic"**, **Money**.

### Relics (run-wide passive modifiers — the "badges")
~20 to start, implemented as hooks on stat calc / damage / turn-end / post-battle:
- Boulder Badge (+Def 10%), Cascade (Water moves +15%), Thunder (Speed +10%), Rainbow (end-of-battle heal 10%),
  Soul (status immunity first 2 turns), Marsh (Special +10%), Volcano (Fire +15%), Earth (+1 reward choice).
- Amulet Coin (+50% money), Exp. Share (bench gets 50% XP), Lucky Egg, Leftovers-Charm (heal 1/16 per turn),
  Focus Band-Charm (once per battle survive at 1 HP), Scope Lens-Charm (crit up), Silph Scope (see enemy team before choosing 3),
  PP Up Pouch (+PP), Old Amber (one-time Aerodactyl recruit), Master Ball (catch one boss mon).
- Cursed relics from events (power + drawback).

### Shop (Poké Mart)
Potions/Super/Hyper/Full Restore, Revive, Ethers/Elixirs, Full Heal, Rare Candy, vitamins, 1 relic slot, 1 TM slot.
Prices scale per act. Items used from a between-battle **Bag screen** (Stadium has no in-battle items in most cups —
keep it that way; item use is a strategic between-node decision).

### Events (text + choice, ~12 at launch)
Mt. Moon fossil (pick Kabuto/Omanyte), Game Corner (gamble money), Daycare (deposit a mon, collect +5 levels after 2 nodes),
Silph Co. (trade a mon for a random one 5 levels higher), Bill's PC (swap a party member with a random rental),
Lavender Tower (lose HP, gain Ghost relic), Safari Zone (pick 1 of 3 random wild mons), Prof. Oak (free Rare Candy or heal).

### Enemy generation & scaling
- Pull trainers from Stadium/GLC roster tables; level = act base + row×2 + ascension bonus; DVs/stat-exp scale with depth.
- Elites: +3 levels, max DVs. Bosses: fixed signature teams (GLC leaders, E4, Mewtwo) with scaled levels.

### Meta-progression (saved)
- Unlock more starter pools (Prime Cup rentals, Little Cup), new relics, new events, **Ascension 1–10** modifiers
  (enemy level +, fewer Centers, permadeath, 6v6, less money…).
- Stats: runs, wins, best floor, Hall of Fame (last winning teams).
- Seeded runs: show seed on summary; enter seed from menu.

## 2. Technical architecture

```
src/roguelike/                 (new, compiled only with ROGUELIKE=1)
  rl_core.c/.h     run state struct, run RNG (xorshift32), act/node progression  [PURE C]
  rl_map.c         DAG map generation                                           [PURE C]
  rl_party.c       party ops: persist HP/PP/status, XP/levels, recruit/release  [PURE C + 22630 calls]
  rl_enemy.c       enemy team generation from roster tables + scaling          [PURE C]
  rl_rewards.c     reward/shop/event tables & resolution                        [PURE C]
  rl_relics.c      relic defs + hook dispatch (rl_hook_stat, rl_hook_damage, …)  [PURE C]
  rl_save.c        meta-progression record (magic, version, crc) → EEPROM/CPak
  rl_text.c/.h     strings (game encoding) for all roguelike UI
src/fragments/rl/  (new overlay fragment, loaded via FRAGMENT_LOAD like others)
  rl_scene_map.c   map screen; rl_scene_draft.c; rl_scene_reward.c; rl_scene_shop.c;
  rl_scene_event.c; rl_scene_bag.c; rl_scene_summary.c; rl_ui.c (window/list/cursor kit)
Hooks (guarded by #ifdef ROGUELIKE, minimal diff to original files):
  include/variables.h     STATE_ROGUELIKE = 0x30
  src/29BA0.c             dispatcher case + menu entry + post-battle return to RL
  frag 64/36/menu-select  add "Roguelike" option
  frag 63/54              inject player party + enemy team + rule set before battle
  frag 62                 (a) load persisted current HP/PP/status into battle mons at init
                          (b) relic hooks in stat/damage/turn-end
                          (c) on battle end: copy back HP/PP/status/fainted + win/loss → rl state
host_tests/            native gcc unit tests for all [PURE C] modules (seeded determinism, map validity,
                        scaling curves, save round-trip, relic math)
tools/rltest/          headless libretro harness (Python ctypes): load core+ROM, step frames,
                        scripted input, read RDRAM via .map symbols, screenshots, savestates, assertions
```

- **Pure-C core** is platform-agnostic (no libultra) so it is unit-testable on host and parallelizable.
- **Run state** lives in a fixed static struct in main-segment BSS (survives fragment swaps); ~1 KB.
- **Matching build stays intact**: everything behind `ROGUELIKE`; `make` (vanilla) must still hit the md5.
- **Code space**: new fragment for scenes; core in main segment (or a resident fragment). Verify ROM growth
  (>32 MiB → expand to 64 MiB, supported by emulators/flashcarts) or reclaim unused debug/N64DD regions.
- **Save**: prefer EEPROM free region if found (Stadium uses 4/16Kbit?), else Controller Pak note "STADIUM RL".

## 3. Workstreams (parallelizable)

| ID | Workstream | Depends on | Deliverable / Done criteria |
|---|---|---|---|
| W0 | **Repo + build infra**: fork setup, `ROGUELIKE=1` flag, `src/roguelike` wiring, prove a shifted non-matching ROM boots (add dummy code, grow fragment, boot to title) | — | `make` still matches; `make ROGUELIKE=1 …` boots in emulator |
| W1 | **Test harness**: libretro headless runner, input scripts, RAM symbol reads from `.map`, screenshot capture, savestate fixtures, `make rltest` | — | Script boots ROM, navigates to Free Battle, asserts state var; runs in CI-like script |
| W2 | **RE: battle fragment 62** — name battle-side mon structs (current HP, PP, status, fainted), battle init (party copy-in), battle end + winner flag, damage calc, stat-stage use, turn-end; document in `docs/battle.md` | — | Symbol names + header structs; harness test reads HP in battle |
| W3 | **RE: battle entry & teams** — frag 63/64/54/event-battle loader: how player party & opponent trainer are set, rule sets, how to start a battle programmatically and return to a chosen state | — | `rl_start_battle(party, enemy, rules)` prototype proven via harness |
| W4 | **RE: UI/text** — working text draw for arbitrary strings (font + encoding), window/list drawing, sprite/portrait (mon icons) reuse | — | `rl_ui` kit: draw window, list, cursor, text, mon icon; demo scene |
| W5 | **Pure-C core**: run state, RNG, map gen, enemy gen (roster data extraction to C tables), rewards/shop/events, relic math, XP curve | spec only | Host unit tests green; deterministic per seed |
| W6 | **Save/meta**: find free EEPROM space or CPak path, `rl_save` w/ checksum + versioning | W0 | Round-trip test in emulator & host |
| W7 | **Integration: vertical slice** — menu entry → fixed 3-mon party → 3 chained battles w/ HP persistence → summary | W0,W2,W3,W4 | Harness: win battle 1, assert HP carried into battle 2 |
| W8 | **Scenes**: map, draft, reward, shop, bag, event, summary | W4,W5,W7 | Each scene harness-tested |
| W9 | **Battle hooks**: relics, PP/status persistence, XP/levels | W2,W5,W7 | Relic effects verified in harness |
| W10 | **Content & balance**: trainer pools, bosses, events text, relic tuning, ascension | W5,W8,W9 | Simulated runs (auto-pilot AI via harness) complete |
| W11 | **Polish & docs**: README, build guide, IPS/BPS patch output (distribute patch, never ROM) | all | `make rl-patch` → `.bps`; docs |

## 4. Phases & orchestration

- **Phase A (parallel, ~6 agents)**: W0, W1, W2, W3, W4, W5. I merge each, run vanilla-match build + host tests + harness.
- **Phase B**: W6, W7 (vertical slice gate — must pass before scaling out).
- **Phase C (parallel)**: W8 split by scene (map/draft, reward/shop/bag, event/summary), W9, W10 data.
- **Phase D**: W10 balancing via auto-run harness, W11.

Orchestration rules: one child session per workstream on its own branch; each must deliver code + tests +
a short `docs/` note; I review, rebase/merge into the integration branch, run the gate
(`make` md5 OK → `make ROGUELIKE=1` → `host_tests` → harness smoke), and send fixes back. Agents don't stop until
their done-criteria pass on the integration branch.

## 5. Risks & mitigations
- **Battle engine is unnamed C** → W2 dedicated RE; use harness RAM diffing (snapshot before/after a hit) to locate HP/PP fields fast.
- **ROM shifting breaks hardcoded offsets** → W0 proves early; fall back to in-place patching of fragment slack + ROM append region.
- **Text/font encoding** → reuse existing tables; fall back to `HAL_Printf` debug font for MVP.
- **Fragment RAM collisions** → new fragment placed in unused vram slot; verified via map + harness.
- **Legal** → never commit ROM/assets; distribute BPS patch only.
