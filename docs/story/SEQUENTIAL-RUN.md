# Sequential run preparation (source/HOST only)

`tools/rltest/story_sequential.py` replaces the **assumptions**, not the frozen
files, of the old in-battle six-party observer. It reads the resident campaign
control through the diagnostic-only `gScCampaignObserverControl` pointer and
reads UI storage only in `SC_RUN_ACTIVE`; it never writes RDRAM or launches a
core. It checks operation/encounter/variant/ordered-party identity, KO order,
fixed in-battle progression, cumulative postbattle KO-time XP/stat experience
and levels, loss discard, and preservation of fainted HP/status. The shared
budget includes all frames and has no learning exclusion or reload reset.
Native fences, move-choice/evolution fidelity, actual journal codec, battle
menu mapping and Transform visuals still require separate checks.

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
| Exact proposed controller-input policy file | `c1b293a3f0567a2ef001bdedf50d54fb14fdaa6acae943b446b77db59c1c26a4` |
| Source-package 32-bit layout/proof JSON | `3ba1ef3a2a3b7741a35e236982ebc2eaf6be52a312d2fd01d7bfc0104c0191f9` |

`tools/rltest/story_sequential_inputs.json` pins win, carry reload, loss/terminal
reload and reachable Transform **policies**, not a proven native button
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

**Execution remains blocked** until source review, native ELF/source/package
binding, actual isolated Pak-note conversion and reviewed battle-menu
cursor/button mapping are ready, followed by an explicit user grant and one
exclusive Mupen slot. No existing `six_natural` command may silently consume
this package. Old c399 evidence and normal 4 MiB paths are untouched.
