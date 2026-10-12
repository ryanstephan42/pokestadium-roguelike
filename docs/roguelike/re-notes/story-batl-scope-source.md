# Original BATL scope authority: default-off source candidate

This is a default-off SOURCE/HOST/IDO component, not the complete callable
service or a runtime/ROM/emulator clearance. `STORY_SERVICE_CANDIDATE` is not
enabled by any production build target.

`sc_batl_clock_source` is compiled beside the actual static `D_84390120` in
`fragment62_2F74E0.c`. Its IDO object uses HI16/LO16 relocations to that original
translation unit's BSS, not a guessed timer address. The original battle
initializer has guarded scope-open after `func_84301430` and scope-close after
the final `func_800078D4` return, before reset/pop. Those hooks additionally
require `STORY_NATIVE_PROBE` and its active story probe.

The native encoded namespace `0x84300000` decodes using the existing memmap
algorithm to map index `0x33`; **filename fragment 62 is not map index 62**.
`Memmap_SetFragmentMap` retains a monotonic epoch for that original map.
Clear/rebind refuses while the scope is active, borrowed, or permanently failed,
before changing the map fields. Epoch exhaustion also fail-stops. No caller
supplies a battle generation or timer to `sc_batl_scope_open`.

The scope issues separate monotonic battle generations and single-use borrow
tickets. A borrow captures the actual roots, registrations, groups, metadata,
counts, original worker pointers, clock and loader/map epoch. Authentic
changed-view retirement retains the ticket and fails permanently; wrong/stale
tokens cannot retire or poison the legitimate operation. Close additionally
refuses if either original worker has model/move pending bits (`unk_01 & 3`).
That is a **close precondition, not an implemented worker drain**.

The joined service now provides `sc_batl_scope_drain`: it stops admission to the
two captured workers, receives their existing completion queues nonblocking, and
invokes the actual `func_80019B48` consumer only after message/job/queue/permit
checks. Refusal retains the permit and failed scope. The service fixture exercises
this drain, permit retirement, exact DATA/module authority, and reentry together.
The explicit zero-initialized native arena is preserved across reentry; live
controls and allocation generations are never cleared to initialize another battle.

`run_story_batl_scope.sh <fresh-output>` executes ten modes in each of normal
and sanitizer builds: unchanged lifecycle/reentry, root change, real guarded
map set/clear refusal, clock change, registration/group/metadata/count changes,
and worker-pointer change. Tests invoke the real memmap/scope code; roots and
clock are HOST fixtures. Expected `osStopThread` outcomes during map refusal are
intercepted only by the HOST harness, and map/epoch fields are checked unchanged.
No fixture mutation is evidence of native gameplay reachability.

The script also compiles scope, memmap and the whole original clock translation
unit with actual IDO. The original initializer with both candidate/probe guards
has separately compiled successfully. Vanilla `make` still printed
`build/pokestadium-us.z64: OK`; normal `make rl` also built. No fullgate or emulator
was run.

The guarded native initializer and qualified two-phase callers are source-wired
to this service; HOST fixtures simulate the OS, never execute relocated MIPS.
Still required: complete campaign owner/dispatcher integration and joined native
link/import/stack/lifetime/whole-fit accounting and review. In particular, new guarded original-fragment code
can change native symbol placement. The final image authority must be derived
from the **exact candidate native link**, never assume that frozen v3 native
symbol offsets still apply. All v3 and earlier sealed bytes remain immutable;
they are not patched or installed against unreviewed new bindings.
