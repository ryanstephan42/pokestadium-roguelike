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
The compiled diagnostic table adds 304 read-only bytes in this opt-in probe
configuration; it does not change normal vanilla/roguelike configurations.

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
| Exact source-derived controller-input policy file | `328d16e9605bc6d6dbdf898e93ce1c9514f1d249052f0f7cc07db886a736a5ad` |
| Source-package 32-bit layout/proof JSON (includes mon/event offsets) | `71e30bb97870dfb1378a51b6ff4f5996018dd8a1107b4b814b8a381f4b0abad6` |
| Normal route journal note | `9753609d39d19521f9ba910edd1025eb247d041bd850475e5cf86e78eda74272` |
| Normal interrupted journal note | `21b21dd6ab6b2d5e4a2e320e915af85cecde6d579ebc9b2d2e8883bf167274a3` |
| Transform route journal note | `3d3dd757918ab6b98e61e5d2e0b7cc84386c0be676e800e35c6527099dcc52e4` |

`tools/rltest/story_sequential_inputs.json` pins win, carry reload, loss/terminal
reload and reachable Transform **policies**, not a proven core-calibrated
schedule. Win uses Charmander's actual Ember in slot 3, not a fabricated Water
Gun on level-five Squirtle. Loss proposes legal non-damaging choices where
available; a native outcome is never forced or assumed. Both outcomes must be
observed; a different outcome fails that scenario.

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
