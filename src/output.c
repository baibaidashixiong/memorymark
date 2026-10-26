#include "mm.h"
#include <inttypes.h>
static void quoted(const char *s) {
    putchar('"');
    for (;*s;++s) {
        unsigned char c=(unsigned char)*s;
        if (c=='"' || c=='\\') { putchar('\\'); putchar(c); }
        else if (c<32) printf("\\u%04x",c);
        else putchar(c);
    }
    putchar('"');
}
static void stats_json(const char *name, mm_stats s) {
    printf(",\"%s\":{\"min\":%.9g,\"median\":%.9g,\"mean\":%.9g,\"stddev\":%.9g}",name,s.min,s.median,s.mean,s.stddev);
}
static void sample_json(const mm_sample *s, int latency) {
    printf("{\"seconds\":%.9g,\"iterations\":%" PRIu64 ",\"accesses\":%" PRIu64 ",\"logical_bytes\":%" PRIu64 ",\"gbps\":%.9g,\"ns_access\":",s->seconds,s->iterations,s->accesses,s->bytes,s->gbps);
    if (latency) printf("%.9g",s->ns_access); else printf("null");
    printf(",\"checksum\":\"%016" PRIx64 "\"}",s->checksum);
}
void mm_output_begin(const mm_config *c) {
    if (c->format==1) puts("{\"schema_version\":1,\"results\":[");
    else if (c->format==2) puts("version,os,arch,compiler,build,test,size_per_thread,threads,stride,seed,read_ratio,accesses_per_pass_requested,iterations_requested,duration,warmup,repeat,memory_limit,affinity,sequence_hash,verified,round,worker,seconds,iterations,accesses,logical_bytes,gbps,ns_access,checksum,gbps_min,gbps_median,gbps_mean,gbps_stddev,latency_min,latency_median,latency_mean,latency_stddev,seconds_min,seconds_median,seconds_mean,seconds_stddev,scaling_efficiency");
    else printf("MemoryMark %s | %s %s | %s | %s\nLogical traffic only; cached stores do not imply DRAM writeback.\n",MM_VERSION,mm_os(),mm_arch(),mm_compiler(),MM_BUILD);
}
static void affinity(const mm_config *c) {
    if (!c->ncpus) printf("none");
    for (int i=0;i<c->ncpus;++i) printf("%s%d",i ? ";" : "",c->cpus[i]);
}
static const char *score_label(mm_test t) {
    static const char *labels[]={"Sequential read","Sequential write","70/30 read/write","Random read","Random write","Dependent latency"};
    return labels[t];
}
void mm_score_output(const mm_config *c,const mm_score_result *r) {
    if (c->format==1) {
        printf("{\"schema_version\":2,\"score_version\":1,\"tool\":\"MemoryMark\",\"tool_version\":\"%s\",\"os\":\"%s\",\"arch\":\"%s\",\"compiler\":",MM_VERSION,mm_os(),mm_arch());
        quoted(mm_compiler());
        printf(",\"build\":\"%s\",\"score\":%.9g,\"score_unit\":\"million_logical_accesses_per_second_geometric_mean\",\"status\":\"%s\",\"verified\":true,\"max_variability\":%.9g,\"elapsed_seconds\":%.9g,\"profile\":{\"name\":\"memorymark-v1\",\"size_per_thread\":%zu,\"accesses_per_pass\":%zu,\"threads\":%u,\"duration_per_component\":%.9g,\"warmup\":%.9g,\"repeat\":%u,\"seed\":%" PRIu64 ",\"mix_read_ratio\":%u,\"stride\":8,\"traffic\":\"logical_8_byte_accesses\",\"cache_policy\":\"warm_no_flush\"},\"cpu_affinity\":[",MM_BUILD,r->score,r->stable?"stable":"unstable",r->max_cv,r->elapsed,r->size,r->accesses,r->threads,r->duration,r->warmup,r->repeat,r->seed,r->read_ratio);
        for (int i=0;i<c->ncpus;++i) printf("%s%d",i?",":"",c->cpus[i]);
        printf("],\"components\":[");
        for (unsigned i=0;i<MM_SCORE_COMPONENTS;++i) {
            const mm_score_component *x=&r->components[i];
            if (i) putchar(',');
            printf("{\"test\":\"%s\",\"gbps\":%.9g,\"million_ops_per_second\":%.9g,\"variability\":%.9g,\"ns_access\":",mm_test_name(x->test),x->gbps,x->million_ops,x->cv);
            if (x->test==LATENCY) printf("%.9g",x->ns_access); else printf("null");
            putchar('}');
        }
        printf("]}\n");
    } else if (c->format==2) {
        printf("score_version,tool_version,score,unit,status,max_variability,elapsed_seconds,size_per_thread,accesses_per_pass,threads,duration,warmup,repeat,seed,mix_read_ratio,affinity");
        for (unsigned i=0;i<MM_SCORE_COMPONENTS;++i) printf(",%s_gbps,%s_mops,%s_variability,%s_ns_access",mm_test_name((mm_test)i),mm_test_name((mm_test)i),mm_test_name((mm_test)i),mm_test_name((mm_test)i));
        printf("\n1,%s,%.9g,MMark,%s,%.9g,%.9g,%zu,%zu,%u,%.9g,%.9g,%u,%" PRIu64 ",%u,",MM_VERSION,r->score,r->stable?"stable":"unstable",r->max_cv,r->elapsed,r->size,r->accesses,r->threads,r->duration,r->warmup,r->repeat,r->seed,r->read_ratio);
        affinity(c);
        for (unsigned i=0;i<MM_SCORE_COMPONENTS;++i) {
            const mm_score_component *x=&r->components[i];
            printf(",%.9g,%.9g,%.9g,",x->gbps,x->million_ops,x->cv);
            if (x->test==LATENCY) printf("%.9g",x->ns_access);
        }
        putchar('\n');
    } else {
        printf("MemoryMark Score: %.1f MMark\n",r->score);
        printf("Status: %s (max variability %.1f%%, verification PASSED)\n",r->stable?"STABLE":"UNSTABLE",r->max_cv*100);
        printf("Profile: fixed-v1 | %u thread | %zu MiB/thread | %zuM accesses/pass | %.1f s x %u | warm %.1f s\n",r->threads,r->size/(1024*1024),r->accesses/(1024*1024),r->duration,r->repeat,r->warmup);
        printf("Environment: %s %s | %s | %s",mm_os(),mm_arch(),mm_compiler(),MM_BUILD);
        if (c->ncpus) { printf(" | CPU "); affinity(c); }
        printf("\n\n");
        for (unsigned i=0;i<MM_SCORE_COMPONENTS;++i) {
            const mm_score_component *x=&r->components[i];
            printf("%-19s %9.2f GB/s  %10.2f Mops/s",score_label(x->test),x->gbps,x->million_ops);
            if (x->test==LATENCY) printf("  (%0.2f ns/access)",x->ns_access);
            putchar('\n');
        }
        printf("\nTotal run: %.1f s | Score = geometric mean of six million-access rates\n",r->elapsed);
        printf("Compare only MemoryMark %s results from the same build type and idle system.\n",MM_VERSION);
    }
}
static void csv_row(const mm_config *c,const mm_result *r,unsigned round,int worker,const mm_sample *s) {
    printf("%s,%s,%s,\"%s\",%s,%s,%zu,%u,%zu,%" PRIu64 ",%u,%zu,%" PRIu64 ",%.9g,%.9g,%u,%zu,",MM_VERSION,mm_os(),mm_arch(),mm_compiler(),MM_BUILD,mm_test_name(c->test),c->size,c->threads,c->stride,c->seed,c->read_ratio,c->accesses,c->iterations,c->duration,c->warmup,c->repeat,c->memory_limit);
    affinity(c);
    printf(",%016" PRIx64 ",true,%u,%d,%.9g,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%.9g,",r->sequence_hash,round,worker,s->seconds,s->iterations,s->accesses,s->bytes,s->gbps);
    if (c->test==LATENCY) printf("%.9g",s->ns_access);
    printf(",%016" PRIx64 ",%.9g,%.9g,%.9g,%.9g,",s->checksum,r->bandwidth.min,r->bandwidth.median,r->bandwidth.mean,r->bandwidth.stddev);
    if (c->test==LATENCY) printf("%.9g,%.9g,%.9g,%.9g",r->latency.min,r->latency.median,r->latency.mean,r->latency.stddev);
    else printf(",,,");
    printf(",%.9g,%.9g,%.9g,%.9g,",r->elapsed.min,r->elapsed.median,r->elapsed.mean,r->elapsed.stddev);
    if (r->efficiency>0) printf("%.9g",r->efficiency);
    putchar('\n');
}
void mm_output(const mm_config *c,const mm_result *r,int first) {
    if (c->format==1) {
        if (!first) puts(",");
        printf("{\"version\":\"%s\",\"os\":\"%s\",\"arch\":\"%s\",\"compiler\":",MM_VERSION,mm_os(),mm_arch());
        quoted(mm_compiler());
        printf(",\"build\":\"%s\",\"test\":\"%s\",\"size_per_thread\":%zu,\"threads\":%u,\"stride\":%zu,\"seed\":%" PRIu64 ",\"read_ratio\":%u,\"accesses_per_pass_requested\":%zu,\"iterations_requested\":%" PRIu64 ",\"duration\":%.9g,\"warmup\":%.9g,\"repeat\":%u,\"memory_limit\":%zu,\"cpu_affinity\":[",MM_BUILD,mm_test_name(c->test),c->size,c->threads,c->stride,c->seed,c->read_ratio,c->accesses,c->iterations,c->duration,c->warmup,c->repeat,c->memory_limit);
        for (int i=0;i<c->ncpus;++i) printf("%s%d",i ? "," : "",c->cpus[i]);
        printf("],\"traffic\":\"logical_8_byte_accesses_excludes_index_stream_and_write_allocate\",\"cache_policy\":\"warm_cached_no_flush\",\"numa_policy\":\"OS_first_touch_private_worker\",\"sequence_hash\":\"%016" PRIx64 "\",\"verified\":true,\"scaling_efficiency\":",r->sequence_hash);
        if (r->efficiency>0) printf("%.9g",r->efficiency); else printf("null");
        stats_json("gbps",r->bandwidth); stats_json("seconds",r->elapsed);
        if (c->test==LATENCY) stats_json("ns_access",r->latency);
        printf(",\"rounds\":[");
        for (unsigned j=0;j<c->repeat;++j) {
            if (j) putchar(',');
            printf("{\"total\":"); sample_json(&r->samples[j],c->test==LATENCY);
            printf(",\"workers\":[");
            for (unsigned i=0;i<c->threads;++i) { if (i) putchar(','); sample_json(&r->workers[(size_t)j*c->threads+i],c->test==LATENCY); }
            printf("]}");
        }
        printf("]}");
    } else if (c->format==2) {
        for (unsigned j=0;j<c->repeat;++j) {
            csv_row(c,r,j,-1,&r->samples[j]);
            for (unsigned i=0;i<c->threads;++i) csv_row(c,r,j,(int)i,&r->workers[(size_t)j*c->threads+i]);
        }
    } else {
        printf("\n%s | size/thread=%zu bytes threads=%u stride=%zu seed=%" PRIu64 " read-ratio=%u%%\n",mm_test_name(c->test),c->size,c->threads,c->stride,c->seed,c->read_ratio);
        printf("duration=%.6g warmup=%.6g repeat=%u iterations-requested=%" PRIu64 " accesses-requested=%zu budget=%zu affinity=",c->duration,c->warmup,c->repeat,c->iterations,c->accesses,c->memory_limit);
        affinity(c); putchar('\n');
        printf("GB/s: min=%.4f median=%.4f mean=%.4f stddev=%.4f\n",r->bandwidth.min,r->bandwidth.median,r->bandwidth.mean,r->bandwidth.stddev);
        printf("seconds: min=%.6f median=%.6f mean=%.6f stddev=%.6f\n",r->elapsed.min,r->elapsed.median,r->elapsed.mean,r->elapsed.stddev);
        if (c->test==LATENCY) printf("Dependent ns/access: min=%.3f median=%.3f mean=%.3f stddev=%.3f\n",r->latency.min,r->latency.median,r->latency.mean,r->latency.stddev);
        if (r->efficiency>0) printf("Scaling efficiency: %.4f\n",r->efficiency);
        for (unsigned j=0;j<c->repeat;++j) {
            const mm_sample *s=&r->samples[j];
            printf("round=%u seconds=%.6f passes(sum)=%" PRIu64 " accesses=%" PRIu64 " bytes=%" PRIu64 " GB/s=%.4f checksum=%016" PRIx64 "\n",j,s->seconds,s->iterations,s->accesses,s->bytes,s->gbps,s->checksum);
            for (unsigned i=0;i<c->threads;++i) {
                s=&r->workers[(size_t)j*c->threads+i];
                printf("  worker=%u seconds=%.6f passes=%" PRIu64 " accesses=%" PRIu64 " bytes=%" PRIu64 " GB/s=%.4f checksum=%016" PRIx64 "\n",i,s->seconds,s->iterations,s->accesses,s->bytes,s->gbps,s->checksum);
            }
        }
        printf("verification=passed sequence_hash=%016" PRIx64 " cache=warm/no-flush NUMA=OS-first-touch\n",r->sequence_hash);
    }
}
void mm_output_end(const mm_config *c) { if (c->format==1) puts("\n]}"); }
