#include "mm.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
    mm_mutex mutex;
    mm_cond cond;
    unsigned ready, timed, done, generation, released;
    int stop, failed;
    double start;
} gate;
typedef struct {
    const mm_config *config;
    gate *g;
    unsigned id;
    mm_memory memory;
    mm_sample samples[MM_MAX_REPEAT];
    double ends[MM_MAX_REPEAT];
} worker;
static int verify(const mm_memory *m, const mm_config *c) {
    if (c->test==LATENCY) return mm_ring_valid(m);
    if (c->test==COPY) {
        for (size_t i=0;i<m->count;++i) {
            size_t p=i*m->stride_words;
            if (m->data[p]!=((uint64_t)p^c->seed)) return 0;
            if (m->data2[p]!=m->data[p]+m->calls) return 0;
        }
    } else if (c->test==TRIAD) {
        for (size_t i=0;i<m->count;++i) {
            size_t p=i*m->stride_words;
            if (m->data[p]!=((uint64_t)p^c->seed) || m->data2[p]!=(((uint64_t)p+UINT64_C(0x9e3779b97f4a7c15))^c->seed)) return 0;
            if (m->data3[p]!=m->data[p]+UINT64_C(3)*m->data2[p]) return 0;
        }
    } else if (c->test==SEQ_WRITE) {
        for (size_t i=0;i<m->count;++i) if (m->data[i*m->stride_words]!=m->calls) return 0;
    } else if (c->test==RANDOM_WRITE) {
        for (size_t i=0;i<m->index_count;++i) if (m->data[m->indices[i]]!=m->calls) return 0;
    } else if (c->test==MIX) {
        for (size_t i=0;i<m->count;) {
            size_t n=m->count-i<100 ? m->count-i : 100;
            for (size_t j=n*c->read_ratio/100;j<n;++j)
                if (m->data[(i+j)*m->stride_words]!=m->calls) return 0;
            i+=n;
        }
    } else {
        for (size_t i=0;i<m->slots;++i) if (m->data[i]!=((uint64_t)i^c->seed)) return 0;
    }
    return 1;
}
static unsigned bytes_per_access(const mm_config *c) {
    if (c->test==COPY) return 16;
    if (c->test==TRIAD) return 24;
    return 8;
}
static void *work(void *arg) {
    worker *w=arg;
    const mm_config *c=w->config;
    gate *g=w->g;
    int failed=mm_bind(c->ncpus ? c->cpus[w->id] : -1);
    if (!failed) failed=mm_memory_init(&w->memory,c,w->id);
    for (unsigned round=0;round<c->repeat;++round) {
        if (!failed) {
            double warm=mm_now();
            do { mm_kernel(&w->memory,c); } while (mm_now()-warm<c->warmup);
            mm_store_fence();
        }
        mm_lock(&g->mutex);
        if (failed) g->failed=1;
        ++g->ready;
        mm_broadcast(&g->cond);
        while (g->generation<=round && !g->stop) mm_wait(&g->cond,&g->mutex);
        int stop=g->stop;
        mm_unlock(&g->mutex);
        if (stop) break;
        mm_sample *s=&w->samples[round];
        size_t per_pass=(c->test==LATENCY || c->test==RANDOM_READ || c->test==RANDOM_WRITE) ? w->memory.index_count : w->memory.count;
        uint64_t max_passes=UINT64_MAX/8/(uint64_t)per_pass;
        double start=mm_now(), end;
        do {
            if (s->iterations==max_passes) { failed=1; break; }
            mm_kernel(&w->memory,c);
            ++s->iterations;
            end=c->iterations ? start : mm_now();
        } while (c->iterations ? s->iterations<c->iterations : end-start<c->duration);
        /* Include store ordering in the measured interval, never a DRAM flush. */
        mm_store_fence();
        w->ends[round]=mm_now();
        s->seconds=w->ends[round]-start;
        s->accesses=s->iterations*(uint64_t)per_pass;
        s->bytes=s->accesses*bytes_per_access(c);
        s->checksum=w->memory.checksum;
        mm_lock(&g->mutex);
        ++g->timed;
        mm_broadcast(&g->cond);
        while (g->timed<(round+1)*c->threads) mm_wait(&g->cond,&g->mutex);
        mm_unlock(&g->mutex);
        if (!(s->seconds>0) || !verify(&w->memory,c)) failed=1;
        s->gbps=(double)s->bytes/s->seconds/1e9;
        s->ns_access=s->seconds*1e9/(double)s->accesses;
        mm_lock(&g->mutex);
        if (failed) g->failed=1;
        ++g->done;
        mm_broadcast(&g->cond);
        /* No next-round warmup may overlap another worker's timed interval. */
        while (g->released<=round && !g->stop) mm_wait(&g->cond,&g->mutex);
        stop=g->stop;
        mm_unlock(&g->mutex);
        if (stop) break;
    }
    return NULL;
}
static int compare_double(const void *a,const void *b) {
    double x=*(const double *)a,y=*(const double *)b;
    return (x>y)-(x<y);
}
mm_stats mm_statistics(const double *v, unsigned n) {
    double sorted[MM_MAX_REPEAT], sum=0, variance=0;
    mm_stats s={0,0,0,0};
    if (!n || n>MM_MAX_REPEAT) return s;
    memcpy(sorted,v,n*sizeof(double));
    qsort(sorted,n,sizeof(double),compare_double);
    for (unsigned i=0;i<n;++i) sum+=v[i];
    s.min=sorted[0]; s.mean=sum/n;
    s.median=n%2 ? sorted[n/2] : (sorted[n/2-1]+sorted[n/2])/2;
    for (unsigned i=0;i<n;++i) variance+=(v[i]-s.mean)*(v[i]-s.mean);
    s.stddev=sqrt(variance/n);
    return s;
}
static int budget(const mm_config *c) {
    size_t count=1+(c->size/8-1)/(c->stride/8), extra=0;
    if (c->test==LATENCY) extra=count;
    if (c->test==RANDOM_READ || c->test==RANDOM_WRITE) extra=c->accesses ? c->accesses : count;
    size_t buffers=c->size;
    if (c->test==COPY && c->size>SIZE_MAX-buffers) return -1;
    if (c->test==COPY) buffers+=c->size;
    if (c->test==TRIAD) {
        if (c->size>SIZE_MAX/2 || buffers>SIZE_MAX-2*c->size) return -1;
        buffers+=2*c->size;
    }
    if (extra>(SIZE_MAX-buffers)/sizeof(size_t)) return -1;
    size_t each=buffers+extra*sizeof(size_t);
    if (each>c->memory_limit/c->threads) return -1;
    /* Reject configurations that cannot even represent one aggregate pass. */
    size_t ops=(c->test>=RANDOM_READ) ? (c->accesses ? c->accesses : count) : count;
    if (ops>UINT64_MAX/24/c->threads || (c->iterations && c->iterations>UINT64_MAX/24/c->threads/ops)) return -1;
    return 0;
}
int mm_run(const mm_config *c, mm_result *r) {
    gate g;
    worker *ws[MM_MAX_THREADS]={0};
    mm_thread ts[MM_MAX_THREADS];
    unsigned launched=0;
    int rc=-1;
    double bandwidth[MM_MAX_REPEAT], latency[MM_MAX_REPEAT], elapsed[MM_MAX_REPEAT];
    memset(r,0,sizeof(*r)); memset(&g,0,sizeof(g));
    if (budget(c)) { fprintf(stderr,"Allocation budget or workload counter exceeded (limit=%zu bytes); reduce size/threads/accesses/iterations or raise --memory-limit\n",c->memory_limit); return -1; }
    if (mm_mutex_init(&g.mutex)) return -1;
    if (mm_cond_init(&g.cond)) { mm_mutex_destroy(&g.mutex); return -1; }
    r->workers=calloc((size_t)c->threads*c->repeat,sizeof(mm_sample));
    if (!r->workers) goto cleanup;
    for (unsigned i=0;i<c->threads;++i) {
        ws[i]=mm_alloc(sizeof(worker));
        if (!ws[i]) break;
        memset(ws[i],0,sizeof(worker));
        ws[i]->config=c; ws[i]->id=i; ws[i]->g=&g;
        if (mm_thread_start(&ts[i],work,ws[i])) break;
        ++launched;
    }
    if (launched!=c->threads) goto stop;
    for (unsigned round=0;round<c->repeat;++round) {
        mm_lock(&g.mutex);
        while (g.ready<(round+1)*c->threads) mm_wait(&g.cond,&g.mutex);
        if (g.failed) { mm_unlock(&g.mutex); goto stop; }
        g.start=mm_now();
        g.generation=round+1;
        mm_broadcast(&g.cond);
        while (g.done<(round+1)*c->threads) mm_wait(&g.cond,&g.mutex);
        int failed=g.failed;
        double start=g.start;
        mm_unlock(&g.mutex);
        if (failed) goto stop;
        mm_sample *s=&r->samples[round];
        double end=start;
        for (unsigned i=0;i<c->threads;++i) {
            mm_sample *x=&ws[i]->samples[round];
            r->workers[(size_t)round*c->threads+i]=*x;
            if (UINT64_MAX-s->bytes<x->bytes || UINT64_MAX-s->accesses<x->accesses || UINT64_MAX-s->iterations<x->iterations) goto stop;
            s->bytes+=x->bytes; s->accesses+=x->accesses; s->iterations+=x->iterations;
            s->checksum^=x->checksum;
            if (ws[i]->ends[round]>end) end=ws[i]->ends[round];
        }
        s->seconds=end-start;
        if (!(s->seconds>0)) goto stop;
        s->gbps=(double)s->bytes/s->seconds/1e9;
        /* Mean worker dependent-load cost, not inverse aggregate throughput. */
        s->ns_access=0;
        for (unsigned i=0;i<c->threads;++i) s->ns_access+=ws[i]->samples[round].ns_access/c->threads;
        bandwidth[round]=s->gbps; latency[round]=s->ns_access; elapsed[round]=s->seconds;
        mm_lock(&g.mutex); g.released=round+1; mm_broadcast(&g.cond); mm_unlock(&g.mutex);
    }
    rc=0;
stop:
    mm_lock(&g.mutex); g.stop=1; mm_broadcast(&g.cond); mm_unlock(&g.mutex);
    for (unsigned i=0;i<launched;++i) if (mm_thread_join(ts[i])) rc=-1;
    for (unsigned i=0;i<c->threads;++i) if (ws[i]) {
        r->sequence_hash^=ws[i]->memory.sequence_hash;
        mm_memory_destroy(&ws[i]->memory); mm_free(ws[i]);
    }
cleanup:
    mm_cond_destroy(&g.cond); mm_mutex_destroy(&g.mutex);
    if (rc) { free(r->workers); r->workers=NULL; fprintf(stderr,"Benchmark failed: allocation, CPU affinity, thread, counter, clock or verification error\n"); return -1; }
    r->bandwidth=mm_statistics(bandwidth,c->repeat);
    r->latency=mm_statistics(latency,c->repeat);
    r->elapsed=mm_statistics(elapsed,c->repeat);
    r->efficiency=1;
    return 0;
}
