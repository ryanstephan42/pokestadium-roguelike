# Typed source-only UI driver

`sc_ui_driver.c` connects the accepted value/request UI APIs to **injected**
launch, draw and leave calls. It has no native imports, allocator/image pointers,
BATL permits or memory-admission logic. The driver and request backing are
provided by the caller; `ScUi`/request sizes and lifetime are unchanged. It is
still excluded from all ROM profiles by the existing `src/story/` filter.

The native owner must acquire the reviewed UI/catalog backing, shared render
resources and real save-device binding **before** `sc_ui_driver_bind`. Missing
callbacks/backing or an unprepared/in-flight UI fail explicitly. Binding tests
only structural readiness, not that Expansion Pak admission actually occurred;
passing host fixture callbacks is not native authority.
Driver backing must start zeroed; a bound driver cannot be rebound, including
while a submission or journal retry is pending.

`sc_ui_driver_step` takes edge/repeat button presses. It draws through the
injected presentation call, copies a checkpointed `ScUiBattleRequest` when
`SC_UI_REQUEST` is issued, and calls `launch`. **Launch is a submission callback**:
it must return `SC_OK` only when the owner accepted the copied operation; it must
not deliver observe/finish callbacks synchronously before returning. A refused
submission must retain no borrowed request backing or service work. Accepted
submission may consume/copy the plain values, never attach authority to them.
A final `finish` delivery must follow per-battle worker drain and release of
request backing, before a subsequent request can overwrite it; this ordering
is an owner callback obligation, not proof implemented by the adapter.
An unsuccessful
launch retains the same interrupted checkpoint, operation ID and party values,
and A explicitly retries; B cannot turn it into a successful battle or discard
the transaction. Successful launches are not reissued by repeated A presses.

Deliver persistent native snapshots/results through `sc_ui_driver_observe` and
`sc_ui_driver_finish` with the captured operation ID. Wrong/stale/unlaunched
callbacks fail without state changes. The native caller must pause its
progression while `driver.ui->state.phase == SC_LEARNING`, deliver UI input,
and resume when learning settles to `SC_BATTLE`; this adapter does not schedule
native threads or overlay swaps. Journal retry remains a UI transaction, and
duplicate result delivery cannot launch another battle.

`leave` is called once on a resolved `SC_UI_EXIT`; no later step is accepted.
There is no unbind/destroy shortcut for pending saves or battles. The native
owner retains the display-loop lifecycle and all loader/AI/battle authority.

`bash host_tests/run_story_ui_driver.sh` exercises these calls with explicit
host fixtures against real campaign/journal APIs, ASan/UBSan and IDO. This is
submission/refusal proof only, not native launch or visible-game execution.
The sequence completes/saves the first HOST battle, resolves its reward, exits
to the menu and continues the persisted campaign, then requests a different
encounter with a different operation ID and refuses the old result. The
campaign driver/backing lifetime spans both per-battle requests; no native
generation, root/import pointer or loader lifetime is held in UI values.
The concrete blockers remain owner-provided admitted backing, graphics/Pak
acquisition and reviewed launch/pause/result scheduling. No shared runtime or
build edits were made.
