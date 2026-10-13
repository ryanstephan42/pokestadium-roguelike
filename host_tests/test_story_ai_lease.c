#include <assert.h>
#include <stdio.h>
#include "../src/story/sc_ai_lease.h"

int main(void) {
    ScAiLease lease = {0};
    unsigned first, second, third, ignored = 0;
    assert(sc_ai_lease_acquire(&lease, SC_AI_LEASE_COLD, 0, 0, &first) == 0);
    assert(sc_ai_lease_bind(&lease, SC_AI_LEASE_COLD, first, 0x80200000, 176752) == 0);
    assert(sc_ai_lease_publish(&lease, SC_AI_LEASE_COLD, first, 0x80200000, 176752) == 0);
    assert(sc_ai_lease_enter(&lease, SC_AI_LEASE_COLD, first, 0x80200000, 176752) == 0);
    assert(lease.in_call == 1);
    assert(sc_ai_lease_leave(&lease, SC_AI_LEASE_COLD, first) == 0);
    assert(sc_ai_lease_revoke(&lease, SC_AI_LEASE_COLD, first) == 0);
    assert(sc_ai_lease_free_admit(&lease, SC_AI_LEASE_COLD, first,
                                 0x80200000, 0x80200000, 176752) == 0);
    assert(sc_ai_lease_release(&lease, SC_AI_LEASE_COLD, first, 0, 0) == 0);
    assert(sc_ai_lease_acquire(&lease, SC_AI_LEASE_AI, 0, 0, &second) == 0);
    assert(second != first);
    assert(sc_ai_lease_bind(&lease, SC_AI_LEASE_AI, second, 0x80200000, 60000) == 0);
    assert(sc_ai_lease_publish(&lease, SC_AI_LEASE_AI, second, 0x80200000, 60000) == 0);
    assert(sc_ai_lease_enter(&lease, SC_AI_LEASE_AI, second, 0x80200000, 60000) == 0);
    assert(sc_ai_lease_leave(&lease, SC_AI_LEASE_AI, second) == 0);
    assert(sc_ai_lease_revoke(&lease, SC_AI_LEASE_AI, second) == 0);
    assert(sc_ai_lease_free_admit(&lease, SC_AI_LEASE_AI, second,
                                 0x80200000, 0x80200000, 60000) == 0);
    assert(sc_ai_lease_release(&lease, SC_AI_LEASE_AI, second, 0, 0) == 0);
    assert(sc_ai_lease_acquire(&lease, SC_AI_LEASE_COLD, 0, 0, &third) == 0);
    assert(third != second && third != first);
    assert(sc_ai_lease_bind(&lease, SC_AI_LEASE_COLD, third, 0x80200000, 176752) == 0);
    assert(sc_ai_lease_free_admit(&lease, SC_AI_LEASE_AI, second,
                                 0x80200000, 0x80200000, 176752) == SC_AI_LEASE_STALE);
    assert(lease.owner == SC_AI_LEASE_COLD && lease.generation == third);
    assert(sc_ai_lease_acquire(&lease, SC_AI_LEASE_AI, 0, 0, &ignored) == SC_AI_LEASE_BUSY);
    assert(ignored == 0);
    assert(sc_ai_lease_revoke(&lease, SC_AI_LEASE_COLD, third) == 0);
    assert(sc_ai_lease_release(&lease, SC_AI_LEASE_COLD, third, 0, 0) == 0);
    assert(lease.error == SC_AI_LEASE_STALE);
    lease.error = 0;
    assert(sc_ai_lease_acquire(&lease, SC_AI_LEASE_AI, 0x80200000, 1, &ignored) == SC_AI_LEASE_UNKNOWN);
    assert(lease.owner == 0);
    lease.error = 0; lease.generation = 65535;
    assert(sc_ai_lease_acquire(&lease, SC_AI_LEASE_AI, 0, 0, &ignored) == SC_AI_LEASE_EXHAUSTED);
    {
        ScAiLease nested = {0};
        assert(sc_ai_lease_acquire(&nested, SC_AI_LEASE_AI, 0, 0, &first) == 0);
        assert(sc_ai_lease_bind(&nested, SC_AI_LEASE_AI, first, 0x80300000, 60000) == 0);
        assert(sc_ai_lease_publish(&nested, SC_AI_LEASE_AI, first, 0x80300000, 60000) == 0);
        assert(sc_ai_lease_enter(&nested, SC_AI_LEASE_AI, first, 0x80300000, 60000) == 0);
        assert(sc_ai_lease_enter(&nested, SC_AI_LEASE_AI, first, 0x80300000, 60000) == SC_AI_LEASE_BUSY);
        assert(sc_ai_lease_revoke(&nested, SC_AI_LEASE_AI, first) == SC_AI_LEASE_BUSY);
        assert(nested.in_call == 1 && nested.callable == 1);
        assert(sc_ai_lease_leave(&nested, SC_AI_LEASE_COLD, first) == SC_AI_LEASE_STALE);
        assert(nested.in_call == 1);
        assert(sc_ai_lease_leave(&nested, SC_AI_LEASE_AI, first) == 0);
        assert(sc_ai_lease_revoke(&nested, SC_AI_LEASE_AI, first) == 0);
        assert(sc_ai_lease_free_admit(&nested, SC_AI_LEASE_AI, first,
                                     0x80300000, 0x80300000, 60000) == 0);
        assert(sc_ai_lease_release(&nested, SC_AI_LEASE_AI, first, 0, 0) == 0);
        assert(nested.error == SC_AI_LEASE_BUSY);
    }
    puts("lease: COLD/AI/COLD, reused-base stale callback, nesting, unknown owner and generation exhaustion passed");
    return 0;
}
