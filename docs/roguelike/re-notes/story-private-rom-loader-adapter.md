# Private Cart-ROM loader adapter: isolated SOURCE/HOST candidate

## Scope and outcome

Coordinator authorized private caller-level adapter implementation as isolated/
noninstalled output, plus optional native-header/MIPS side compilation.
No runtime wiring, ROM installation/build/fullgate/emulator/retry/merge.
Existing singleton global PI manager/arbitration and normal/global paths are
unchanged. DD/Leo/driveROM are unsupported before private story dispatch.
Prior transport-error evidence and seals are unchanged.

Implemented `host_tests/story_private_rom_loader.{h,c}`, isolated native API
bridge `story_private_rom_native.{h,c}`, test/runner
`test_story_private_rom_loader.c`/`run_story_private_rom_loader.sh`.
At the original isolated review, none was in a production Makefile. The later
default-off `STORY_SERVICE_CANDIDATE` build compiles this loader and native bridge
for the joined service. The bridge calls actual osEPiStartDma, not raw PI registers
or a second manager. HOST fixtures and target compilation do not authorize native
execution or establish complete campaign ownership, fit, or retirement.

## Ownership, request and queue contract

Pure core copies one request and immutable callback table into persistent
caller-owned loader state; request includes expected allocation generation/
operation ticket, physical staging extent, trusted ROM offset extent.
Initialization requires zeroed never-used object. Reentry/start on any
non-IDLE state refuses; no reset/retry/free API exists. Preparation/validation
states prevent duplicate poll/submit during callback processing.
Callbacks and storage must be private, trusted and single-caller; their C
visibility is not a capability or concurrency proof.

Exact8MiB + admitted cold route + exclusive writable staging + no held locks
before cache or enqueue. Physical RAM owner starts>=400000, ends<7DA800;
requested byte span and16-byte cache-rounded span both fit that owner.
Native bridge reads osMemSize==800000 without mutation; normal lower4MiB
pool/gExpansionRAMStart remain unchanged. Physical input is converted to
KSEG0 for OS DMA; arbitrary KSEG/TLB/device aliases are not accepted as RAM.
Device namespace is ROM **offset**[0,10000000), full trusted source extent;
cart baseB0000000 OR offset produces expected cartridge physical address.

Pure preflight enforces actual raw/API read-domain predicates: READ only,
RAM8-byte/device2-byte alignment, positive even size<=16MiB, full ordered
owner/device spans and overflow guards. Native bridge additionally checks
OSPiHandle typeDEVICE_TYPE_CART, domainPI_DOMAIN1, baseB0000000 before cache/
enqueue. Cart pointer must be the trusted captured existing startup handle;
struct-field checks alone do not authenticate a forged caller handle.

Native bridge owns one static60-byte o32 transport: persistent OSIoMesg,
OSMesgQueue, one cell, captured handle and single-attempt flag. Queue is
created empty only once immediately before enqueue; retQueue is private,
never passed to timer/other producer, and only singleton PI manager receives
its address. Only one request can be outstanding; sole producer means one
ordinary Cart completion fits empty one-slot queue. This excludes64DD track
multi-post mode by exact cart handle. Context is opaque but its ABI check
does not prove deployed route privacy/callsite coverage.

Preparation uses osWritebackDCacheAll then osInvalDCache(staging,bytes);
source `invaldcache.s` size>=8192 is whole index invalidation, so writeback
before it avoids silently losing dirty unrelated data. Exclusive staging
borrow must span cache/submit/completion/validation; no concurrent CPU write.
The native functions implement calls, **not** a proof of serialization, real
hardware DMA padding or instruction-publication correctness. Published executable
span still requires reviewed I/D-cache sync supplied by the final callback.

## Completion, fail-stop and publication

osEPiStartDma -1 is definitive no enqueue for this exact API; adapter rejects
without accepted-transfer retention.0 is queued only. Unexpected result/null
expected identity conservatively enters retained fail-stop. Native attempt
flag blocks subsequent retry even after known queue rejection; no handle/
queue reinitialization after any attempt.

Core polls receive nonblocking. Empty queue remains queued until an existing
authorized deadline expires (no new watchdog/budget); expired deadline is
fail-stop even if a late completion is now available. Wrong pointer/unknown
receive/error is fail-stop before any data validator. Expected message is
the persistent OSIoMesg pointer, not a zero/null/sharedrsp ack.
No reused request object/ticket permits an ABA completion: transport and loader
are one-shot. Post-completion validation consumes only once.

Fail-stop calls mandatory explicit refusal callback with reason/retained=true.
It retains request, queue, staging and allocator owner indefinitely; no
reset/pop/free/reuse/retry on this route. Caller must stop campaign operations
and report error without destroying backing, not timeout-return to menu and
pop. Timeout cannot establish hardware stopped. A later PI post can safely
land in the still-live queue but cannot salvage/restart the failed loader.
The actual singleton manager raw-start error/access-release problem remains
global hardware liveness uncertainty, not repaired by caller-private queue.

On exactly expected completion, invoke mandatory trusted digest -> container
-> import -> relocation -> final publication-cache callbacks in order, then
publish state last. Any false outcome retains/poisons logically via fail-stop,
with no published callable pointer. Core supplies no success default; all
callbacks including explicit refusal are required.

Digest callback must compare complete received source bytes to expected digest
from trusted sealed manifest, not header-supplied digest. Container validates
whole file spans/overflow/alignment and actual initialization; imports require
expected battle/image generation and allowlist; relocation validates every
target before mutation; final cache sync completes before export. These
trusted byte validators are **injected interfaces**, not newly implemented
crypto/parser/relocator. HOST tests mock them and prove ordering/refusal, not
actual digest/relocation/import safety. Native bridge supplies only preparation,
enqueue/receive. Until real trusted callbacks/owner authority/fail-stop caller
are reviewed, this is not an installed loader or callable module.

## Source fidelity and tests

Pinned actual APIs/predicates:
`lib/ultralib/src/io/epidma.c`, `epirawdma.c`, `cartrominit.c`, `devmgr.c`;
PR/os_pi.h/rcp.h and R4300 cache headers; prior transfer report's exact
return/post error distinction. No libultra source copied into new implementation.

```
bash host_tests/run_story_private_rom_loader.sh .local/rltest/story-private-rom-host-v1
bash host_tests/run_story_private_rom_loader.sh .local/rltest/story-private-rom-reproduction-v1
```

Normal and ASan/UBSan each report311 assertions in both runs. Tests cover18
invalid-domain/span/permission/generation inputs; every mandatory validation/
cache failure; known rejection vs queued/unknown outcomes; wrong/empty/error/
expired/late completion; expected identity, persistent copied request, one
outstanding enqueue/no retries/post-publication consumption; source/destination
edge/cache-rounded backing. All device/cache/byte-validator outcomes are
explicit mocks; no native operation or staging bytes transfer occurs.

## Native ABI side output and remaining measurements

Existing clang22.1.8, no compiler install. `--target=mips-unknown-linux-gnu
-mabi=32 -march=mips3 -EB -ffreestanding -O2 -fstack-usage -Wall -Wextra
-Werror`; native bridge additionally repository `-D_LANGUAGE_C -DLANGUAGE_C
-Ilib/ultralib/include -Iinclude -Ihost_tests`, core `-Ihost_tests`.
Objects at ignored `.local/rltest/story-private-rom-host-v1/`.
Static assertions verify OSIoMesg24,OSMesgQueue24,transport60,
SprRequest44,SprLoader100bytes. Compiler object aggregate text812 bridge/
1900 core,bss60 bridge (section accounting, not installed link growth).
Per-function compiler stack reports: enqueue56,receive48,prepare-cache40;
core start48,poll40,preflight0;transport accessor0bytes.
These are o32 big-endian MIPS3 **side outputs**, not IDO/link/ROM candidate.
No full-stack estimate: caller/OS/cache/refusal/byte-validator/import/relocator
frames and simultaneous payload/control lifetime are not included.

Native ABI compilation resolves header types/signatures, not linker storage
ownership, exact original-toolchain fit or runtime task/lifetime. Initial
clang VR4300 name was unsupported; MIPS3 target used. Repository C defines
were required. Final objects are warning-clean; no native execution.

Still blocked: actual allocator-issued admission/handle provenance; actual
trusted bytes validators; caller fail-stop retention/reporting; concurrent
cache/backing/queue lifetime coverage; linker control/staging spans and lower
bootstrap/full stack/peak/fit. Independent full078D4->producer stop->actual
two-loader pending consume-before-reset/pop, private battle import/borrow
revoke/unwind, side-aware native bank transaction/forced-write guard/rollback,
RNG/cache provenance/Struggle4 domain and new CPU/VI references remain gates.
Adapter removes legacy sharedrsp completion ambiguity, not global PI-manager
error/liveness uncertainty or these independent obligations. Stop at validator
review; no installed eligibility or safe-fit claim.
