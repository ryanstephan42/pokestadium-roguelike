# Upper campaign UI resources

The story campaign uses `rl_ui_story_init(ScMaArena *)` through
`sc_native_ui_acquire(native, pak, arena)`. The caller supplies its already
admitted campaign arena; this code does not admit Expansion memory or change
the global main-pool allocator. Ordinary `rl_ui_init()` remains unchanged.

One non-executable DATA allocation holds the complete campaign UI resource
set. All 151 usable species icons are loaded at acquisition, together with
fonts 2/3/4 and their owned descriptor, archive tables and move/species name
blobs. The borrowed font descriptor is never modified. The checked icon cache
cannot fall back to native pool allocation if an owned entry is missing.
Consequently BATL and held-learning draws/input do not allocate resources.

## Target charges

| Upper component | Bytes |
|---|---:|
| Owned font descriptor, aligned | 96 |
| Font archive table | 112 |
| Icon archive table | 2,448 |
| Species and move name blobs | 4,096 |
| Glyphs 2/3/4 | 190,672 |
| 151 raw icons | 483,200 |
| Reusable acquisition scratch | 16,464 |
| **Single upper allocation** | **697,088** |

`sc_ma_alloc` aligns the block base to 32 bytes. Its other allocations have
16-byte sizes, so this allocation may introduce up to 16 bytes of placement
padding. Slot/lease metadata lives in the existing arena control; do not charge
it again as another allocation. The text archive table uses scratch and is not
retained. Scratch remains in the slab for the entire parent lifetime.

The lower restore record requests 76 bytes, charged 96 under the native
`ALIGN16(request) + 16` pool rule. It includes the prior font pointer, prior
renderer resource slots, arena handle/lease and acquisition state. With the
32-byte resource-parent scope, retained lower resources total **128 bytes**.
There is no separate font restore, text restore, substitution table or glyph
destructor allocation on the upper path.

Acquisition uses the existing ROM-read DMA dispatcher in chunks of at most
4,096 bytes, checks dispatcher allocation failure before waiting, and checks
the reply. Its temporary dispatcher request is not retained across BATL.
Completion uses a story-owned, one-slot queue initialized once in the resource
module, not the private queue in `3FB0.c`. The synchronous campaign acquisition
submits one chunk and consumes its completion before submitting another.
Queue, message slot and initialization flag add 32 bytes of target module
storage before section alignment; they do not change the slab or lower-pool
charges. The queue remains resident with the module through acquisition and
retirement, including crash retention.
The native worker provides cache invalidation for ROM reads; the existing
PERS-SZP decompressor provides decompressed-cache writeback.

## Lifetime and failures

The slab remains live across route, BATL, learning, reward and terminal screens.
Release requires the existing UI transaction/exit checks and `tasks_retired`.
Prior font/resource pointers are restored **before** the DATA lease ends and
the arena block is released. That sequence disables interrupts and rechecks
the resident crash-takeover flag before modifying any pointers or lease state;
a takeover after earlier display-retirement validation refuses release.
Interrupt state is restored on success and refusal. This exclusion is not a
GPU/VI fence: the caller still owes the existing `tasks_retired` proof.
The lower scope then pops.

Failed acquisition rolls back unpublished resources through the lower restore
callback. A forced scope pop after successful acquisition but before retirement
does not release or reuse the slab: it logs an explicit lifetime error and keeps
the outstanding resource count nonzero, preventing image retirement. This is
a fail-closed ownership violation, not a recoverable campaign exit.

## Verification scope

`bash host_tests/run_story_upper_resources.sh` checks the real resource and arena
code using 32-bit generated archive fixtures, including all 151 icons, bounds,
malformed-header rollback, failed DMA submission without a blocking receive,
borrowed-font preservation, stale leases and retirement refusal. It runs
ASan/UBSan and compiles the affected production units with IDO.

For a dependent branch lacking the tracked campaign arena, set
`SC_MA_SOURCE_DIR` to an authorized frozen source directory. These checks are
not native execution or simultaneous-fit acceptance. The owner must thread the
arena through its admitted backing producer, include the new unit in its link, remeasure
SC image growth, and qualify the combined arena/lower-pool ledger. The existing
pixel/backing layout is unchanged.

The legacy `run_story_native_resources.sh` controller/lifecycle fixtures are
also updated for the arena argument. Their resource initializer and retirement
are explicitly fixture-bound to the previous lower-pool model; they do not
prove upper loading or upper retirement. Those are covered by the new runner.
