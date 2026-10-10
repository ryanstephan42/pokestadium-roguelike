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
