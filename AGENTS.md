# Agent guide: Pokémon Stadium Roguelike

Fork of the pret/pokestadium decomp (US N64). Goal: a new "Roguelike Run" mode. Full plan:
`docs/roguelike/PLAN.md` — read it before working.

## Hard rules
- **Vanilla build must stay byte-matching.** `make` (default `COMPARE=1`) must print
  `build/pokestadium-us.z64: OK`. All mod code lives behind `#ifdef ROGUELIKE` or in files only
  compiled for the roguelike build.
- Roguelike build: `make rl` (provided by workstream W0; builds with `ROGUELIKE=1 NON_MATCHING=1 COMPARE=0`
  into a separate build dir so it never pollutes the matching build).
- **Never commit** ROMs, extracted `asm/`, `assets/`, `build/`, `expected/`, savestates of copyrighted
  data, or screenshots of game output to git. They are git-ignored; keep it that way.
- Run `scripts/rl-gate.sh` before declaring work done. It must end with `GATE PASSED`.
- Keep diffs to original decomp files minimal and guarded. Prefer new files under `src/roguelike/`,
  `src/fragments/rl/`, `host_tests/`, `tools/rltest/`, `docs/roguelike/`.
- When you identify an unnamed symbol (struct field, function, global), rename it in the decomp
  only if it doesn't break matching; otherwise document it in `docs/roguelike/re-notes/*.md`
  with address, file, evidence.
- Pure roguelike logic (`src/roguelike/rl_*.c` marked PURE) must not include libultra headers so it can be
  compiled and unit-tested with host gcc (`host_tests/`).
- Commit in small logical commits with clear messages. Do not rewrite history on `master`.

## Environment
- Worktrees are populated by `scripts/rl-worktree-setup.sh` (copies baserom/extracted data from the
  root checkout `~/workspace/pokestadium-roguelike`).
- Emulators: RetroArch libretro cores at `~/.config/retroarch/cores/mupen64plus_next_libretro.so`
  and `parallel_n64_libretro.so`. The headless harness (W1) lives in `tools/rltest/`.
- Key code: state machine `src/29BA0.c` + `include/variables.h`; battle system fragment 62
  (`src/fragments/62`); battle prep/rules fragment 63; pre-battle menu fragment 64; rental/rules
  fragment 54; Pokémon record/stat calc `src/22630.c`, `src/232C0.c`; RNG `src/math_util.c`;
  input `src/controller.c`; overlays `src/memmap.c`, `src/stage_loader.c`; roster formats
  `oldnotes/utils/extractrostertbltbl.c`.
