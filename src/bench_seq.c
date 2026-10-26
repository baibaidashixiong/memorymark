#include "mm.h"
void mm_seq(mm_memory *m, const mm_config *c) {
    uint64_t a=0,b=0,d=0,e=0, value=++m->calls;
    size_t n=m->count, s=m->stride_words;
    if (c->test==SEQ_READ) {
        size_t i=0;
        /* Four independent accumulators avoid a single reduction dependency. */
        for (; n-i>=4;i+=4) {
            a+=m->data[i*s]; b+=m->data[(i+1)*s];
            d+=m->data[(i+2)*s]; e+=m->data[(i+3)*s];
        }
        for (;i<n;++i) a+=m->data[i*s];
    } else if (c->test==SEQ_WRITE) {
        for (size_t i=0;i<n;++i) m->data[i*s]=value;
    } else {
        /* Repeat exactly 100 logical operations, with a fixed read prefix. */
        size_t i=0;
        while (i<n) {
            size_t block=n-i<100 ? n-i : 100;
            size_t reads=block*c->read_ratio/100;
            for (size_t j=0;j<reads;++j) a+=m->data[(i+j)*s];
            for (size_t j=reads;j<block;++j) m->data[(i+j)*s]=value;
            i+=block;
        }
    }
    m->checksum+=a+b+d+e+value;
}
void mm_kernel(mm_memory *m, const mm_config *c) {
    if (c->test==LATENCY) mm_latency(m,c);
    else if (c->test==RANDOM_READ || c->test==RANDOM_WRITE) mm_random(m,c);
    else mm_seq(m,c);
}
