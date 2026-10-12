# Sequential run preparation (source/HOST only)

`tools/rltest/story_sequential.py` replaces the **assumptions**, not the frozen
files, of the old in-battle six-party observer. It reads the resident campaign
control through the diagnostic-only `gScCampaignObserverControl` pointer and
reads UI storage only in `SC_RUN_ACTIVE`; it never writes RDRAM or launches a
core. It checks operation/encounter/variant/ordered-party identity, KO order,
fixed in-battle progression, cumulative postbattle KO-time XP/stat experience
and levels, loss discard, and preservation of fainted HP/status. The shared
budget includes all frames and has no learning exclusion or reload reset.
Native fences, move-choice/evolution fidelity, native journal acceptance, core
button calibration and Transform visuals still require separate checks.

Generate a new source package with the reviewed Moon catalog already present:

```sh
python3 -m tools.rltest.prepare_story_sequential \
  --out build/story/sequential-source-package-review \
  --catalog-c build/story/sc_moon_data.c \
  --catalog-json build/story/moon-data.json
python3 -m unittest host_tests.test_story_sequential_observer
```

The generator runs only host gcc (`-m32`), validates legal checkpoints and
codec round trips, and emits checkpoint bytes, an offset schema, catalog/input/
source hashes and a manifest. Output must be fresh and under `build/story`.
The schema's **HOST** 32-bit offsets must be independently bound to the final
native ELF before capture; source layout is not a native artifact seal.
The native OS/lifetime prefix is larger than the HOST seams: IDO confirms
control size **4,104**, versus HOST **3,768**, with sequential fields shifted
by **336** bytes. Never pass the unbound HOST control offsets to an emulator.
The compiled diagnostic tables add 468 read-only bytes in this opt-in probe
configuration: 83 shared layout words and 34 native-only lifetime/scheduler
words. The latter cannot use the substituted HOST OS/display types. Binding
checks their actual ELF/ROM bytes, map address, exact source-defined
snapshot/frame/span leaf layouts and nonoverlapping fields inside the native
control prefix. The reviewed control4104/+336 rule is unchanged. Normal
vanilla/roguelike configurations have neither table.

The normal route checkpoint uses Red, Standard healing, Charmander, seed 42,
and the existing legal six-party prehistory through the lab rival, three
Viridian Forest trainers and Pewter Gym's Cooltrainer. Target is canonical
`PewterGym/TEXT_PEWTERGYM_BROCK`, index 58, variant 0. Prior victories are pure
HOST preparation, not native coverage. The Transform checkpoint repeats that
legal prehistory using the approved all-species random starter option: seed 28
produces Ditto with actual Transform (move 144). Both checkpoints pass
`sc_state_valid` and `sc_available`, have six slots, and round-trip SC01 v2.
No progression/milestone bit or native battle record is patched to make them
reachable.

Current reproducible pins:

| Artifact | SHA256 |
|---|---|
| Normal route, win/loss input checkpoint | `e21f6b6e740c4d981ba6dc1f9147bf163adca492ed72c71d19b0d2b62030ea30` |
| Normal interrupted, loss reload expected checkpoint | `180dcef711d1968afe49eb0ad18b4468ff1d3ffb99352a778f74bf8b96847d96` |
| Transform route checkpoint | `246906f5edbc2e1e6648c3c7f19969e6e365bcb28de5642f9973f45146275b4c` |
| Exact source-derived controller-input policy file | `b0cb767d6ffe1d499b946f87a9b2a8f5fc36931d2047c49f41ab996a4998384c` |
| Source-package 32-bit layout/proof JSON (includes mon/event/menu offsets) | `3633070e34da87204eef22b31fa7dd3137609e83c958d6f60394273d3bee59e4` |
| Normal route journal note | `9753609d39d19521f9ba910edd1025eb247d041bd850475e5cf86e78eda74272` |
| Normal interrupted journal note | `21b21dd6ab6b2d5e4a2e320e915af85cecde6d579ebc9b2d2e8883bf167274a3` |
| Transform route journal note | `3d3dd757918ab6b98e61e5d2e0b7cc84386c0be676e800e35c6527099dcc52e4` |

`tools/rltest/story_sequential_inputs.json` pins win, carry reload, loss/terminal
reload and reachable Transform **policies**, not a proven core-calibrated
schedule. Win uses Charmander's actual Ember in slot 3, not a fabricated Water
Gun on level-five Squirtle. Loss proposes legal non-damaging choices where
available; the pinned Diglett knows only Scratch, so the worker uses the first
actual legal move when no reviewed non-damaging move remains. A native outcome
is never forced or assumed. Both outcomes must be
observed; a different outcome fails that scenario.

## Composed scenario worker (requires separate execution approval)

`run_story_sequential.py` composes the reviewed Linux supervisor and the actual
`story_sequential_worker.py`. It does not authorize itself. After the source
delta, fresh native link, authority fixed point, binding and seal are reviewed,
the separately approved exclusive reservation uses:

```sh
python3 -m tools.rltest.run_story_sequential \
  --seal build/story/CANDIDATE-native-seal.json \
  --seal-sha256 APPROVED_SEAL_SHA256 \
  --package build/story/CANDIDATE-source-package \
  --binding build/story/CANDIDATE/sequential-binding.json \
  --core /absolute/path/to/mupen64plus_next_libretro.so \
  --core-sha256 APPROVED_CORE_SHA256 \
  --out build/story/CANDIDATE-one-reservation \
  --exclusive-approved-reservation
```

Every child inherits the same supervisor streams and starts a fresh OS
process/core: win, carry reload, loss, its terminal reload (both share the loss
cap), then Transform. Only one core is alive at a time. `Frontend` runs one
frame *inside its constructor*; the worker's subclass leases that frame before
`retro_run`, not by attaching a budget after construction. The supervisor
clock already includes worker startup, core loading, calibration and all
reloads. Native C stdout is redirected to stderr so it cannot corrupt the
frame protocol. All exits report reserved/observed frames, monotonic time,
scenario statuses, actual carry-note hash, artifact hashes and exact-tree
reaping. Unknown frames after a killed incomplete step remain unknown, not0.

The worker verifies the external seal/core pins and all sealed source/artifact
hashes before loading a core. It converts only that fresh core's formatted
empty Pak, using the hash-bound real HOST journal note; cold reload restores
only the private actual save container. During calibration, candidate
RetroPad/right-stick mappings are hypotheses: actual native controller
`buttonPressed` bits establish mappings. Missing buttons/Pak acceptance abort.
There are no guessed C aliases or silent fallbacks. Menu entry waits for the
guarded diagnostic `gScCampaignMenuReady` at fragment57's actual top-page
input loop, not a guessed fade delay. UI rows/cursor/IDs/labels are read through
the newly bound menu fields; choices are confirmed by normal controller input.

Every battle frame goes through the read-only sequential observer. Before
battle input, the native-only witnesses expose retained joined-cleanup flags
and the last atomic scheduler snapshot: completed fallback fence, no pending
work/tasks/queue completions, VI on the resident fallback, crash framebuffer
outside retired UI spans, freed UI storage/display scope and an unmapped story
image. Missing/stale proof aborts. These are observed retained scheduler facts,
not a proof of every allocator epoch, overall heap fit or GPU correctness.
The replay observer compares actual returned HP, status and PP against every
postbattle party slot, allowing only the canonical max-HP increase on level-up
while preserving damage and fainting. Original progression fields in native
carry must remain unchanged; declining learning/evolution leaves moves intact.
The actual result remains in resident value storage after the shell consumes
it. If the short REOPEN stage falls between frontend frames, capture may read
that retained result in ACTIVE only when request operation equals the actual
launched operation, `sequential_active` and `result_ready` are both clear, and
the ledger binding is unchanged. This observes a completed/consumed result;
it does not predict one from HP or UI progression.
Move/party inputs require actual unlocked native menus, live HP/PP/disable
guards and observed mode3/slot acknowledgement, including transient ACKs
during the held frame. Native-to-story mapping comes from the actual launcher,
not an assumption that story slot0 equals native slot0.

Winning replay declines each learning/evolution choice through explicit rows,
then declines the reward and saves through UI. Carry reload compares the
complete observed committed state plus all party values, without healing.
Loss preserves the interrupted journal; its fresh-process Continue compares
the exact prebattle state with only phase changed to terminal. No separate
terminal save is invented. Transform must actually change the live move set
to the opponent's; selected Transform alone is not a pass. This proves battle
data observation/carry, not visual animation fidelity.

HOST composition tests run the real worker orchestration, private Pak/save
handling, report/cold comparisons and supervisor against fake core buffers and
labelled UI/result seams. Direct fixtures exercise constructor leasing, native
mapping, transient acknowledgement, bound menu selection, calibration,
retained scheduler refusals and external-pin refusal. These tests do not load
a ROM and are not native gameplay evidence.

The carry-reload artifact must be the actual win journal from the same approved
reservation. Its hash is recorded on successful commit and checked unchanged
on fresh Continue. Inventing its bytes/hash before the native win would conceal
HP/PP/status carry defects. The interrupted source checkpoint pins the loss
expectation, not a fabricated terminal write: current code reconstructs terminal
state from the durable interrupted prebattle note.

## Isolated Pak conversion

The HOST generator now writes the three `.note` files using the actual
`sc_storage_store` journal, verifies `sc_save_decode` and `sc_storage_load`,
and preserves the pinned source states. Slot zero is generation zero and slot
one is empty. This is not a Python reimplementation of the codec.

`tools/rltest/story_sequential_pak.py` installs a hash-checked note into a
supplied formatted **empty, isolated single-bank 32 KiB template**, never a
user's existing save. It refuses existing notes, allocated/orphan pages,
damaged header/inode copies or checksums, wrong note hashes and existing output.
It writes the SC01/RL directory identity, authentic name/extension, occupied
status (required by `osPfsReadWriteFile`), six-page chain, payload and both inode
checksums; the existing owned-byte checker refuses unrelated changes.

```sh
python3 -m tools.rltest.story_sequential_pak \
  --package build/story/sequential-source-package-review \
  --template build/story/isolated-empty-template.mpk \
  --note normal-route.note --out build/story/new-isolated-story.mpk
```

Synthetic formatted HOST templates exercise conversion only. An actual core
template/container and native Pak/journal acceptance still require the later
authorized run. Real win carry uses the native-written journal, not reconversion
from a fabricated HOST win.

## Source-derived menu policy and binding

`story_sequential_buttons.py` derives direct buttons from fragment 62's
`func_8430EE50`, `ED88`, `E72C`, `E8E8`, `EF4C` and `F0C0`.
Move slots are **C-up, C-right, C-down, C-left**. Six-party native slots are
**C-up, C-left, B, C-right, C-down, A**; fewer than four use C-up/right/down.
Fight is A and switch is B. No D-pad battle cursor schedule is invented.

The read-only menu reader is gated to resident sequential BATL and one human
trainer/controller. Inputs require unlocked actual move/party modes, legal
move IDs, PP/disable eligibility or living nonactive party slots. Send one
edge and release frame through counted steps, then wait for native mode 3
and the selected native slot. Apply the launcher's native-to-story mapping;
do not assume the story slot is a native slot. Forced/locked actions, Struggle
and animation waits are not blindly scheduled. Unexpected/nonacknowledged
input fails at the scenario cap rather than retrying or forcing an outcome.

The exact Mupen core's RetroPad/right-stick C mapping and acknowledgement timing
are **not yet calibrated**. Calibration must observe actual N64 `buttonPressed`
bits for this exact core/configuration. The helper refuses missing mappings;
frontend B means N64 A, and frontend Y means N64 B, not C buttons.

`bind_story_sequential.py` can check a supplied final ELF/ROM/map without building:

```sh
python3 -m tools.rltest.bind_story_sequential \
  --package build/story/sequential-source-package-review \
  --elf build/story/CANDIDATE/pokestadium-us.elf \
  --rom build/story/CANDIDATE/pokestadium-rl.z64 \
  --map build/story/CANDIDATE/pokestadium-us.map \
  --out build/story/CANDIDATE/sequential-layout-binding.json
```

It checks source/package hashes, ELF32/MSB/MIPS linked type, symbol extents,
ROM-loaded compiled layout/pointer bytes, resident control/run placement and
map agreement. Compiled target offsets override only the reviewed **+336**
control-tail shift with HOST size **3,768** and native size **4,104**; other
uniform shifts (including zero) are refused. All pure/UI/mon/event offsets and
the UI pointer position must agree. Use its **bound `abi`**, not the HOST schema.
IDO object witnesses and synthetic ELF fixtures are preparation, not final-link
proof. This binder is only a layout binding, not the complete catalog/build
configuration, heap-fit, reader/ACE/epoch or retirement artifact seal.

## Executable budget supervisor

`story_sequential_supervisor.py` runs one worker in a private process session.
The dedicated Linux supervisor enables subreaping and starts its deadline
**before spawning the worker/core or boot**. Each frame batch requires a
JSON request/ack before execution. `bind_frontend_budget` attaches that protocol
to each fresh frontend before frame zero, including `press`/`wait`/`step`;
reloads share the same worker, streams, aggregate and deadline.

It enforces scenario order and separate caps: win 7,000, carry reload 2,000,
loss plus terminal reload 5,000, optional Transform 4,000; total <=18,000,
elapsed <180 seconds. Unused allocations are **not transferable**. Silence,
malformed protocol, exhausted caps, failed/missing completion or watchdog expiry
abort. Every exit kills the exact owned group and then pidfd-kills/reaps all
subreaper-adopted descendants, including detached children. HOST subprocess
tests verify preboot timeout, capped reservation, fresh-reload continuity,
implicit frontend steps, refusal, normal orphan exit and detached-child cleanup.

The native scenario worker is still pending: it must compose this supervisor,
bound ABI, calibrated buttons, Pak container, observer and actual UI actions.
An independently running policy helper is not execution enforcement.

**Execution remains blocked** until source review, final native artifact binding
and an explicit user build/run grant with one exclusive Mupen slot. Build-only
gaps are the full 8 MiB link/catalog/configuration seal and final ELF/ROM/map;
emulator-only gaps are the core Pak container/acceptance, calibrated buttons,
actual menu acknowledgement, lifecycle/fence/heap observations, actual win
carry reload, loss/terminal reload and reachable Transform. The gate and vanilla
byte-match remain separately outstanding; neither was run under SOURCE/HOST
scope. No existing `six_natural` command may silently consume this package.
Old c399 evidence and normal 4 MiB paths are untouched.
