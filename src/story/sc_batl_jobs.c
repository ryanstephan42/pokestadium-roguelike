/* PURE */
#include "sc_batl_jobs.h"

int sc_job_in(unsigned int base, unsigned int total, unsigned int at, unsigned int bytes) {
    return base != 0 && (at & 3u) == 0 && at >= base && bytes <= total && at - base <= total - bytes;
}

int sc_job_model_result(const ScJobModelView* v) {
    if (v->size == 0) {
        return SC_JOB_FAILED;
    }
    if (v->result == 0) {
        /* 198E4 sends a job straight back only for the live header with identical model and tint */
        if (v->token != 0 && v->token == v->live && v->live_model == v->request && v->live_tint == v->job_tint) {
            return SC_JOB_CACHED;
        }
        return SC_JOB_FAILED;
    }
    if (v->token != 0 || v->buffer == 0 || v->buffer != v->expected ||
        !sc_job_in(v->buffer, SC_JOB_MODEL_BYTES, v->result, SC_JOB_MODEL_HEADER_BYTES)) {
        return SC_JOB_FAILED;
    }
    return SC_JOB_LOADED;
}

int sc_job_model_header_ok(unsigned int buffer, unsigned int entry, unsigned int nroots, unsigned int roots) {
    return nroots >= 1 && sc_job_in(buffer, SC_JOB_MODEL_BYTES, entry, 4) &&
           sc_job_in(buffer, SC_JOB_MODEL_BYTES, roots, nroots * 4u);
}

int sc_job_model_root_ok(unsigned int buffer, unsigned int root, unsigned int type, unsigned int owner,
                         unsigned int entry) {
    if (!sc_job_in(buffer, SC_JOB_MODEL_BYTES, root, SC_JOB_ROOT_NODE_BYTES)) {
        return 0;
    }
    return type != 0xE || (sc_job_in(buffer, SC_JOB_MODEL_BYTES, root, SC_JOB_ROOT_BYTES) && owner == entry);
}

int sc_job_fx_complete(unsigned int size, unsigned int buffer, unsigned int expected, const short* requests,
                       const unsigned int* results) {
    int n = 0;
    int i;

    if (size == 0 || buffer == 0 || buffer != expected || requests == 0 || results == 0) {
        return 0;
    }
    while (requests[n] != -1) {
        if (requests[n] < 0 || ++n > SC_JOB_FX_MAX) {
            return 0;
        }
    }
    for (i = 0; i < n; i++) {
        if (!sc_job_fx_list_ok(buffer, results[i])) {
            return 0;
        }
    }
    return results[n] == 0;
}

/* Mirrors the 8432C830 walk: every 8-byte record it reads (tag s8 +0, slot s16 +2, payload +4)
 * lies in the buffer, every published slot indexes D_843920C0[256], and the walk terminates. */
int sc_job_fx_list_ok(unsigned int buffer, unsigned int list) {
    unsigned int at = list;
    short id;

    for (;;) {
        if (!sc_job_in(buffer, SC_JOB_FX_BYTES, at, 8)) {
            return 0;
        }
        id = *(const short*)(at + 2);
        if (id == 0) {
            /* C830 re-reads this record forever unless its tag ends the list */
            return *(const signed char*)at == 0;
        }
        if (id < 0 || id >= SC_FX_SLOTS) {
            return 0;
        }
        at += 8;
        if (!sc_job_in(buffer, SC_JOB_FX_BYTES, at, 8)) {
            return 0;
        }
        if (*(const signed char*)at == 0) {
            return 1;
        }
    }
}

void sc_fx_reset(ScFxTable* t) {
    int i;

    for (i = 0; i < SC_FX_SLOTS; i++) {
        t->kind[i] = SC_FX_EMPTY;
        t->owner[i] = SC_FX_NONE;
        t->generation[i] = 0;
    }
    t->sent[0] = t->sent[1] = 0;
    t->completed[0] = t->completed[1] = 0;
    t->last = -1;
    t->publish = SC_FX_NONE;
}

void sc_fx_base_begin(ScFxTable* t) {
    int i;

    for (i = 0; i < SC_FX_SLOTS; i++) {
        t->kind[i] = SC_FX_EMPTY;
        t->owner[i] = SC_FX_NONE;
        t->generation[i] = 0;
    }
    t->publish = SC_FX_BASE;
}

int sc_fx_send(ScFxTable* t, int w, int other_pending) {
    if (w < 0 || w > 1 || other_pending || t->sent[w] != t->completed[w] || t->sent[w] == 0xFFFFFFFFu ||
        t->publish != SC_FX_NONE) {
        return 0;
    }
    t->sent[w]++;
    t->last = -1;
    return 1;
}

int sc_fx_complete(ScFxTable* t, int w) {
    if (w < 0 || w > 1 || t->sent[w] == t->completed[w]) {
        return 0;
    }
    t->completed[w] = t->sent[w];
    t->last = w;
    return 1;
}

int sc_fx_publish_begin(ScFxTable* t, int any_pending) {
    if (any_pending || t->last < 0 || t->publish != SC_FX_NONE || t->sent[t->last] != t->completed[t->last]) {
        return 0;
    }
    t->publish = t->last;
    return 1;
}

int sc_fx_note(ScFxTable* t, int id, int record_kind) {
    if (t->publish == SC_FX_NONE || id <= 0 || id >= SC_FX_SLOTS) {
        return 0;
    }
    t->kind[id] = record_kind == 3 ? SC_FX_GRAPH : SC_FX_DATA;
    t->owner[id] = (unsigned char)t->publish;
    t->generation[id] = t->publish == SC_FX_BASE ? 0 : t->sent[t->publish];
    return 1;
}

void sc_fx_publish_end(ScFxTable* t) {
    t->publish = SC_FX_NONE;
}

int sc_fx_admit(const ScFxTable* t, int id, int graph) {
    int owner;

    if (id < 0 || id >= SC_FX_SLOTS) {
        return 0;
    }
    if (t->kind[id] == SC_FX_EMPTY) {
        /* never published this battle: the slot still holds the C86C NULL */
        return 1;
    }
    if (t->kind[id] != (graph ? SC_FX_GRAPH : SC_FX_DATA)) {
        return 0;
    }
    owner = t->owner[id];
    if (owner == SC_FX_BASE) {
        return 1;
    }
    return owner <= 1 && t->generation[id] == t->sent[owner] && t->completed[owner] == t->sent[owner];
}
