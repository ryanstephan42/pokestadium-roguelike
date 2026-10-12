#ifdef STORY_NATIVE_UI
#include "sc_native_loop.h"

static int fail(ScNativeLoop *loop, int status, const char *message) {
    HAL_Printf(8, 8, "%s", message);
    if (loop && loop->backing.ui) {
        loop->backing.ui->status = status; loop->backing.ui->message = message;
    }
    return status;
}
static int validate(ScNativeLoop *loop) {
    if (!loop || !loop->acquired) return SC_INVALID;
    return loop->owner.validate(loop->owner.context, &loop->backing);
}
static int queue(void *context, const ScUiBattleRequest *request) {
    ScNativeLoop *loop = context;
    if (request != loop->backing.request || loop->queued) return SC_INVALID;
    loop->queued = 1;
    return SC_OK;
}
static void draw(void *context, const ScUi *ui) {
    ScNativeLoop *loop = context;
    loop->frame_status = sc_native_ui_draw(loop->backing.native_ui, ui);
}
static void leave(void *context) { ((ScNativeLoop *)context)->exited = 1; }

static int finish_drained(ScNativeLoop *loop) {
    int status = sc_native_ui_finish(loop->backing.native_ui, loop->backing.driver,
                                    loop->drained_operation, loop->drained_outcome);
    /* An unavailable Pak/session has not dispatched driver.finish. Once the
       transaction starts, save retries belong to the UI, never the event. */
    if (status != SC_UNAVAILABLE || !loop->backing.ui->request_ready)
        loop->drained_operation = 0;
    return status;
}

int sc_native_loop_open(ScNativeLoop *loop, ScNativeDisplay *display,
                        const ScNativeCampaignOwner *owner) {
    ScUiDriverCalls bridge;
    ScNativeCampaignBacking *b;
    int status;
    if (!loop || loop->acquired || !display || display->acquired ||
        !owner || !owner->acquire || !owner->validate ||
        !owner->submit || !owner->poll || !owner->resume || !owner->retire_display || !owner->release)
        return fail(NULL, SC_INVALID, "Story loop: owner acquisition callbacks unavailable");
    loop->prepared = loop->exited = loop->queued = loop->held_presented = 0;
    loop->held_operation = loop->held_generation = loop->event_operation = loop->event_serial = 0;
    loop->drained_operation = loop->drained_outcome = 0;
    loop->display_acquired = loop->pak_bound = loop->resources_acquired = 0;
    loop->display_used = 0;
    loop->frame_status = SC_OK;
    loop->owner = *owner; loop->display = display;
    status = owner->acquire(owner->context, &loop->backing);
    if (status != SC_OK) return status;
    loop->acquired = 1;
    b = &loop->backing;
    status = validate(loop);
    if (status != SC_OK) return status;
    if (!b->lease_id || !b->ui || !b->request || !b->driver || !b->native_ui || !b->pak ||
        !b->event || !b->catalog || !b->arena)
        return fail(NULL, SC_INVALID, "Story loop: incomplete admitted backing tuple");
    if (b->driver->bound || b->native_ui->acquired || b->native_ui->prepared || b->pak->bound)
        return fail(NULL, SC_INVALID, "Story loop: backing already owns another lifetime");
    status = sc_native_display_acquire(display, b->framebuffer);
    if (status != SC_OK) return status;
    loop->display_acquired = loop->display_used = 1;
    status = sc_native_pak_bind(b->pak, 0);
    if (status != SC_SAVE_OK) return fail(loop, SC_UNAVAILABLE, "Story loop: Pak binding failed");
    loop->pak_bound = 1;
    status = sc_native_ui_acquire(b->native_ui, b->pak, b->arena);
    if (status != SC_OK) return status;
    loop->resources_acquired = 1;
    status = sc_native_ui_prepare(b->native_ui, b->ui, b->catalog, b->seed);
    if (status != SC_OK) return status;
    bridge.context = loop; bridge.launch = queue; bridge.draw = draw; bridge.leave = leave;
    status = sc_ui_driver_bind(b->driver, b->ui, b->request, &bridge);
    if (status == SC_OK) loop->prepared = 1;
    return status;
}

static int consume_event(ScNativeLoop *loop, ScNativeCampaignEvent *event);

/* The shared event is a clean output buffer: the owner retains the queued
   identity and writes it only during poll; the loop consumes and clears it
   after every poll so no delivered event can be replayed. */
static int poll_event(ScNativeLoop *loop) {
    ScNativeCampaignBacking *b = &loop->backing;
    ScNativeCampaignEvent *event = b->event;
    unsigned char *bytes;
    unsigned i;
    int status;
    if (loop->drained_operation) return finish_drained(loop);
    if (loop->held_operation || loop->queued) return SC_OK;
    if (b->driver->launched && loop->event_operation == b->driver->operation_id &&
        loop->event_serial == ~(RlU32)0)
        return fail(loop, SC_UNAVAILABLE, "Story loop: event serial exhausted; lease retained");
    status = loop->owner.poll(loop->owner.context, b, event);
    if (status == SC_OK) status = consume_event(loop, event);
    bytes = (unsigned char *)event;
    for (i = 0; i < sizeof(*event); ++i) bytes[i] = 0;
    return status;
}

static int consume_event(ScNativeLoop *loop, ScNativeCampaignEvent *event) {
    ScNativeCampaignBacking *b = &loop->backing;
    int status;
    if (event->kind == SC_NATIVE_EVENT_NONE) return SC_OK;
    if (event->kind > SC_NATIVE_EVENT_FAILED)
        return fail(loop, SC_INVALID, "Story loop: unsupported owner event");
    if (!b->driver->bound || !b->driver->launched || b->driver->ui != b->ui ||
        b->driver->request != b->request || b->native_ui->driver != b->driver ||
        b->native_ui->prepared != b->ui || b->ui->device != &b->pak->device ||
        !event->operation_id || event->operation_id != b->driver->operation_id ||
        event->operation_id != b->request->operation_id || event->operation_id != b->ui->operation_id)
        return fail(loop, SC_INVALID, "Story loop: stale owner event operation");
    if (!event->generation || (loop->event_operation == event->operation_id &&
        event->generation <= loop->event_serial))
        return fail(loop, SC_INVALID, "Story loop: stale owner event serial");
    if ((event->kind == SC_NATIVE_EVENT_OBSERVE || event->kind == SC_NATIVE_EVENT_LEARNING) &&
        (event->active_slot >= b->ui->state.party_count ||
         event->enemy_slot >= b->request->enemy_count || event->enemy_hp > 999))
        return fail(loop, SC_INVALID, "Story loop: invalid observation bounds");
    if (event->kind == SC_NATIVE_EVENT_DRAINED &&
        (event->outcome > SC_DRAW || b->ui->state.phase != SC_BATTLE || b->ui->state.learn_mask))
        return fail(loop, SC_INVALID, "Story loop: unsettled or invalid drained result");
    loop->event_operation = event->operation_id; loop->event_serial = event->generation;
    if (event->kind == SC_NATIVE_EVENT_OBSERVE) {
        status = sc_ui_driver_observe(b->driver, event->operation_id, event->party,
            event->active_slot, event->enemy_slot, event->enemy_hp);
        if (status == SC_OK && b->ui->state.phase == SC_LEARNING)
            return fail(loop, SC_INVALID, "Story loop: KO learning requires held event");
        return status;
    }
    if (event->kind == SC_NATIVE_EVENT_LEARNING) {
        status = sc_ui_driver_observe(b->driver, event->operation_id, event->party,
            event->active_slot, event->enemy_slot, event->enemy_hp);
        if (status != SC_OK) return status;
        if (b->ui->state.phase != SC_LEARNING)
            return fail(loop, SC_INVALID, "Story loop: learning hold not established");
        loop->held_operation = event->operation_id;
        loop->held_generation = event->generation;
        loop->held_presented = 0;
        return SC_OK;
    }
    if (event->kind == SC_NATIVE_EVENT_DRAINED) {
        if (b->ui->state.phase != SC_BATTLE)
            return fail(loop, SC_PENDING, "Story loop: result before learning settled");
        loop->drained_operation = event->operation_id;
        loop->drained_outcome = event->outcome;
        return finish_drained(loop);
    }
    if (event->kind == SC_NATIVE_EVENT_FAILED)
        return fail(loop, event->error < SC_INVALID || event->error > SC_UNREVIEWED ?
            SC_INVALID : event->error, "Story loop: native service failed");
    return fail(loop, SC_INVALID, "Story loop: unsupported owner event");
}

int sc_native_loop_step(ScNativeLoop *loop) {
    ScNativeCampaignBacking *b;
    int status, ended;
    if (!loop || !loop->prepared || loop->exited) return SC_INVALID;
    status = validate(loop);
    if (status != SC_OK) return status;
    b = &loop->backing;
    status = poll_event(loop);
    if (status != SC_OK && status != SC_PENDING) return status;
    if (b->driver->launched && !loop->held_operation && !loop->queued &&
        b->ui->state.phase == SC_BATTLE)
        return SC_PENDING;
    status = sc_native_display_begin(loop->display);
    if (status != SC_OK) return status;
    loop->frame_status = SC_OK;
    if ((b->ui->state.phase == SC_LEARNING && (!loop->held_operation || !loop->held_presented)) ||
        (loop->held_operation && loop->held_presented && b->ui->state.phase == SC_BATTLE))
        status = sc_native_ui_draw(b->native_ui, b->ui);
    else status = sc_native_ui_input(b->native_ui, b->driver);
    ended = sc_native_display_end(loop->display);
    if (ended != SC_OK || loop->frame_status != SC_OK || status != SC_OK) {
        if (loop->queued) {
            loop->queued = 0; b->driver->launched = 0; b->driver->launch_pending = 1;
        }
        return ended != SC_OK ? ended : loop->frame_status != SC_OK ? loop->frame_status : status;
    }
    if (loop->held_operation) loop->held_presented = 1;
    if (loop->queued) {
        status = validate(loop);
        if (status != SC_OK) return status;
        status = loop->owner.submit(loop->owner.context, b, b->request);
        loop->queued = 0;
        if (status != SC_OK) {
            b->driver->launched = 0; b->driver->launch_pending = 1;
            return fail(loop, status, "Story loop: submission refused; A retries checkpointed values");
        }
    }
    if (loop->held_operation && loop->held_presented && b->ui->state.phase == SC_BATTLE &&
        !b->ui->state.learn_mask) {
        status = validate(loop);
        if (status == SC_OK) status = loop->owner.resume(loop->owner.context, b,
            loop->held_operation, loop->held_generation, &b->ui->state, &b->request->progression);
        if (status != SC_OK) return status;
        loop->held_operation = loop->held_generation = 0;
    }
    return SC_OK;
}

int sc_native_loop_close(ScNativeLoop *loop) {
    ScNativeCampaignBacking *b;
    int status;
    if (!loop || !loop->acquired) return SC_INVALID;
    status = validate(loop); if (status != SC_OK) return status;
    b = &loop->backing;
    if (loop->prepared && (!loop->exited || loop->held_operation || loop->queued ||
        loop->drained_operation)) return SC_PENDING;
    if (loop->pak_bound && b->pak->session) return SC_PENDING;
    if (loop->display_acquired) {
        status = sc_native_display_retire(loop->display, &loop->owner, b);
        if (status != SC_OK) return status;
    }
    if (loop->resources_acquired) {
        func_8000771C();
        status = sc_native_ui_release(b->native_ui, loop->prepared ? b->driver : NULL, 1);
        if (status != SC_OK) return status;
        loop->resources_acquired = 0;
    }
    if (loop->pak_bound) {
        status = sc_native_pak_release(b->pak); if (status != SC_SAVE_OK) return SC_INVALID;
        loop->pak_bound = 0;
    }
    if (loop->display_acquired) {
        status = sc_native_display_release(loop->display); if (status != SC_OK) return status;
        loop->display_acquired = 0;
    }
    status = loop->owner.release(loop->owner.context, b);
    if (status == SC_OK) loop->acquired = loop->prepared = 0;
    return status;
}
#endif
