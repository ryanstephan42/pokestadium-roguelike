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

Before every frame lease, the worker atomically persists `progress.json`;
completed frames are persisted separately after each return. Waits, input and
individual calibration candidates are recorded before dispatch. The last
32 observer transitions and last bound witness survive a cap kill without
depending on `finally`. Up to eight rotating read-only video PNG slots capture
boundaries and every500 waiting frames; files stay in private ignored output.
Boot witnesses include game state, memory size, runner stage, menu readiness
and mapped overlay indices. Each leaf records its own read error; one missing
symbol cannot erase the other leaves. Boot addresses, including ELF absolute
`osMemSize`, are bound from the sealed ELF rather than assumed present in GNU
map standalone-symbol lines. The first two video slots preserve early frames;
six later slots rotate. Repeated Start/wait boundaries do not trigger a new
snapshot every pulse; unseen boundaries or500 elapsed frames do.
An interrupted frame remains unverified; the report uses persisted completed
frames and names the last boundary instead of inferring from reservations.

Boot sends ordinary Start pulses every120 frames after initial logo startup
while state1 still includes the fragment17 movie. Source `func_80029310` does
not set title state2 until that movie returns; fragment17 `func_86B00C34`
accepts Start/A/B when its stage permits. Waiting for state2 before sending any
input can therefore waste the boot allocation. The pulse's actual native
button-down value is recorded. Full button calibration still checks actual
pressed bits at title; calibration itself may advance title, so subsequent
Start pulses are limited to title state2 until the real menu is reached.
The title loop has a30-tick readiness gate (`func_821009B4`); menu entry still
requires the guarded input-ready witness, never a guessed fade delay.
This is a source-derived stall hypothesis, not evidence recovered from the
failed583b2d6 reservation.

The approved04b5d09 run reached7000 persisted completed frames in127.47s but
stayed state1 despite verified Start bit0x1000. All eight viewed surviving
PNGs were nearly black with faint banding, no visible title/menu. No story,
calibration, battle or save accuracy was reached. This does not establish a
specific native boot cause. Source comparison with `tools/rl/boot_test.py`
and `test_play_run.py` finds the same Frontend, Angrylion software RDP, HLE
RSP, dynamic-recompiler CPU, word-swapped RAM and software video callback.
Boot smoke uses default Pak; play-run and story use pak1=memory. Story alone
previously requested EnableExpansionPak=True, but offline inspection of the
pinned core has no such key. The worker now uses the same core default as
normal play-run, with actual ELF-bound osMemSize8MiB still mandatory for story.
It does not guess a replacement from the "Disable Expansion Pak" label.
During real negotiation, the worker captures registered legacy SET_VARIABLES
and V1/V2 option definitions (including US international definitions). It
refuses any explicitly requested or active Mupen option absent from those
definitions, and any requested value outside registered choices. A registered
extra-memory-disable option is set only to its unique registered False/off/
disabled value; ambiguity refuses. Negotiated options and the selected extra
memory value are persisted before frame zero. Story also refuses unless the
ELF-bound actual osMemSize is8MiB from frame120 onward, independent of options.
Save type is ROM/core-selected, not overridden.
Both configure joypad device1 at ports0/1; story additionally calibrates right
stick/controller bits and uses preboot private note setup. Historical exact
core bytes/settings cannot be claimed identical without prior pinned evidence.
No software/HW readback deviation has been identified from source alone.

An explicitly approved `--control` manifest enables a separate fresh-process,
private normal-roguelike boot control before story. The manifest and ROM/map/
provenance bytes must be part of the same sealed artifact set, with the exact
core hash. Both control and story use identical frontend/software/Expansion
Pak/Pak/controller options; control neither installs a story checkpoint nor
touches historical normal saves/output. The control runs1500 frames only,
records video/boot witnesses and then unloads; this is renderer/boot diagnosis,
not a new gameplay validation. Title/menu by its endpoint is PASS; otherwise
FAIL, and the story attempt proceeds for comparison. Historical allocation was
1500 control +5500 win +2000 reload +5000 loss/reload +4000 Transform=18000;
caps do not transfer and180s includes the control. Without `--control`, the
previous7000/2000/5000/4000 policy remains. Either partition requires its own
explicit user grant, never reuse a consumed run.

The user's subsequent test-procedure decision supersedes that aggregate
partition: select exactly one scenario with `--only-scenario`. Each reservation
has a maximum of 18,000 frames and a 480-second wall watchdog, including core
load, boot, calibration and fresh reloads. Loss and terminal reload share the
same reservation/counter. A 1,500-frame control is used only after a change to
frontend/core setup and is included in that reservation's 18,000-frame ceiling
(leaving 16,500 for the selected scenario). Scenarios execute sequentially,
never concurrently. See `SEQUENTIAL-PROPOSAL.md` for the current procedure.
Standalone carry takes `--producer-run` and `--producer-report-sha256`; the
producer must be a safely reaped PASS win on the same ROM/core. Its aggregate
report pins the exact save container, committed state, calibration and
scenario report. Altering any of these inputs is refused before core load.

The newly approved sequential-only normal checkpoint uses Squirtle (starter
7, seed 42), not the shared Charmander fixture. HOST preparation replays the
same five legal prior encounters with the correct rival branch, ordinary
KO XP, accepted level-up learning, recruits and available location healing.
Bubble (145) is therefore learned through the actual level-up API and is
packed into slot 2; its PP travels with it. Brock remains the two-enemy target.
The shared fixture, frozen c399 and Transform checkpoint are unchanged.

The worker verifies the external seal/core pins and all sealed source/artifact
hashes before loading a core. It converts only that fresh core's formatted
empty Pak, using the hash-bound real HOST journal note, only during pre-boot
scenario setup before frame zero. Cold reload models ordinary frontend Pak
persistence: load only the exact unmodified game-written save container before
the first frame, matching its producer report hash. Checkpoints or substituted
bytes are refused. Reports record producer, loaded and post-reload hashes;
the whole container must remain identical across Continue. During calibration, candidate
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
# Standing-grant boot diagnosis

The first `8ddd6dc` control reached the visible title at 1500 frames under
registered Angrylion four-thread settings; story remained at intro state 1
through 5500 frames with actual 8 MiB. This rules out shared software readback
and absent Expansion Pak as explanations for that run, not story boot faults.
Read-only diagnostics now retain mapped overlay bases/sizes, saved Game-thread
PC/cause/SP/RA and at most 16 stack words, plus pool/failure records. Missing
diagnostic symbols are reported individually; no native memory is changed.

The next diagnosis caught a stopped Game thread in fragment2 relocation:
`NON_MATCHING=1` shortened its image by 32 bytes but retained the original
header/table offsets. Story-service builds now retain matching original
overlay bodies, as normal roguelike already does, except regenerated31/62.
Fragment57's story menu witness requires its own regenerated header/table.
The top-level story command remains nonmatching for resident/new story code.
These target-specific build rules do not affect vanilla or normal roguelike.
The extra menu relocation table crosses the fixed GB-Tower ROM frontier.
Story-only linking also defers the adjacent symbol-loaded `_6EB340`/
`_6EC4D0` data pair to the existing ROM tail. Their original ordering and
adjacency are preserved because fragment28's matching assembly uses the
second segment's start as the first segment's end. RAM reservations and
fixed GB-Tower/model ROM offsets do not change.

The repaired image first reached title state2 in run3, then refused incomplete
C-button calibration. State2 precedes title asset loading/fade and its
30-tick input gate. The worker now adds 120 neutral counted frames before
calibration and persists each candidate's observed pressed-bit set. This is
a bounded frontend timing correction, not an assumed C-button mapping.
Run4 verified all four actual C bits after settling, but title A then began
the fade before R/D-pad/Start were tested. Non-advancing candidates now come
first, A next-to-last and Start last; Start has up to 120 counted frames to
observe its edge across the title-to-menu transition. No mapping is accepted
without the actual native pressed-bit observation.
Run5 calibrated every required button, then waited on the first state4
Game Pak Check screen. Entry now acknowledges it once with calibrated A
after the normal harness's 60-frame settling interval, only while the
campaign top-page readiness witness remains false. The readiness witness,
not state4 alone, still gates R campaign entry.
Verified button pulses hold six counted frames and release for two, matching
the successful normal harness's short press duration. One VI-frame pulses
can miss Stadium's asynchronous controller polling.
Run6 reached the actual six-party native battle with an interrupted Pak
checkpoint and retained bound ledger, then its process exited before the
first action returned. Exit status is now persisted. Optional `--debug-exit`
wraps only that scenario in a process-local GDB syscall-exit catch, retaining
host registers/thread backtraces in private output. It neither writes game
RAM nor changes core options; debugger overhead remains in the same watchdog,
and a missing native report still fails. No external debug-symbol download.
Run7 stopped on GDB startup output before a story frame; the supervisor
correctly rejected non-JSON on its protocol. Debug mode now inherits a
dedicated counted-step descriptor, captures all GDB stdout privately, and
disables user GDB init files. The protocol is never shared with debugger
output, including startup messages.
Run8 proved a deliberate `exit(1)` inside the pinned core, not a Python
exception or OS signal. The stripped core return site at offset `0x2de692`
follows its `Compile at bogus memory address: %x` fatal branch. The next
diagnostic also retains mappings and 256 bytes of that caller's stack so
the rejected guest address can be resolved rather than inferred.
Run9 retained guest PC `0x8430d480`, exactly the linked
`sc_batl_clock_source` symbol. Resident scope code called that overlay symbol
directly, bypassing the loader's virtual-to-loaded-address conversion. The
scope now resolves and bounds-checks both the clock callback and the battle
root array through the current fragment map. Its lifetime/generation checks
remain unchanged; the opened clock is retained instead of calling twice.
Run10 passed the former fatal jump but stopped on a native-observation
admission error at frame931. Per-frame diagnostics now retain the raw
40-byte runner record and both sides' controls/live mon/selected-party
records, bounded to six records and 8 MiB RDRAM. This is read-only evidence
to identify the rejected invariant without weakening the fail-closed gate.

The validator's matching rebuild on 2026-10-12 changed only the consumed
reference map's textual representation: the vanilla ROM still matches
`ed1378bc12115f71209a77844965ba50`, and the ELF is byte-identical to the
previous seal. Every map-derived anchor, linker symbol script and native
header matched the sealed outputs after build-directory normalization.
The coordinator authorized a new rebaseline record; historical seals were
not rewritten. Subsequent story builds must pass
`VANILLA_BUILD_DIR=build/story/reference-vanilla`, whose private immutable
map/ELF/ROM copies and `rebaseline.json` are pinned by the new seal. Without
the old map bytes, this is equality of all consumed facts, not a claimed
raw-map diff.

User decision after run11: story moves stay packed like Red/Blue. Learning
fills the first empty slot; only a full moveset offers replacement choices.
Transfer/import compacts in original order with PP/max-PP travelling with
the move. Saves containing gaps are refused as invalid, not silently
truncated. The sequential package compacts its own prepared checkpoints;
the shared historical fixture and frozen c399 are unchanged. The old
six-probe build excludes the new rule; vanilla/normal roguelike are unchanged.
Run12 preserved Ember and passed 4,274 native observations with no guard
error. Its viewed snapshot shows a rendered human action menu waiting for
A/B/Start; the worker had pulsed A during the earlier battle fade and then
waited forever for that missed edge. The worker now rechecks the actual
mode after at most 60 counted neutral frames, then returns to its normal
input policy, allowing another fresh edge when the menu is ready. It
does not write native state or extend any scenario/watchdog cap.

Run13 (`6e78171`, v34) accepted four genuine Ember selections: the last
read-only witness retained moves `[10,45,52,0]`, PP `[35,40,21,0]`,
Charmander HP 18/32 and Geodude HP 15/33. The viewed frame at 5,143
shows the ordinary battle animation (Charmander 18/32, Geodude 20/33).
No KO had occurred when the win scenario exhausted its 5,500-frame cap;
the reservation used 7,000 frames including control and 160.365 seconds.
Control passed; win failed at the cap, and carry/loss/Transform remained
untested. The supervisor confirmed exact tree reap and no orphans.
The report is `build/story/sequential-6e78171-standing-run13.report.json`,
SHA `57c882b239946e3c1964b564bb55d83d6712dee17ab9b02cb59dd24a0db80c19`.
The checkpoint's Ember-versus-Rock matchup now appears too slow for
the bounded reservation, rather than input readiness being the blocker.
A new legal sequential-only checkpoint or easier authenticated target
has been referred to the coordinator; neither is approved by this report.

The subsequent user decision approved the new Squirtle checkpoint and separate
18,000-frame/480-second reservations described above. Run14 captured two
ordered natural KOs but refused shell reacquisition: a completed prior
retirement still marked the newly acquired DATA generation retired. The
successful-acquisition path now resets only those completed-generation flags;
the actual lifetime HOST regression reacquires, validates and releases a second
generation and still refuses a copied stale DP token.

Run15 passed that boundary and replayed XP/carry, but postbattle save lacked
the native Pak session. Resume now acquires the session, consumes its result
before applying it and releases the session afterward. Begin refusal alone
retains a result; callback/end refusal fail-stops with the applied marker,
mapping and lifetime retained, so reopening cannot replay earnings. HOST
regressions cover both refusals and the installer's preserved FAILED phase.
The read-only observer also recognizes the identity-bound consumed marker
while blocking Pak I/O still leaves the runner at REOPEN; it continues to
validate the original result/party and does not treat this as replay completion.

Run16 (`c6bdcd9` image) first passed the entire win/save scenario in 7,579
frames/190.157 seconds, with exact reap/no orphans. Its actual carry-note SHA is
`40da60fd82ab6ea22fdae92cadc77a3f4ad659c8e177f37e89483f471dd6f4b1`;
full game-written save SHA is
`87febd6cc0b447e6541088a744473cdcb2584cc0c37995142995a46d4b299b6c`.
This is success-path evidence for that image, not proof of the later
fail-stop correction. Run17 on the corrected image stopped at the observer's
old ready-bit requirement during Pak I/O; it is FAIL, not a producer.
Carry must use a new passing producer on the corrected image.

Run18 (`4508ea8`, v44, byte-identical to the accepted `ed0eec0` image)
passed win in 7,145 frames/180.901 seconds. Run19 passed fresh carry reload in
636 frames/26.071 seconds. Producer, loaded and post-Continue container hashes
all equal `442403848708ae14b445f7fe22fcc3ac55b0ed2eb07284bad5111d7c423a2ae3`;
the actual carry-note hash is
`47f771acef4e87452018876978f95d712d76185dea8bb4963e49a0982022adae`.
Both reservations report exact reap and no orphans. The producer aggregate
report SHA is `525faf52232ec7fcae7e5265affa64dd4e2409d384e771be0f6d29f6a39ce26a`;
the carry aggregate report SHA is
`efadeb469c23cf6d04c5c1483d945d37131e492385f72d0e5a78f9ee784bdc42`.

Run20 exhausted the six-mon natural-loss reservation at 18,000 frames/
446.098 seconds. Squirtle remained the first active mon at 3/33 HP after
12 Tail Whips; the five bench mons were untouched. The viewed framebuffer
shows a genuine Geodude attack, not a title/menu stall. A smaller legal loss
fixture has been requested from the coordinator; it is not approved here.

Run21 exhausted Transform at 18,000 frames/392.594 seconds, with exact reap/
no orphans and no pool failure. Overall scenario is FAIL: neither postbattle
restoration nor save was reached. Read-only final live records show Ditto
species 132 with copied Geodude moves `[33,111,0,0]`; viewed video5/6 shows
Geodude's model beneath Ditto's label facing Onix. This proves an actual
Transform observation, not completion. The aggregate report
`build/story/sequential-4508ea8-standing-run21-transform.report.json` has SHA
`fb2fc6147aede4b6e3461cab2e196b6ee98f3c49079ced34d6245c0a05460b07`.
Final controls remain in the unlocked move menu. A missed move-selection
edge followed by an unbounded acknowledgement wait is the current hypothesis.

Move/party acknowledgement now returns to the ordinary counted input policy
after at most 60 neutral frames, just like action-menu readiness. Unacknowledged
input is never recorded as accepted. The first matching live Transform witness
is also retained in `transform-observed.json` before the next step, without
overwriting it on later frames. Reports expose this as diagnostic evidence;
it cannot turn a capped/incomplete scenario into PASS. HOST regressions cover
missed/retried/delayed edges and retained evidence after a cap.

Run22 still capped at 18,000 frames/399.949 seconds, with exact reap/no orphans.
The first persisted Transform is frame 1,643: Ditto copied Geodude's moves
`[33,111,0,0]` with five PP each. Overall scenario remains FAIL, not restoration
acceptance. The final PP in both run21 and run22 is `[0,4,0,0]`: the earlier
retained PP interpretation was reversed. Run22 aggregate report SHA is
`a28d420caab1467678a68339711655f955037a06350bb5b4c3213c3025b27440`.

Source inspection identifies `side+0x8C4` as the input-dismissable native error
dialog, not an animation lock. `func_8430EF4C`/`func_8430E8E8` dismiss it on
ordinary buttons; `func_8430EBA8` also uses A to confirm native Struggle when
all PP is exhausted. The worker previously sent no input while that field
was set. It now presses calibrated A only at actual move/party dialog modes,
without setting an accepted move marker. Read-only diagnostics retain the
bounded 0x8C-byte dialog record. This is a source-derived stall hypothesis
until a new run verifies the boundary; no historical report is rewritten.
