#include "mm.h"
#include <stdlib.h>
#include <string.h>
uint64_t mm_rng(uint64_t *s) {
    uint64_t z=(*s += UINT64_C(0x9e3779b97f4a7c15));
    z=(z^(z>>30))*UINT64_C(0xbf58476d1ce4e5b9);
    z=(z^(z>>27))*UINT64_C(0x94d049bb133111eb);
    return z^(z>>31);
}
uint64_t mm_bounded(uint64_t *s, uint64_t bound) {
    uint64_t x, threshold=(uint64_t)(0-bound)%bound;
    do { x=mm_rng(s); } while (x<threshold);
    return x%bound;
}
int mm_memory_init(mm_memory *m, const mm_config *c, unsigned worker) {
    uint64_t rng=c->seed + (uint64_t)worker*UINT64_C(0x517cc1b727220a95);
    memset(m,0,sizeof(*m));
    m->stride_words=c->stride/8;
    m->slots=c->size/8;
    m->count=1+(m->slots-1)/m->stride_words;
    m->index_count=c->accesses ? c->accesses : m->count;
    m->data=mm_alloc(c->size);
    if (!m->data) return -1;
    for (size_t i=0;i<m->slots;++i) m->data[i]=(uint64_t)i^c->seed;
    if (c->test==COPY || c->test==TRIAD) {
        m->data2=mm_alloc(c->size);
        if (!m->data2) goto fail;
        for (size_t i=0;i<m->slots;++i) m->data2[i]=((uint64_t)i+UINT64_C(0x9e3779b97f4a7c15))^c->seed;
    }
    if (c->test==TRIAD) {
        m->data3=mm_alloc(c->size);
        if (!m->data3) goto fail;
        memset(m->data3,0,c->size);
    }
    if (c->test==RANDOM_READ || c->test==RANDOM_WRITE || c->test==LATENCY) {
        size_t n=c->test==LATENCY ? m->count : m->index_count;
        if (n>SIZE_MAX/sizeof(size_t)) goto fail;
        m->indices=mm_alloc(n*sizeof(size_t));
        if (!m->indices) goto fail;
        if (c->test==LATENCY) {
            for (size_t i=0;i<n;++i) m->indices[i]=i*m->stride_words;
            for (size_t i=n;i>1;--i) {
                size_t j=(size_t)mm_bounded(&rng,i), x=m->indices[i-1];
                m->indices[i-1]=m->indices[j]; m->indices[j]=x;
            }
            for (size_t i=0;i<n;++i) m->data[m->indices[i]]=(uint64_t)m->indices[(i+1)%n];
        } else {
            for (size_t i=0;i<n;++i) m->indices[i]=(size_t)mm_bounded(&rng,m->count)*m->stride_words;
        }
        m->sequence_hash=UINT64_C(1469598103934665603);
        for (size_t i=0;i<n;++i) m->sequence_hash=(m->sequence_hash^(uint64_t)m->indices[i])*UINT64_C(1099511628211);
        if (c->test==LATENCY && !mm_ring_valid(m)) goto fail;
        /* The permutation scratch is not needed by the timed dependent loads. */
        if (c->test==LATENCY) { mm_free(m->indices); m->indices=NULL; }
    }
    return 0;
fail:
    mm_memory_destroy(m); return -1;
}
int mm_ring_valid(const mm_memory *m) {
    size_t p=0;
    for (size_t i=0;i<m->count;++i) {
        if (p>=m->slots || p%m->stride_words || (i && p==0)) return 0;
        p=(size_t)m->data[p];
    }
    return p==0;
}
void mm_memory_destroy(mm_memory *m) { mm_free(m->data); mm_free(m->data2); mm_free(m->data3); mm_free(m->indices); memset(m,0,sizeof(*m)); }
