/* PURE */
#include "sc_ai_lease.h"

static int fail(ScAiLease *lease, int status) {
    if (lease && !lease->error) lease->error = (unsigned)status;
    return status;
}

static int owns(const ScAiLease *lease, unsigned owner, unsigned token) {
    return lease && owner && owner <= SC_AI_LEASE_AI &&
        lease->owner == owner && token && lease->generation == token;
}

int sc_ai_lease_acquire(ScAiLease *lease, unsigned owner,
                        unsigned map_address, unsigned map_size, unsigned *token) {
    if (!lease || !token || !owner || owner > SC_AI_LEASE_AI)
        return fail(lease, SC_AI_LEASE_UNKNOWN);
    if (lease->owner || lease->callable || lease->in_call)
        return fail(lease, SC_AI_LEASE_BUSY);
    if (lease->error || map_address || map_size)
        return fail(lease, SC_AI_LEASE_UNKNOWN);
    if (lease->generation == 65535)
        return fail(lease, SC_AI_LEASE_EXHAUSTED);
    lease->owner = owner;
    lease->generation++;
    lease->address = lease->size = lease->callable = 0;
    *token = lease->generation;
    return SC_AI_LEASE_OK;
}

int sc_ai_lease_bind(ScAiLease *lease, unsigned owner, unsigned token,
                     unsigned address, unsigned size) {
    if (!owns(lease, owner, token)) return fail(lease, SC_AI_LEASE_STALE);
    if (lease->error || lease->address || !address || !size || size >= 0x100000)
        return fail(lease, SC_AI_LEASE_UNKNOWN);
    lease->address = address;
    lease->size = size;
    return SC_AI_LEASE_OK;
}

int sc_ai_lease_publish(ScAiLease *lease, unsigned owner, unsigned token,
                        unsigned map_address, unsigned map_size) {
    if (!owns(lease, owner, token)) return fail(lease, SC_AI_LEASE_STALE);
    if (lease->error || !lease->address || map_address != lease->address ||
        map_size != lease->size) return fail(lease, SC_AI_LEASE_UNKNOWN);
    lease->callable = 1;
    return SC_AI_LEASE_OK;
}

int sc_ai_lease_revoke(ScAiLease *lease, unsigned owner, unsigned token) {
    if (!owns(lease, owner, token)) return fail(lease, SC_AI_LEASE_STALE);
    if (lease->in_call) return fail(lease, SC_AI_LEASE_BUSY);
    lease->callable = 0;
    return SC_AI_LEASE_OK;
}

int sc_ai_lease_enter(ScAiLease *lease, unsigned owner, unsigned token,
                      unsigned map_address, unsigned map_size) {
    if (!owns(lease, owner, token)) return fail(lease, SC_AI_LEASE_STALE);
    if (lease->in_call) return fail(lease, SC_AI_LEASE_BUSY);
    if (lease->error || !lease->callable || map_address != lease->address ||
        map_size != lease->size) return fail(lease, SC_AI_LEASE_UNKNOWN);
    lease->in_call = 1;
    return SC_AI_LEASE_OK;
}

int sc_ai_lease_leave(ScAiLease *lease, unsigned owner, unsigned token) {
    if (!owns(lease, owner, token)) return fail(lease, SC_AI_LEASE_STALE);
    if (!lease->in_call) return fail(lease, SC_AI_LEASE_UNKNOWN);
    lease->in_call = 0;
    return SC_AI_LEASE_OK;
}

int sc_ai_lease_free_admit(ScAiLease *lease, unsigned owner, unsigned token,
                           unsigned address, unsigned map_address, unsigned map_size) {
    if (!owns(lease, owner, token)) return fail(lease, SC_AI_LEASE_STALE);
    if (lease->callable || lease->in_call || !address || address != lease->address ||
        map_address != address || map_size != lease->size)
        return fail(lease, SC_AI_LEASE_UNKNOWN);
    return SC_AI_LEASE_OK;
}

int sc_ai_lease_release(ScAiLease *lease, unsigned owner, unsigned token,
                        unsigned map_address, unsigned map_size) {
    if (!owns(lease, owner, token)) return fail(lease, SC_AI_LEASE_STALE);
    if (lease->callable || lease->in_call || map_address || map_size)
        return fail(lease, SC_AI_LEASE_UNKNOWN);
    lease->owner = SC_AI_LEASE_NONE;
    lease->address = lease->size = 0;
    return SC_AI_LEASE_OK;
}
