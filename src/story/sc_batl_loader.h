#ifndef SC_BATL_LOADER_H
#define SC_BATL_LOADER_H

/* Campaign BATL loader DATA spans: the two animation pools and the staging
   buffer move into the admitted campaign DATA lease (CPU/worker DMA write,
   CPU and RSP/RDP read as texture/display-list data, never executed;
   docs/story/BATL-LOADER-RIGHTS.md). Release waits for graphics idle. Both model pools execute a
   relocated entry stub and stay vanilla lower allocations. The vanilla
   M,A,M,A,S request order of func_84301430 is enforced while armed. */
#define SC_BATL_LOADER_MODEL 0x3C000U
#define SC_BATL_LOADER_ANIMATION 0x18000U
#define SC_BATL_LOADER_STAGING 0x28000U
#define SC_BATL_LOADER_BYTES (2U * SC_BATL_LOADER_ANIMATION + SC_BATL_LOADER_STAGING)
int sc_batl_loader_bind(void *base, u32 bytes);
int sc_batl_loader_unbind(void);
int sc_batl_loader_arm(void);
int sc_batl_loader_disarm(const void *worker0, const void *worker1);
int sc_batl_loader_live(void);
void *sc_batl_loader_alloc(u32 size);

#endif
