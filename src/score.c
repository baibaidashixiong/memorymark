#include "mm.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
double mm_score_from_rates(const double *rates, unsigned n) {
    long double logs=0;
    if (!n) return 0;
    for (unsigned i=0;i<n;++i) {
        if (!(rates[i]>0) || !isfinite(rates[i])) return 0;
        logs+=logl((long double)rates[i]);
    }
    return (double)expl(logs/n);
}
int mm_run_score(const mm_config *base, mm_score_result *r) {
    static const mm_test tests[MM_SCORE_COMPONENTS]={SEQ_READ,SEQ_WRITE,MIX,RANDOM_READ,RANDOM_WRITE,LATENCY};
    mm_config c=*base;
    double rates[MM_SCORE_COMPONENTS];
    double begin=mm_now();
    memset(r,0,sizeof(*r));
    c.score=0; c.sweep=0; c.thread_sweep=0; c.size=128*1024*1024;
    c.threads=1; c.repeat=3; c.duration=.5; c.warmup=.1;
    c.stride=8; c.accesses=8*1024*1024; c.iterations=0; c.read_ratio=70; c.seed=1;
    r->size=c.size; r->threads=c.threads; r->repeat=c.repeat; r->duration=c.duration;
    r->warmup=c.warmup; r->read_ratio=c.read_ratio; r->seed=c.seed;
    r->accesses=c.accesses;
    r->memory_limit=base->memory_limit;
    r->cpu=base->ncpus ? "explicit" : "os-selected";
    for (unsigned i=0;i<MM_SCORE_COMPONENTS;++i) {
        mm_result result;
        c.test=tests[i];
        if (mm_run(&c,&result)) return -1;
        mm_score_component *x=&r->components[i];
        x->test=tests[i];
        x->gbps=result.bandwidth.median;
        x->ns_access=tests[i]==LATENCY ? result.latency.median : 0;
        x->million_ops=tests[i]==LATENCY ? 1000/x->ns_access : x->gbps*125;
        x->cv=tests[i]==LATENCY
            ? (result.latency.mean>0 ? result.latency.stddev/result.latency.mean : HUGE_VAL)
            : (result.bandwidth.mean>0 ? result.bandwidth.stddev/result.bandwidth.mean : HUGE_VAL);
        rates[i]=x->million_ops;
        if (x->cv>r->max_cv) r->max_cv=x->cv;
        free(result.workers);
    }
    r->score=mm_score_from_rates(rates,MM_SCORE_COMPONENTS);
    r->stable=r->score>0 && r->max_cv<=.10;
    r->elapsed=mm_now()-begin;
    return r->score>0 ? 0 : -1;
}
