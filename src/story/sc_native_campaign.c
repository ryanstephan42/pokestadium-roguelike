#if defined(STORY_SERVICE_CANDIDATE) && defined(STORY_NATIVE_UI)
/* Persistent-image (fragment 0xe9) production owner adapter. Controls are
   resident (sc_campaign.c); this code runs only while the image is mapped and
   the installer holds the LIFETIME pin. */
#include "sc_native_campaign.h"
#include "sc_native_loop.h"
#include "sc_data.h"

static ScCampaignControl *own(void *context) {
    ScCampaignControl *c = sc_campaign_bound();
    return c && context == c ? c : 0;
}

static int acquire(void *context, ScNativeCampaignBacking *backing) {
    ScCampaignControl *c = own(context);
    if (!c) return SC_INVALID;
    return sc_native_lifetime_acquire(&c->lifetime, c->arena, &sc_catalog, c->seed, backing);
}

static int validate(void *context, const ScNativeCampaignBacking *backing) {
    ScCampaignControl *c = own(context);
    return c ? sc_native_lifetime_validate(&c->lifetime, backing) : SC_INVALID;
}

/* The hook is installed before submit so a refused install owns no request;
   a refused submit uninstalls it again (nothing armed yet). */
static int submit(void *context, const ScNativeCampaignBacking *backing,
                  const ScUiBattleRequest *request) {
    ScCampaignControl *c = own(context);
    int status;
    if (!c || c->installed) return SC_INVALID;
    status = sc_native_terminal_install(&c->binding, &c->terminal, &c->operation, backing);
    if (status != SC_OK) return status;
    c->installed = 1;
    status = sc_native_operation_submit(&c->operation, backing, request);
    if (status != SC_OK) {
        if (sc_native_terminal_uninstall(&c->binding) != SC_OK) return SC_INVALID;
        c->installed = 0;
    }
    return status;
}

/* Settles the launch hook: a PENDING raw-flag drain is retried by the owner
   with the recorded flags; a drained OK uninstalls; a refused arm/drain/abort
   stays installed (HOOK pin retains the image) and is reported. */
static int settle(ScCampaignControl *c, const ScNativeCampaignBacking *backing) {
    ScNativeTerminalBinding *b = &c->binding;
    if (!c->installed || !b->drained) return SC_OK;
    if (b->status == SC_PENDING)
        b->status = sc_native_terminal_drain(&c->terminal, &c->operation, backing,
                                             b->player_alive, b->enemy_alive);
    if (b->status == SC_PENDING) return SC_OK;
    if (b->status != SC_OK) return b->status;
    if (sc_native_terminal_uninstall(b) != SC_OK) return SC_INVALID;
    c->installed = 0;
    return SC_OK;
}

static int poll(void *context, const ScNativeCampaignBacking *backing,
                ScNativeCampaignEvent *event) {
    ScCampaignControl *c = own(context);
    int status;
    if (!c) return SC_INVALID;
    status = settle(c, backing);
    if (status != SC_OK) return status;
    return sc_native_operation_poll(&c->operation, backing, event);
}

static int resume(void *context, const ScNativeCampaignBacking *backing, RlU32 id,
                  RlU32 serial, const ScState *settled, const ScCombat *progression) {
    ScCampaignControl *c = own(context);
    return c ? sc_native_operation_resume(&c->operation, backing, id, serial, settled,
                                          progression) : SC_INVALID;
}

static int retire_display(void *context, const ScNativeCampaignBacking *backing,
                          const unk_D_80068BB0 *images) {
    ScCampaignControl *c = own(context);
    return c ? sc_native_lifetime_retire(&c->lifetime, backing, images) : SC_INVALID;
}

static int release(void *context, const ScNativeCampaignBacking *backing) {
    ScCampaignControl *c = own(context);
    int status;
    if (!c) return SC_INVALID;
    if (c->installed) {
        status = settle(c, backing);
        if (status != SC_OK) return status;
        if (c->installed) {
            if (c->terminal.armed || c->binding.drained) return SC_PENDING;
            if (sc_native_terminal_uninstall(&c->binding) != SC_OK) return SC_INVALID;
            c->installed = 0;
        }
    }
    return sc_native_lifetime_release(&c->lifetime, backing);
}

int sc_campaign_dispatch(unsigned command) {
    ScCampaignControl *c = sc_campaign_bound();
    ScNativeCampaignOwner calls;
    ScNativeLoop *loop;
    int status;
    if (!c) return SC_INVALID;
    loop = &c->lifetime.loop;
    switch (command) {
    case SC_CAMPAIGN_OPEN:
        calls.context = c;
        calls.acquire = acquire; calls.validate = validate;
        calls.submit = submit; calls.poll = poll; calls.resume = resume;
        calls.retire_display = retire_display; calls.release = release;
        status = sc_native_loop_open(loop, &c->lifetime.display, &calls);
        if (status != SC_OK) return status;
        return sc_native_operation_bind(&c->operation, &c->lifetime);
    case SC_CAMPAIGN_STEP:
        return sc_native_loop_step(loop);
    case SC_CAMPAIGN_CLOSE:
        return loop->acquired ? sc_native_loop_close(loop) : SC_OK;
    }
    return SC_INVALID;
}
#endif
