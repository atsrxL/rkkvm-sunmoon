// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef RKMOON_SPSC_H
#define RKMOON_SPSC_H
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#define SPSC_SIZE 2048u
_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "32-bit atomics must be lock-free");
// Exactly one producer and one consumer. Never reset a live ring.
typedef struct { atomic_uint head,tail; uint8_t bytes[SPSC_SIZE]; } spsc_t;
static inline unsigned spsc_count(spsc_t *r) {return atomic_load_explicit(&r->head,memory_order_acquire)-atomic_load_explicit(&r->tail,memory_order_acquire);}
static inline unsigned spsc_free(spsc_t *r) {return SPSC_SIZE-spsc_count(r);}
static inline bool spsc_write(spsc_t *r,const uint8_t *p,unsigned n) {
 unsigned h=atomic_load_explicit(&r->head,memory_order_relaxed);
 if(n>SPSC_SIZE || n>SPSC_SIZE-(h-atomic_load_explicit(&r->tail,memory_order_acquire)))return false;
 for(unsigned i=0;i<n;i++)r->bytes[(h+i)&(SPSC_SIZE-1)]=p[i];
 atomic_store_explicit(&r->head,h+n,memory_order_release);return true;
}
static inline unsigned spsc_read(spsc_t *r,uint8_t *p,unsigned n) {
 unsigned t=atomic_load_explicit(&r->tail,memory_order_relaxed);
 unsigned available=atomic_load_explicit(&r->head,memory_order_acquire)-t;
 if(n>available)n=available;
 for(unsigned i=0;i<n;i++)p[i]=r->bytes[(t+i)&(SPSC_SIZE-1)];
 atomic_store_explicit(&r->tail,t+n,memory_order_release);return n;
}
#endif
