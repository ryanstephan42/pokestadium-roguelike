#ifndef SC_BATL_JOBS_H
#define SC_BATL_JOBS_H

/* PURE: battle loader job completion and effect-record publication admission. No libultra. */

#define SC_JOB_MODEL_BYTES 0x3C000u
#define SC_JOB_FX_BYTES 0x18000u
#define SC_JOB_FX_MAX 15
#define SC_JOB_MODEL_HEADER_BYTES 0x18u
#define SC_JOB_ROOT_BYTES 0x2Cu

/* Result of a model (type 1) completion. */
enum { SC_JOB_FAILED = 0, SC_JOB_LOADED = 1, SC_JOB_CACHED = 2 };

typedef struct {
    unsigned int result;     /* job +08: constructed header, 0 when nothing new was built */
    unsigned int token;      /* job +0C: live header sent by an exact cache hit, else 0 */
    unsigned int size;       /* job +14: 0 marks a parser/loader refusal */
    unsigned int buffer;     /* job +10: model buffer the request was bound to */
    unsigned int expected;   /* worker buffer selected for this request */
    unsigned int live;       /* worker +24: currently published header */
    unsigned int live_model; /* model id held by the live header (+00), valid when live != 0 */
    unsigned int request;    /* requested model id (job +02 plus one) */
    unsigned int live_tint;  /* worker +2C */
    unsigned int job_tint;   /* job +18 */
} ScJobModelView;

/* [at, at+bytes) lies inside [base, base+total), 4-aligned, no wrap */
int sc_job_in(unsigned int base, unsigned int total, unsigned int at, unsigned int bytes);
/* SC_JOB_CACHED, or SC_JOB_LOADED when the new header may be inspected further, else SC_JOB_FAILED */
int sc_job_model_result(const ScJobModelView* v);
/* loaded header fields, read only after sc_job_model_result returned SC_JOB_LOADED */
int sc_job_model_header_ok(unsigned int buffer, unsigned int entry, unsigned int nroots, unsigned int roots);
/* one constructed root: type is root +00 (node header), owner is +28 for type-E model roots */
int sc_job_model_root_ok(unsigned int buffer, unsigned int root, unsigned int type, unsigned int owner,
                         unsigned int entry);
#define SC_JOB_ROOT_NODE_BYTES 0x18u

/* Provenance of one fresh model load, recorded on the loader thread by func_80019484: the job it
 * served, the 920000 archive index it loaded, the FRAGMENT that index returned and the header
 * func_80019328 built from it. A worker has at most one model job in flight (unk_01 bit 0). */
typedef struct {
    unsigned int state; /* 0 empty, 1 recorded, 2 a second load arrived before admission */
    unsigned int job, index, fragment, header;
} ScJobTicket;
void sc_job_ticket_put(ScJobTicket* t, unsigned int job, unsigned int index, unsigned int fragment,
                       unsigned int header);
/* consumes the ticket; 1 only when it records exactly this job/index/fragment/header */
int sc_job_ticket_take(ScJobTicket* t, unsigned int job, unsigned int index, unsigned int fragment,
                       unsigned int header);

/* Effect (type 2) completion: every requested 8cc000 entry produced a record list inside the
 * worker's own effect buffer, followed by the NULL terminator. */
int sc_job_fx_complete(unsigned int size, unsigned int buffer, unsigned int expected, const short* requests,
                       const unsigned int* results);
/* one result list as 8432C830 walks it; reads only bytes already proven inside the buffer */
int sc_job_fx_list_ok(unsigned int buffer, unsigned int list);

/* Effect-record slot provenance. Base records come from the battle's entry-0 load; request
 * records live in one worker's effect buffer and go stale when that worker sends a new request. */
#define SC_FX_SLOTS 256
#define SC_FX_BASE 2
#define SC_FX_NONE 3
enum { SC_FX_EMPTY = 0, SC_FX_GRAPH = 1, SC_FX_DATA = 2 };

typedef struct {
    unsigned char kind[SC_FX_SLOTS];
    unsigned char owner[SC_FX_SLOTS];
    unsigned int generation[SC_FX_SLOTS];
    unsigned int sent[2];
    unsigned int completed[2];
    int last;     /* worker whose complete request may be published next, -1 none */
    int publish;  /* owner while publishing, SC_FX_NONE outside publication */
} ScFxTable;

void sc_fx_reset(ScFxTable* t);
void sc_fx_base_begin(ScFxTable* t);
/* worker w may send a new request only when no request (its own or the other side's) is
 * outstanding, because both share one request list and one result array */
int sc_fx_send(ScFxTable* t, int w, int other_pending);
int sc_fx_complete(ScFxTable* t, int w);
int sc_fx_publish_begin(ScFxTable* t, int any_pending);
int sc_fx_note(ScFxTable* t, int id, int record_kind);
void sc_fx_publish_end(ScFxTable* t);
/* graph != 0: attachment root (record kind 3); graph == 0: animation/data record */
int sc_fx_admit(const ScFxTable* t, int id, int graph);

#endif
