# Permanent progression runtime

The profile root opens a **Permanent shop / registration** scene, separate from
the in-run Mart and its run-money balance. It shows permanent coins, pending
receipt count, implemented option prices/ownership, effects and controls.
Reserved events, contracts, cosmetics and extra kits are not offered.

The reviewed PR16 policy remains unchanged: at most one receipt per terminal
run whose final team adds a genuinely new eligible species. All those species
join the pending choice union. Duplicate-only teams still record and pay earned
currency. Registering one species or explicitly skipping consumes one receipt;
New Run first requires resolving remaining receipts. Back never means skip.

Profile edits use a separate resident old/candidate snapshot, including the
exact loaded suspend record. Retry reloads and compares the full encoded record
before writing, so an uncertain purchase, register or skip cannot double-debit
or consume another receipt. An unresolved edit offers only retry/back, and
blocks New Run and settings. Terminal recording still uses its original
once-only resident candidate, never the profile-edit path. Returning from an
unsaved summary retains its candidate rather than discarding an uncertain write.

Ordinary drafts use `rl_meta_choose_species` over base plus registered species
in the selected canonical rental pool. Both formerly inverted eligibility
filters now reject **locked**, not unlocked, species. Distinct species have
inverse-BST weights; rerolls consume run RNG and may repeat the prior draft.
Purchased themes filter that pool in settings (only themes with six legal
registered candidates can be selected). LEFT/RIGHT on a draft Pokemon cycles
owned compatible canonical move sets; zero restores its original loadout.

Ordinary starters use source species/moves only, level 30, fixed `0x9999` DVs,
zero stat experience, native rebuilt stats, no held relic, and base move PP
without PP Ups. All starter pools follow the same baseline. This deliberately
does not inherit source rental levels or trained stats. Legacy native regression
test modes retain their separate uniform source-rental/boost fixture.

Potion Cache replaces 200 of the existing 300 starting coins with one Potion,
leaving 100 coins. Kit and theme choices are session settings, reapplied only to
a new draft, not saved RunState fields. Ownership persists; after a cold restart
the player selects the owned theme/kit again. No permanent stats, extra healing,
stat absorption, or bonus reward odds are added. Prices, baseline DVs/level,
species weights and kit valuation are provisional balance proposals.

## Reproducible checks

`make -C host_tests battle-adapter-test meta-test` covers the actual play scene
and native rental catalogs, C89 declarations, ASan/UBSan, deterministic weighted
drafts, base/registered eligibility and heavily weighted locked exclusions,
theme/kit/move effects, and suspended-run/uncertain purchase/register/skip retries.

`python3 -m tools.rltest.test_permanent_progression` first follows an ordinary
seed-99 fossil event, recruits Kabuto, confirms starter releases and loses a
native battle with unmodified combat conditions to earn a non-base receipt.
It then completes native losses using the explicit legacy one-HP/Growl fixture
to fund sufficient currency (26 total completed encounters). It uses real registration,
skip, shop and settings controls, three fresh processes, active suspension
preservation, and ordinary themed/variant/kit drafts. It never injects profile
currency, registration bits or terminal outcomes. This is not an ordinary
unboosted full-run win/balance proof.

`python3 -m tools.rltest.test_meta_runtime` now also repeats a real loss in a
fresh process, replacing `/tmp/pr16_twoloss.py`.
`python3 -m tools.rltest.test_save_migration` replaces `/tmp/pr16_migrate.py`:
it creates a native UI suspension, converts only the framed note's records to
explicit v3 wire fixtures, then proves native load, UI v4 upgrade and cold reload
preserve the entire run and every unowned save byte. ROM/save binaries and game
screenshots remain ignored local artifacts, never tracked fixtures.

`scripts/rl-gate.sh` remains mandatory. Full ordinary native three-act/finale
win and additional relic-cadence cases are separate final validation items;
these focused progression checks do not substitute for them.
