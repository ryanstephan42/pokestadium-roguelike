#ifndef SC_AI_LEASE_H
#define SC_AI_LEASE_H

enum ScAiLeaseOwner { SC_AI_LEASE_NONE, SC_AI_LEASE_COLD, SC_AI_LEASE_AI };
enum ScAiLeaseStatus {
    SC_AI_LEASE_OK, SC_AI_LEASE_BUSY, SC_AI_LEASE_UNKNOWN,
    SC_AI_LEASE_STALE, SC_AI_LEASE_EXHAUSTED
};

typedef struct ScAiLease {
    unsigned int owner, generation, address, size, callable, error, in_call;
} ScAiLease;

int sc_ai_lease_acquire(ScAiLease *lease, unsigned owner,
                        unsigned map_address, unsigned map_size, unsigned *token);
int sc_ai_lease_bind(ScAiLease *lease, unsigned owner, unsigned token,
                     unsigned address, unsigned size);
int sc_ai_lease_publish(ScAiLease *lease, unsigned owner, unsigned token,
                        unsigned map_address, unsigned map_size);
int sc_ai_lease_enter(ScAiLease *lease, unsigned owner, unsigned token,
                      unsigned map_address, unsigned map_size);
int sc_ai_lease_leave(ScAiLease *lease, unsigned owner, unsigned token);
int sc_ai_lease_revoke(ScAiLease *lease, unsigned owner, unsigned token);
int sc_ai_lease_free_admit(ScAiLease *lease, unsigned owner, unsigned token,
                           unsigned address, unsigned map_address, unsigned map_size);
int sc_ai_lease_release(ScAiLease *lease, unsigned owner, unsigned token,
                        unsigned map_address, unsigned map_size);

#endif
