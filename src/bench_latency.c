#include "mm.h"
void mm_latency(mm_memory *m, const mm_config *c) {
    size_t p=m->cursor;
    (void)c;
    for (size_t i=0;i<m->index_count;++i) p=(size_t)m->data[p];
    m->cursor=p;
    m->checksum+=(uint64_t)p + ++m->calls;
}
