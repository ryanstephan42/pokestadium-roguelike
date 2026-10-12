#ifndef SC_MODULE_ARENA_H
#define SC_MODULE_ARENA_H

typedef unsigned int ScMaU32;
#define SC_MA_SLOTS 8
#define SC_MA_SPANS 8
#define SC_MA_READ 1
#define SC_MA_WRITE 2
#define SC_MA_EXEC 4

typedef struct { ScMaU32 slot, generation; } ScMaHandle;
typedef struct { ScMaU32 offset, size, rights; } ScMaSpan;
typedef struct {
    ScMaHandle handle;
    ScMaU32 ticket, kind;
} ScMaLease;
typedef struct {
    ScMaU32 base, size, generation, ticket, lease_kind, published, failed;
    ScMaU32 live, span_count;
    ScMaSpan spans[SC_MA_SPANS];
} ScMaBlock;
typedef struct {
    ScMaU32 end, active, next_ticket;
    unsigned char *backing;
    ScMaU32 backing_bytes;
    ScMaBlock blocks[SC_MA_SLOTS];
} ScMaArena;

/* Backing maps physical [0x400000,end); host storage is not native admission. */
int sc_ma_init(ScMaArena *, ScMaU32 memory, unsigned char *, ScMaU32 bytes);
int sc_ma_alloc(ScMaArena *, ScMaU32 bytes, ScMaHandle *);
int sc_ma_staging(ScMaArena *, ScMaHandle, ScMaLease *);
/* Mutable non-executable backing; ending this lease is not a GPU/VI fence. */
int sc_ma_data(ScMaArena *, ScMaHandle, ScMaLease *);
unsigned char *sc_ma_map(ScMaArena *, const ScMaLease *, ScMaU32 offset,
                        ScMaU32 size, ScMaU32 rights);
int sc_ma_end(ScMaArena *, ScMaLease *);
int sc_ma_publish(ScMaArena *, ScMaHandle, const ScMaSpan *, ScMaU32 count);
int sc_ma_seal(ScMaArena *, ScMaLease *, const ScMaSpan *, ScMaU32 count);
int sc_ma_module(ScMaArena *, ScMaHandle, ScMaLease *);
int sc_ma_fail(ScMaArena *, const ScMaLease *);
int sc_ma_release(ScMaArena *, ScMaHandle);
int sc_ma_shutdown(ScMaArena *);
ScMaU32 sc_ma_address(ScMaArena *, ScMaHandle);

#endif
