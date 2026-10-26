#include "mm.h"
void mm_random(mm_memory *m, const mm_config *c) {
    uint64_t a=0,b=0,d=0,e=0, value=++m->calls;
    size_t n=m->index_count;
    if (c->test==RANDOM_WRITE) {
        for (size_t i=0;i<n;++i) m->data[m->indices[i]]=value;
    } else {
        size_t i=0;
        for (;n-i>=4;i+=4) {
            a+=m->data[m->indices[i]]; b+=m->data[m->indices[i+1]];
            d+=m->data[m->indices[i+2]]; e+=m->data[m->indices[i+3]];
        }
        for (;i<n;++i) a+=m->data[m->indices[i]];
    }
    m->checksum+=a+b+d+e+value;
}
