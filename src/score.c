#include "mm.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
double mm_score_from_rates(const double *rates, unsigned n) {
    double *weights;
    if (!n) return 0;
    weights=malloc(n*sizeof(*weights));
    if (!weights) return 0;
    for (unsigned i=0;i<n;++i) weights[i]=1;
    double score=mm_score_weighted(rates,weights,n);
    free(weights);
    return score;
}
double mm_score_weighted(const double *rates, const double *weights, unsigned n) {
    long double logs=0, total_weight=0;
    if (!n) return 0;
    for (unsigned i=0;i<n;++i) {
        if (!(rates[i]>0) || !mm_isfinite(rates[i]) || !(weights[i]>0) || !mm_isfinite(weights[i])) return 0;
        logs+=(long double)weights[i]*logl((long double)rates[i]);
        total_weight+=weights[i];
    }
    return total_weight>0 ? (double)expl(logs/total_weight) : 0;
}
int mm_run_score(const mm_config *base, mm_score_result *r) {
    static const mm_test tests[MM_SCORE_COMPONENTS]={SEQ_READ,SEQ_WRITE,MIX,COPY,TRIAD,RANDOM_READ,RANDOM_WRITE,LATENCY};
    static const double weights[MM_SCORE_COMPONENTS]={.06,.06,.08,.11,.14,.15,.15,.25};
    static const mm_score_group groups[MM_SCORE_COMPONENTS]={GROUP_STREAM,GROUP_STREAM,GROUP_STREAM,GROUP_STREAM,GROUP_STREAM,GROUP_RANDOM,GROUP_RANDOM,GROUP_LATENCY};
    mm_config c=*base;
    double rates[MM_SCORE_COMPONENTS];
    double group_rates[MM_SCORE_GROUPS][MM_SCORE_COMPONENTS];
    double group_weights[MM_SCORE_GROUPS][MM_SCORE_COMPONENTS];
    unsigned group_counts[MM_SCORE_GROUPS]={0};
    double begin=mm_now();
    memset(r,0,sizeof(*r));
    c.score=0; c.quick=0; c.sweep=0; c.thread_sweep=0;
    c.threads=1; c.iterations=0; c.read_ratio=70; c.seed=1;
    if (base->quick) {
        c.size=16*1024*1024; c.repeat=2; c.duration=.03; c.warmup=.01;
        c.accesses=65536;
    } else {
        c.size=128*1024*1024; c.repeat=5; c.duration=.5; c.warmup=.1;
        c.accesses=8*1024*1024;
    }
    r->size=c.size; r->threads=c.threads; r->repeat=c.repeat; r->duration=c.duration;
    r->warmup=c.warmup; r->read_ratio=c.read_ratio; r->seed=c.seed;
    r->accesses=c.accesses;
    r->latency_stride=64;
    r->quick=base->quick;
    r->memory_limit=base->memory_limit;
    r->cpu=base->ncpus ? "explicit" : "os-selected";
    mm_collect_system_info(&r->system);
    for (unsigned i=0;i<MM_SCORE_COMPONENTS;++i) {
        mm_result result;
        c.test=tests[i];
        c.stride=tests[i]==LATENCY ? r->latency_stride : 8;
        if (mm_run(&c,&result)) return -1;
        mm_score_component *x=&r->components[i];
        x->test=tests[i];
        x->gbps=result.bandwidth.median;
        x->ns_access=tests[i]==LATENCY ? result.latency.median : 0;
        double operation_rates[MM_MAX_REPEAT];
        if (result.samples[0].seconds<=0) { free(result.workers); return -1; }
        for (unsigned round=0;round<c.repeat;++round) operation_rates[round]=(double)result.samples[round].accesses/result.samples[round].seconds;
        x->million_ops=mm_statistics(operation_rates,c.repeat).median/1e6;
        if (!(x->million_ops>0) || !mm_isfinite(x->million_ops)) { free(result.workers); return -1; }
        x->cv=tests[i]==LATENCY
            ? (result.latency.mean>0 ? result.latency.stddev/result.latency.mean : HUGE_VAL)
            : (result.bandwidth.mean>0 ? result.bandwidth.stddev/result.bandwidth.mean : HUGE_VAL);
        rates[i]=x->million_ops;
        x->weight=weights[i];
        x->group=groups[i];
        group_rates[x->group][group_counts[x->group]]=x->million_ops;
        group_weights[x->group][group_counts[x->group]++]=x->weight;
        x->sequence_hash=result.sequence_hash;
        r->sequence_hash^=result.sequence_hash;
        if (x->cv>r->max_cv) r->max_cv=x->cv;
        free(result.workers);
    }
    for (unsigned group=0;group<MM_SCORE_GROUPS;++group) r->group_scores[group]=mm_score_weighted(group_rates[group],group_weights[group],group_counts[group]);
    r->score=mm_score_weighted(rates,weights,MM_SCORE_COMPONENTS);
    r->stable=r->score>0 && r->max_cv<=.10;
    r->elapsed=mm_now()-begin;
    return r->score>0 ? 0 : -1;
}
