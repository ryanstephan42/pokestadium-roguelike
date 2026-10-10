# In-run node scenes (W8)

Normal runs use the canonical seven-row maps, three Acts, and single-node
Mewtwo finale. The map shows node kinds and disables lanes without an outgoing
edge. Trainer, elite and boss choices use W10's `rl_load_encounter`; the
three-trainer W7 regression route remains available only through the guarded
test mode.

`src/fragments/rl/rl_scene_nodes.c` owns the nonbattle UI. It receives resident
run state and canonical Poke Cup rentals; it does not create a second run model.
Successful one-shot choices finish the node in the same update, so suspending
cannot repeat a reward or healing operation.

| Screen | Actions |
|---|---|
| Poke Mart | All ten consumables, one relic, one native TM; act-scaled prices, Bag, Party, leave |
| Bag | Choose an owned item, target Pokemon, and move slot for Ether/TM; native TM compatibility and PP |
| Pokemon Center | Heal living Pokemon by 50%, or revive one fainted Pokemon; Bag and Party |
| Events | All twelve canonical event definitions; party selection for Daycare/trades, three Safari candidates |
| Recruit | Choose one of three canonical rental candidates, or leave |
| Party | View HP/status, reorder members, release with confirmation; cannot release the last member |

The map also exposes Bag and Party without advancing a node. Item, recruit,
trade, TM, money, relic, healing, and daycare mutations use existing core
helpers. Ordinary healing does not revive. Bag/TM consumption happens only
after the helper accepts the action; full parties, insufficient funds,
incompatible moves and unavailable choices are displayed explicitly.

Shop offers and event identity are derived from seed/act/row/lane with fixed
salts. Opening a scene does not consume the live run RNG. Owned relics disable
their fixed shop offer rather than rerolling it. Event resolution uses a
coordinate-derived candidate stream, so leaving a target-selection submenu
or resuming does not change the displayed choice. No save layout was added.
Fossils use the actual Kabuto/Omanyte records in the Poke Cup rental pool.

## Information and battle order

The selected starter shows its native species/move names, level, HP, and
current/maximum PP for all four move slots before confirmation. Names come
from Stadium's resident text archive, not a copied move-name catalog.

Highlighting a battle lane materializes W10's complete scaled encounter on
a private run copy. The panel shows the source roster, trainer/round, species,
and levels; the live RNG, party, route and generated content are untouched.
The preview matches the actual queued trainer roster. Currently the first
three roster members fight (or the one-member Mewtwo team). Winning awards
the displayed canonical coins and XP; recruit-from-roster, Candy, vitamin,
relic, and money are **possible random reward choices**, not promises.

Every fight opens ordered entrant selection before advancing the route.
Living members are initially selected in party order. **A** removes a selected
member or appends an unselected one; selection number **1** is the lead.
**LEFT/RIGHT** moves the highlighted selected member earlier/later without
changing the persistent party. **UP/DOWN** moves the cursor; select
**Confirm active team** to launch, or **B** to return without advancing.
Fainted members remain benched, duplicate species keep distinct run-slot
identities, and battle results are restored by slot rather than species/order.
Earth Badge now exposes the core's fourth reward choice instead of clipping
the generated offer list to three.

`host_tests/test_play_ui.c` drives the actual scene update/draw functions,
checks side-effect-free canonical previews, move/PP detail draws, ordered
launch/result mapping with duplicate species and an unchanged bench, and the
fourth Earth reward. Native trainer/elite/boss entry scenarios capture route
and ordered-party screens, reorder through real inputs, compare the preview
against the queued roster, and check the native lead. Native visual acceptance
is pending the shared emulator validation queue.

## Relics

From the between-node Bag or Party root, press **R** (`R_TRIG`) to open the
owned-relic list; **B** returns to the same root without changing its Back
index. The selected relic shows the canonical `rl_relic_defs` name and wrapped
effect text. Old Amber and Master Ball also show whether their one-time recruit
use is spent. Each detail and shop preview also marks its runtime status:
run-core effects that currently work are identified as active, while native
battle-only effects are explicitly marked as not connected yet. These are
run-wide relics, not items in the consumable Bag.

Relics can be offered as battle rewards or at the Mart, and the Hidden Grotto
event grants a random unowned relic from the profile-permitted set. Lavender
Tower grants Ghost Relic after its HP cost; Mysterious Shrine grants Cursed
Power. Old Amber and Master Ball are the only relics with `consumed_relics`
state: their pure-core recruit helper spends them only after recruitment
succeeds. The helper currently does not enforce that Master Ball is used for
a boss.

Several run-core effects already work after a native battle result: Amulet Coin
increases battle money, Exp. Share and Lucky Egg modify awarded XP, Rainbow
Badge heals the living party after a win, and Earth Badge adds a reward choice.
Most battle-time effects are still inactive in native Stadium battles: the
battle adapter does not invoke the stat, damage, turn-end, status, critical-hit,
or PP relic helpers. Silph Scope's pre-battle reveal is also not wired into the
native selection UI.

Old Amber and Master Ball are recruit-only, with no passive effect. Their
one-time core helper consumes them only if recruitment succeeds, but no
recruit UI currently calls that helper. Master Ball's boss-only restriction is
not enforced. The inventory labels these unavailable recruit options and
shows an already-spent one-use relic. Do not interpret the effect description
alone as proof of native battle integration.

**START** in a nonbattle scene saves to the separately named Controller Pak
note and returns to Menu Select. Resume restores the selected `RL_NODE`
without advancing it again; a Bag/Party suspension from the map resumes the
map. Save errors and recovered-copy warnings are displayed. Native battles
are not suspended mid-command; suspend from the map before choosing a battle.

## Persistent progression

New Run loads the existing Controller Pak profile. Only a missing profile
offers explicit creation; no device offers an explicit unsaved run. Corrupt,
unsupported and recovered saves are never silently reset. The root settings
screen selects an unlocked starter pool and cycles through saved unlocked
ascensions using W10's policy helpers. The resident profile and pending terminal
save survive native battle fragment swaps.

Event selection, shop relics and reward relics obey the profile's permitted
masks. Node-local event selection uses `rl_meta_choose_event` with a
coordinate-derived RNG, never the live run RNG. The ordinary profile starts
with only the first eight events/relics. The emulator's exhaustive content
suite uses a separately documented, explicit unlocked-profile storage fixture,
not a bypass in production scene code.

A terminal native win/loss prepares exactly one `rl_meta_record_run` candidate
and stores it with no suspended run. Storage failures keep the candidate for
retry, and reload recognizes an already-committed identical candidate after
an uncertain write; it never increments it again. Successfully persisted
results are retained on re-entry. The guarded W7 three-win milestone is not a
canonical finale and does not award progression.

`python3 -m tools.rltest.test_meta_runtime` drives a real native loss, requires
one persisted run and retained locked profile masks, copies only libretro save
RAM, then reloads in a fresh emulator process and checks re-entry does not
record the loss again. `host_tests/test_nodes.c` follows all three Acts and
the finale through the canonical core, then verifies win/Hall-of-Fame/ascension
and content unlocks. A native full three-Act/finale victory remains a final
project end-to-end validation item.

## Validation

`make -C host_tests nodes-test` covers the complete shop catalog, all twelve
events, non-reviving heals, daycare return, and version-3 suspend encoding for
every node kind without removing a sole Center.

`python3 -m tools.rltest.test_nodes` drives real draft, node, Bag and Party
inputs, including Controller Pak suspension/resumption for each nonbattle
kind and all twelve events. It writes only the guarded seed configuration,
never the map, party, phase, HP, battle results or scene state. Separate
`--battle-kind trainer`, `--battle-kind elite`, and `--battle-kind boss` modes
navigate canonical routes and verify real native battle initialization.
Screenshots remain ignored under `.local/rltest/nodes/`.

These entry scenarios are not a full three-Act native win/loss playthrough.
The existing W7 scenario remains the native three-win, native-loss and exact
HP/PP carryover regression.
