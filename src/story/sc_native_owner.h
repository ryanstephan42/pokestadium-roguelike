#ifndef SC_NATIVE_OWNER_H
#define SC_NATIVE_OWNER_H

#ifdef STORY_NATIVE_UI
#include "sc_native_ui.h"
#include "sc_module_arena.h"
#include "src/6BC0.h"

enum ScNativeEventKind {
    SC_NATIVE_EVENT_NONE, SC_NATIVE_EVENT_OBSERVE, SC_NATIVE_EVENT_LEARNING,
    SC_NATIVE_EVENT_DRAINED, SC_NATIVE_EVENT_FAILED
};
#define SC_NATIVE_FRAMEBUFFER_BYTES (640U * 480U * 2U)
typedef struct ScNativeFramebufferSpan {
    RlU8 *pixels;
    RlU32 bytes;
} ScNativeFramebufferSpan;

typedef struct ScNativeCampaignEvent {
    ScBattleSnapshot party[6];
    /* generation is the nonzero, strictly increasing event serial within this
       operation, NOT a BATL scope/module/allocator generation. */
    RlU32 operation_id, generation;
    unsigned kind, active_slot, enemy_slot, enemy_hp, outcome;
    int error;
} ScNativeCampaignEvent;

/* Private native control only; never serialize this lease into campaign values. */
typedef struct ScNativeCampaignBacking {
    ScUi *ui;
    ScUiBattleRequest *request;
    ScUiDriver *driver;
    ScNativeUi *native_ui;
    ScNativePak *pak;
    ScNativeCampaignEvent *event;
    const ScCatalog *catalog;
    /* Campaign arena owning this backing; UI resources take their upper slab from it. */
    ScMaArena *arena;
    RlU32 seed, lease_id;
    ScNativeFramebufferSpan framebuffer[2];
} ScNativeCampaignBacking;

/* Framebuffer spans are private native backing, never campaign values.
   acquire admits exactly two SC_NATIVE_FRAMEBUFFER_BYTES spans only on an
   actual 8 MiB machine. pixels is a 64-byte-aligned cached KSEG0 CPU address;
   physical = CPU - 0x80000000, wholly within [0x400000,0x7da7e0).
   Both spans are mutually disjoint and disjoint from every other live arena
   allocation, with exclusive read/write, non-executable leases. The owner
   validates exact pointers/sizes, CPU/physical mapping, extents and lifetime;
   address shape alone is not allocator authority. No second arena, lower-pool
   fallback, global pool enlargement or crash-tail allocation is permitted.
   UI initializes descriptors for these spans but does not allocate, free or
   rebind their pixels. The owner pins both spans and external display/control
   storage (including embedded descriptors/projection/viewport) throughout
   poll/hold/resume/BATL and release PENDING. CPU cache maintenance and RSP/RDP
   addressing must use the admitted mapping and its actual cache/alignment
   rules, never a silently substituted buffer.
   Successful task completion/worker drain alone does not retire scanout.
   Before popping graphics/resource/display scopes, the loop closes admission
   to new display frames, fences submitted work and restores the borrowed
   stage without freeing display storage, then calls retire_display outside
   the Pak session. images must be the exact two embedded descriptors in the
   owner-bound external display control, referencing these exact spans.
   The callback validates this identity and drives a bounded real handoff to
   an owner-admitted live previous or native safe fallback framebuffer.
   No previous stage is not proof of safety: admit the actual fallback
   descriptor/mapping and its presentation mode, then submit/drive its swap;
   a NULL desired-context pointer alone is insufficient.
   Merely selecting the previous context does not submit/swap a restored
   frame. SC_OK requires no A8C/A9C/AAC or queued scheduler descriptor/work
   references to the lease, retired RSP/RDP work and actual VI current/next
   buffers outside both physical spans. PENDING retains every scope, pixel
   span, descriptor and control; retries do not resubmit display closure or
   reopen input. Refusal cannot authorize scope cleanup.
   After retire_display SC_OK, final cleanup may pop resource/display scopes
   without issuing another task or swap referencing the lease. release SC_OK
   additionally validates that this retirement remains true. Until release
   succeeds, no successor acquisition, reuse or unwinding external backing
   storage is permitted. */

/* All callbacks validate the exact live lease/backing tuple and return SC_OK,
   SC_PENDING or an explicit ScStatus refusal, never a Boolean.
   acquire publishes a complete usable tuple only on SC_OK; otherwise clears it.
   submit copies values only on SC_OK; PENDING/refusal owns NO request/work.
   Activation, exit and release happen outside the Pak input session.
   poll requires event == backing->event. The event is an output-only buffer
   that is all zero between deliveries: the owner retains the queued identity
   privately and refuses any non-zero byte found on entry. The loop consumes
   and zeroes it after every poll. SC_OK delivers each event once, in strictly
   increasing serial order; NONE/PENDING/refusal leave the full payload zero.
   LEARNING contains a held KO observation, already paused before presentation.
   After observe it must actually require learning; resume is allowed only after
   presentation/settlement to SC_BATTLE with no learn_mask. Resume validates the
   same operation/held serial, party order and progression, copies values (never
   retains pointers), and consumes the hold once on SC_OK. PENDING retains it.
   DRAINED follows all final observations and settled learning, exactly once,
   after workers/service/permit retire and BATL scope pops. Dispatch finish once;
   save-error retries must not replay that event, battle or reward.
   release PENDING retains the entire lease/backing; SC_OK retires it once, only
   after graphics resources and submitted tasks have already retired. */
typedef struct ScNativeCampaignOwner {
    void *context;
    int (*acquire)(void *context, ScNativeCampaignBacking *backing);
    int (*validate)(void *context, const ScNativeCampaignBacking *backing);
    int (*submit)(void *context, const ScNativeCampaignBacking *backing,
                  const ScUiBattleRequest *request);
    int (*poll)(void *context, const ScNativeCampaignBacking *backing,
                ScNativeCampaignEvent *event);
    int (*resume)(void *context, const ScNativeCampaignBacking *backing,
                  RlU32 operation_id, RlU32 generation, const ScState *settled,
                  const ScCombat *progression);
    int (*retire_display)(void *context, const ScNativeCampaignBacking *backing,
                          const unk_D_80068BB0 *images);
    int (*release)(void *context, const ScNativeCampaignBacking *backing);
} ScNativeCampaignOwner;
#endif

#endif
