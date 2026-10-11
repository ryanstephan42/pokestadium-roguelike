# Roguelike persistence (W6)

## Storage ownership, Stadium US

The plan's EEPROM/free-space suggestion is not an established storage layout.
No cartridge save address is reserved for this mod.

| Device | Evidence in this checkout | W6 policy |
|---|---|---|
| Cartridge FlashRAM, 128 KiB | `src/D470.c` implements flash initialization, 128-byte page programming and sector erase; `src/3FB0.c:func_80003A14/func_80003B04` dispatch flash page reads/writes/erase. `src/26820.c:func_800284B4/func_8002873C` owns four primary and four backup sectors. Installed Mupen exposes the cartridge flash region in its save-memory image. | Never access it. Unused-looking padding is not a safe dedicated region. |
| SRAM helper, 32 KiB at PI physical `0x08000000` | `src/rsp.c:func_80000628` defines an SRAM handle and DMA helpers in the disk/RSP infrastructure. Its existence does not prove retail cartridge SRAM or available mod capacity. | Never access it. |
| EEPROM wrappers | `src/controller.c` probes EEPROM and implements 8-byte-block transfers; no gameplay caller of `Cont_AttemptReadEeprom/Cont_AttemptWriteEeprom` was found. SDK 4 Kbit = 512 bytes, 16 Kbit = 2048 bytes; these are possible device sizes, not proof of an installed Stadium EEPROM. | Never access it; no claimed free gap. |
| Standard Controller Pak | 32 KiB, 128 pages of 256 bytes; first five pages are filesystem metadata, 123 data pages and 16 directory notes. SDK APIs are in `lib/ultralib/src/io/`. | Separately named note only; accept a validated one-bank filesystem. |
| Transfer Pak / GB cartridge | `src/gb_tower.c` and related GB paths own the accessory/cartridge interfaces. | No GB API, bank switch, power command, or cartridge write from W6. |

Vanilla flash sectors occupy primary offsets `0x00000`, `0x04000`, `0x08000`,
`0x0c000`, with mirrors at `0x10000`, `0x14000`, `0x18000`, `0x1c000`.
The corresponding record lengths in `26820.c` are `0x3700`, `0x3700`, `0x3f80`,
`0x3880`. Writes erase whole sectors before page programming. Padding belongs
to those sectors, not to the mod. This is address/ownership evidence, not a
new semantic naming of every vanilla record.

## Native API and errors

`src/roguelike/rl_save.h` exposes:

```c
RlSaveStatus rl_save_load(unsigned controller, RlMeta *meta,
                          RunState *run, RlU8 *has_run);
RlSaveStatus rl_save_store(unsigned controller, const RlMeta *meta,
                           const RunState *optional_run, int create);
```

Controller indices are 0..3. Call synchronously from the game thread between
completed controller input reads, not between `Cont_StartReadInputs` and
`Cont_ReadInputs`, which already hold the SI mutex. The native adapter acquires
the existing EEPROM/SI semaphore and releases it on every outcome. Its shared
metadata scratch is protected by that semaphore. Keep caller run/meta state in
resident storage: the stock game-thread stack is 8 KiB, and the deepest target
storage/codec call chain uses about 6 KiB. The diagnostic mailbox's large
fixtures also reside in BSS rather than exhausting that stack.

`RL_SAVE_OK` publishes a valid load. `RL_SAVE_RECOVERED` also publishes the
surviving copy but **must be displayed as a warning**; saving refuses to replace
the damaged committed copy. All other failures leave load outputs unchanged.
Statuses distinguish not found, no device, unsupported accessory/filesystem,
device changed, page/directory full, I/O error, bad CRC, unknown version, invalid
data and bad API arguments. `rl_save_status_text` supplies ASCII descriptions.
Do not convert a failure into a new `RlMeta`. Only an explicit user decision to
start new progression should pass `create=1`; even then existing CRC/version/
filesystem failures are preserved. There is no automatic deletion, formatting,
repair, migration, unlock policy, or fallback to cartridge storage.

The note uses private company code `0x524c`, game code `0x524c3031` (`RL01`),
Controller Pak font name **STADIUM RL**, extension **V1**. Identity includes all
four values. A same-name different-owner note is never reused. Exactly 2560
bytes (10 pages, one directory slot) must be available. Optional suspension
fits this explicitly allocated budget; it does not borrow additional space.

## Filesystem safety

`osPfsInitPak` is deliberately **not** called. Its implementation can repair
IDs, invoke `osPfsChecker` (which deletes damaged unrelated directory entries),
and, in later SDK revisions, temporarily overwrite block 0 during device
detection. `__osPfsRWInode` can also repair bad checksums when asked to read.

`rl_pak_validate` instead checks a read-only snapshot of the first 1280 bytes:
primary ID checksum/device/bank count, identical backup IDs, matching primary/
mirror inode tables and checksum, every directory chain, loops/cross-links,
orphan allocation, exact size/unique identity of our note. Unsupported,
unformatted, damaged, multi-bank and non-memory accessories are rejected.
No probe writes are made before positive memory-filesystem identification.

Only creation uses the stock `osPfsAllocateFile` allocator after validation,
through RL-only extracted/renamed SDK objects in `rl_save.mk`; vanilla discards
these objects. The inode cache is invalidated first. A new read-only snapshot
then verifies the allocator's result, including identity and page ownership.
New note contents are explicitly zeroed, and its own directory status is marked
occupied. Subsequent loads/updates use raw 32-byte SI transfers mapped only to
the validated note's pages; they do not call SDK read/repair routines. Device ID
is rechecked around transfers. No other note payload, ID, label, or block 0 is
written by this adapter.

Allocation is not hardware-atomic: power loss can interrupt the inode mirror
and directory updates. Preexisting file payloads are never intentionally
rewritten, but a torn filesystem may require external recovery. The adapter
will report invalid data and will not repair it. Device swaps during a single
hardware command cannot be made atomic; do not remove the Pak during saving.

## Framing and update protocol

The current `rl_save_format` payload is version 4: explicit little-endian
encoding, inner IEEE CRC32, 416 bytes meta-only or 972 bytes including a run.
Neither C structures nor padding are passed to its decoder. The decoder also
accepts version 3 (361/917 bytes) and migrates its saved fields unchanged;
new-profile currency/options/species fields start empty. Re-encoding writes
version 4. The maximum padded payload is 992 bytes, within the existing
1280-byte slot; the Controller Pak allocation and ownership do not change.

`rl_meta.c` owns progression policy. A new profile unlocks starter-pool bit 0,
relic bits 0–7, event bits 0–7, and the curated first-stage species whose Gen 1
base-stat totals are at most 320. This cutoff is a conservative implementation
proposal, not a user-selected threshold. Their species IDs are
1, 4, 7, 10, 13, 16, 19, 21, 23, 25, 27, 29, 32, 37, 39, 41, 43, 46, 48, 50,
52, 56, 60, 63, 66, 69, 74, 79, 84, 90, 104, 116, 118, and 129. Terminal saves
persist pending eligible species atomically with run statistics and currency,
so choices survive a cold reload. Player-facing rule: a terminal run earns
one registration choice only if its final team includes at least one eligible
species that is neither already registered nor already in the pending pool. A
repeat run containing only already-registered or already-pending eligible
species earns no additional choice, but still records run statistics and
currency. When a run adds a new eligible species, the profile accumulates the
union of those species plus one receipt for that run. Registering one species
from the union or explicitly skipping consumes exactly one receipt;
already-registered species are idempotent and cannot consume another receipt.
The final receipt clears the remaining union. Receipt count is bounded by the
151 distinct species IDs; capacity is checked before profile mutation. Species
registration unlocks species only; the draft integrator must build the selected
Pokémon from its canonical legal rental loadout.

Successful wins unlock Prime Cup starters on the first win and Little Cup
starters on the fifth; each win also unlocks the next relic and event bit,
starting at bit 8 and stopping at the end of each catalog. `meta.ascension` is
the highest unlocked ascension (initially 0), not the current run selection.
Clearing that highest unlocked level unlocks the next, through level 10; the
selection helper accepts any level from 0 through the unlocked maximum without
changing the saved maximum. A run's floor is `act * 7 + row + 1` when a row has
been entered, or `act * 7` before its first row. Both wins and losses update
the saturated run count and best floor; only wins update the saturated win
count, unlocks, ascension, and Hall of Fame team. The Hall of Fame retains the
latest winning team's records.

`rl_meta_permitted_pools/relics/events` return the saved unlock bits masked to
their catalog sizes, so reward and shop offer builders can intersect their
candidate masks with these policies. `rl_meta_choose_event` selects uniformly
from permitted event IDs using only the caller-provided RNG state; repeated
calls with the same state and unlocks produce the same choice. No unlocked
events yields `RL_EMPTY` without consuming RNG or changing the output.

The recording API accepts only terminal won/lost states and refuses invalid
or not-yet-unlocked ascensions. The terminal-summary integration owns
exactly-once recording: it prepares and caches a candidate `RlMeta`, saves it
atomically without a terminal suspend, and retains that candidate across an
uncertain write until reload confirms whether it committed. Currency and the
pending species choices are part of that same candidate, so neither can be
awarded twice or detached from the final team on retry/reentry.

The current economy constants are conservative implementation proposals,
not user-approved balance values: 10 coins per completed won encounter, 5 per
other completed encounter, and a 25-coin bonus for a full run clear. Payout is
granted only when the run reaches a terminal won/lost state, so abandoning a
run cannot farm currency. The implemented option price proposals are 30 coins
for each of four canonical rental move-set sources, and 50 for each of four
starter themes or the Potion Cache sidegrade. The sidegrade converts 200 of the
existing 300 starting coins (the proposed Act-0 Potion price) into one Potion;
it does not add starting value. Event-pack, challenge-contract, and cosmetic
IDs remain reserved and cannot be purchased until they have implemented
content. The saved option bitfield preserves room for those future IDs, but
they are not presented as available options. These options do not add
permanent combat stats, unbudgeted consumables, or reward odds. Species
selection weights are proposed as `max(1, 1000 / BST)` over distinct species
in the supplied legal loadout catalog, making higher-BST species rarer. Revisit
all values after native UI integration and balance playtesting.

The note holds two 1280-byte slots. Each begins with a 32-byte little-endian
commit header: `RLPK` magic (offset 0), framing version 1 (4), sequence (8),
actual payload length (12), payload CRC32 (16), eight reserved zero bytes (20),
header CRC32 over the first 28 bytes (28). Payload starts at offset 32; transfers
round its length up to 32 bytes and zero padding, but decoding uses only the
stored actual length. An entirely zero header is uncommitted/empty.

On update, validate both slots, choose the latest modulo-32-bit sequence, and
target the inactive slot. Equal or half-range-ambiguous sequences are rejected.
Invalidate that slot's header, write/verify its padded payload, commit the header
last, then reread/validate and compare the exact record. Never invalidate the
latest valid slot. Interrupted payload writes leave the other copy usable;
a torn commit/invalidation may report `RECOVERED` rather than silently succeeding.
I/O failure after the commit may mean the new save exists: reload before retry.
Unknown versions block both loading and saving, even if another slot is readable.
Committed corrupted bytes are never overwritten, including with `create=1`.

## Verification and integration

The runtime's separate permanent-profile edit transaction compares full encoded
old/candidate records, including the retained suspension, before uncertain
purchase/register/skip retries. See [PERMANENT-UI.md](PERMANENT-UI.md) for controls,
ordinary draft effects and reproducible native migration/repeated-loss checks.

`make -C host_tests save-test` runs strict C89 and ASan/UBSan memory fixtures:
roundtrips, suspension, exact v3/v4 lengths, v3-to-v4 migration through the
storage wrapper with active-run RNG/map/party/HP/PP/status preservation, all
481 byte-level cuts across a meta update, read failures at all seven update
reads, terminal pending-species/currency cold reload and uncertain-commit
retry, retained prior records, generation wrap/ambiguity, CRC/version errors,
no-device/full-storage adapter errors, and read-only Pak validation/
cross-links/loops.

`python3 tools/rltest/save_roundtrip.py` uses the installed software Mupen core
and a newly isolated save directory. It executes **the runtime native API**
through the inert-by-default `gRlSaveProbe` mailbox at RL entry, not a Python
replacement storage implementation. It saves meta, updates to suspended run,
copies libretro save RAM to disk, starts a fresh emulator process and restores
save RAM (no savestate), then reloads via the same runtime API. It also checks
runtime missing-device/full-filesystem/damaged-filesystem/CRC/version errors.
The synthetic unrelated vanilla-owned note, every other Pak byte outside the
new note's allocated pages/directory/inode entries, and all cartridge save
memory must remain byte-identical. The first vanilla Menu Select exit initializes
fresh FlashRAM; the script stabilizes that transition before taking the baseline.
The probe returns immediately to Menu Select and changes no game progression.

The installed `/usr/lib/libretro/mupen64plus_next_libretro.so` exposes 296960
bytes of aggregate save RAM and supports the memory-Pak option. Roundtrip,
fresh-process reload, byte preservation and runtime failure fixtures passed
on this core. This is an emulator result, not a physical Controller Pak
power-loss test. If a different core cannot expose/support this hardware, the
test fails explicitly; do not claim a roundtrip based on the host codec alone.
Temporary aggregate save images are kept only in ignored `.local/` directories
and cleaned after the test. Never commit game save images.

All W6 sources are included only by the roguelike source filter and RL linker.
`scripts/rl-gate.sh` remains the required vanilla/roguelike/host/emulator gate.
UI integration must render the returned error/warning and require explicit
creation; the storage API does not automatically load or award progression.
