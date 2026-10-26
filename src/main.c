#include "mm.h"
#include <stdlib.h>
int main(int argc, char **argv) {
    mm_config config;
    int parsed=mm_parse(argc,argv,&config), first=1, status=0;
    if (parsed) return parsed<0 ? 2 : 0;
    if (config.score) {
        mm_score_result result;
        if (mm_run_score(&config,&result)) {
            fprintf(stderr,"Score suite failed; retry on an idle system or raise --memory-limit\n");
            return 1;
        }
        mm_score_output(&config,&result);
        if (fflush(stdout) || ferror(stdout)) { fprintf(stderr,"Output write failed\n"); return 1; }
        return 0;
    }
    mm_output_begin(&config);
    size_t size=config.sweep ? config.min_size : config.size;
    for (;;) {
        int first_test=config.test==ALL ? SEQ_READ : (int)config.test;
        int last_test=config.test==ALL ? LATENCY : (int)config.test;
        for (int t=first_test;t<=last_test;++t) {
            double baseline=0;
            unsigned begin=config.thread_sweep ? 1 : config.threads;
            for (unsigned threads=begin;threads<=config.threads;++threads) {
                mm_config run=config;
                mm_result result;
                run.size=size; run.test=(mm_test)t; run.threads=threads;
                if (mm_run(&run,&result)) { status=1; goto finish; }
                if (baseline==0) baseline=result.bandwidth.median;
                result.efficiency=config.thread_sweep ? result.bandwidth.median/baseline/threads : (threads==1 ? 1 : 0);
                mm_output(&run,&result,first); first=0;
                free(result.workers);
            }
        }
        if (!config.sweep || size==config.max_size) break;
        size_t next;
        if (config.step) {
            next=config.max_size-size<config.step ? config.max_size : size+config.step;
        } else {
            long double scaled=(long double)size*(long double)config.scale;
            if (scaled>=(long double)config.max_size) next=config.max_size;
            else { next=(size_t)scaled; next-=next%8; if (next<=size) next=size+8; }
        }
        size=next;
    }
finish:
    mm_output_end(&config);
    if (fflush(stdout) || ferror(stdout)) { fprintf(stderr,"Output write failed\n"); return 1; }
    return status;
}
