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
static void csv_quoted(const char *s) {
    putchar('"');
    for (;*s;++s) {
        if (*s=='"') putchar('"');
        putchar(*s);
    }
    putchar('"');
}
static void system_json(const mm_system_info *s) {
    printf("{\"cpu_model\":"); quoted(s->cpu_model);
    printf(",\"kernel\":"); quoted(s->kernel);
    printf(",\"machine\":"); quoted(s->machine);
    printf(",\"endianness\":"); quoted(s->endianness);
    printf(",\"pointer_bits\":%u,\"page_size_bytes\":%zu,\"l1d_cache_bytes\":",s->pointer_bits,s->page_size);
    if (s->l1d_cache) printf("%zu",s->l1d_cache); else printf("null");
    printf(",\"l2_cache_bytes\":");
    if (s->l2_cache) printf("%zu",s->l2_cache); else printf("null");
    printf(",\"l3_cache_bytes\":");
    if (s->l3_cache) printf("%zu",s->l3_cache); else printf("null");
    printf(",\"allowed_cpus\":%u,\"numa_nodes\":",s->allowed_cpus);
    if (s->numa_nodes) printf("%u",s->numa_nodes); else printf("null");
    putchar('}');
}
static void system_text(const mm_system_info *s) {
    printf("Architecture: %s | CPU: %s | kernel: %s | %u-bit %s-endian\n",s->machine,s->cpu_model,s->kernel,s->pointer_bits,s->endianness);
    printf("Topology: %u allowed CPUs | NUMA nodes: ",s->allowed_cpus);
    if (s->numa_nodes) printf("%u",s->numa_nodes); else printf("unknown");
    printf(" | page: %zu bytes | L1d/L2/L3: ",s->page_size);
    if (s->l1d_cache) printf("%zu",s->l1d_cache); else printf("?");
    putchar('/');
    if (s->l2_cache) printf("%zu",s->l2_cache); else printf("?");
    putchar('/');
    if (s->l3_cache) printf("%zu",s->l3_cache); else printf("?");
    printf(" bytes\n");
}
static void csv_system_header(void) {
    printf("machine,cpu_model,kernel,endianness,pointer_bits,page_size_bytes,l1d_cache_bytes,l2_cache_bytes,l3_cache_bytes,allowed_cpus,numa_nodes");
}
static void csv_system_values(const mm_system_info *s) {
    csv_quoted(s->machine); putchar(',');
    csv_quoted(s->cpu_model); putchar(',');
    csv_quoted(s->kernel); putchar(',');
    csv_quoted(s->endianness);
    printf(",%u,%zu,",s->pointer_bits,s->page_size);
    if (s->l1d_cache) printf("%zu",s->l1d_cache);
    putchar(',');
    if (s->l2_cache) printf("%zu",s->l2_cache);
    putchar(',');
    if (s->l3_cache) printf("%zu",s->l3_cache);
    printf(",%u,",s->allowed_cpus);
    if (s->numa_nodes) printf("%u",s->numa_nodes);
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
    mm_system_info info;
    mm_collect_system_info(&info);
    if (c->format==1) {
        printf("{\"schema_version\":1,\"architecture\":"); system_json(&info); printf(",\"results\":[");
    } else if (c->format==2) {
        printf("version,os,arch,"); csv_system_header();
        puts(",compiler,build,test,size_per_thread,threads,stride,seed,read_ratio,accesses_per_pass_requested,iterations_requested,duration,warmup,repeat,memory_limit,affinity,sequence_hash,verified,round,worker,seconds,iterations,accesses,logical_bytes,gbps,ns_access,checksum,gbps_min,gbps_median,gbps_mean,gbps_stddev,latency_min,latency_median,latency_mean,latency_stddev,seconds_min,seconds_median,seconds_mean,seconds_stddev,scaling_efficiency");
    } else {
        printf("MemoryMark %s | %s %s | %s | %s\n",MM_VERSION,mm_os(),mm_arch(),mm_compiler(),MM_BUILD);
        system_text(&info);
        puts("Logical traffic only; cached stores do not imply DRAM writeback.");
    }
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
        printf("{\"schema_version\":2,\"score_version\":2,\"tool\":\"MemoryMark\",\"tool_version\":\"%s\",\"os\":\"%s\",\"arch\":\"%s\",\"compiler\":",MM_VERSION,mm_os(),mm_arch());
        quoted(mm_compiler());
        printf(",\"architecture\":"); system_json(&r->system);
        printf(",\"build\":\"%s\",\"score\":%.9g,\"score_unit\":\"million_logical_accesses_per_second_geometric_mean\",\"status\":\"%s\",\"verified\":true,\"comparable\":%s,\"sequence_hash\":\"%016" PRIx64 "\",\"max_variability\":%.9g,\"elapsed_seconds\":%.9g,\"profile\":{\"name\":\"%s\",\"size_per_thread\":%zu,\"accesses_per_pass\":%zu,\"threads\":%u,\"duration_per_component\":%.9g,\"warmup\":%.9g,\"repeat\":%u,\"seed\":%" PRIu64 ",\"mix_read_ratio\":%u,\"stream_stride\":8,\"latency_stride\":%zu,\"traffic\":\"logical_8_byte_accesses\",\"cache_policy\":\"warm_no_flush\"},\"cpu_affinity\":[",MM_BUILD,r->score,r->stable?"stable":"unstable",r->quick?"false":"true",r->sequence_hash,r->max_cv,r->elapsed,r->quick?"memorymark-quick-v2":"memorymark-v2",r->size,r->accesses,r->threads,r->duration,r->warmup,r->repeat,r->seed,r->read_ratio,r->latency_stride);
        for (int i=0;i<c->ncpus;++i) printf("%s%d",i?",":"",c->cpus[i]);
        printf("],\"components\":[");
        for (unsigned i=0;i<MM_SCORE_COMPONENTS;++i) {
            const mm_score_component *x=&r->components[i];
            if (i) putchar(',');
            printf("{\"test\":\"%s\",\"gbps\":%.9g,\"million_ops_per_second\":%.9g,\"variability\":%.9g,\"sequence_hash\":\"%016" PRIx64 "\",\"ns_access\":",mm_test_name(x->test),x->gbps,x->million_ops,x->cv,x->sequence_hash);
            if (x->test==LATENCY) printf("%.9g",x->ns_access); else printf("null");
            putchar('}');
        }
        printf("]}\n");
    } else if (c->format==2) {
        printf("score_version,tool_version,score,unit,status,comparable,sequence_hash,stream_stride,latency_stride,max_variability,elapsed_seconds,"); csv_system_header();
        printf(",size_per_thread,accesses_per_pass,threads,duration,warmup,repeat,seed,mix_read_ratio,affinity");
        for (unsigned i=0;i<MM_SCORE_COMPONENTS;++i) printf(",%s_gbps,%s_mops,%s_variability,%s_sequence_hash,%s_ns_access",mm_test_name((mm_test)i),mm_test_name((mm_test)i),mm_test_name((mm_test)i),mm_test_name((mm_test)i),mm_test_name((mm_test)i));
        printf("\n2,%s,%.9g,MMark,%s,%s,%016" PRIx64 ",8,%zu,%.9g,%.9g,",MM_VERSION,r->score,r->stable?"stable":"unstable",r->quick?"false":"true",r->sequence_hash,r->latency_stride,r->max_cv,r->elapsed);
        csv_system_values(&r->system);
        printf(",%zu,%zu,%u,%.9g,%.9g,%u,%" PRIu64 ",%u,",r->size,r->accesses,r->threads,r->duration,r->warmup,r->repeat,r->seed,r->read_ratio);
        affinity(c);
        for (unsigned i=0;i<MM_SCORE_COMPONENTS;++i) {
            const mm_score_component *x=&r->components[i];
            printf(",%.9g,%.9g,%.9g,%016" PRIx64 ",",x->gbps,x->million_ops,x->cv,x->sequence_hash);
            if (x->test==LATENCY) printf("%.9g",x->ns_access);
        }
        putchar('\n');
    } else {
        printf("MemoryMark Score: %.1f MMark\n",r->score);
        printf("Status: %s (max variability %.1f%%, verification PASSED)\n",r->stable?"STABLE":"UNSTABLE",r->max_cv*100);
        printf("Profile: %s | %u thread | %zu MiB/thread | %zu accesses/pass | %.2f s x %u | warm %.2f s\n",r->quick?"quick-diagnostic-v2":"fixed-v2",r->threads,r->size/(1024*1024),r->accesses,r->duration,r->repeat,r->warmup);
        printf("Sequence: aggregate=%016" PRIx64 " | stream-stride=%zu | latency-stride=%zu\n",r->sequence_hash,(size_t)8,r->latency_stride);
        printf("Environment: %s %s | %s | %s",mm_os(),mm_arch(),mm_compiler(),MM_BUILD);
        if (c->ncpus) { printf(" | CPU "); affinity(c); }
        printf("\n\n");
        system_text(&r->system);
        putchar('\n');
        for (unsigned i=0;i<MM_SCORE_COMPONENTS;++i) {
            const mm_score_component *x=&r->components[i];
            printf("%-19s %9.2f GB/s  %10.2f Mops/s",score_label(x->test),x->gbps,x->million_ops);
            if (x->test==LATENCY) printf("  (%0.2f ns/access)",x->ns_access);
            putchar('\n');
        }
        printf("\nTotal run: %.1f s | Score = geometric mean of six million-access rates\n",r->elapsed);
        if (r->quick) printf("Quick profile is for diagnostics only; do not compare its score across machines.\n");
        else printf("Compare only MemoryMark %s results from the same build type and idle system.\n",MM_VERSION);
    }
}
static void csv_row(const mm_config *c,const mm_result *r,unsigned round,int worker,const mm_sample *s,const mm_system_info *info) {
    printf("%s,%s,%s,",MM_VERSION,mm_os(),mm_arch());
    csv_system_values(info);
    printf(",\"%s\",%s,%s,%zu,%u,%zu,%" PRIu64 ",%u,%zu,%" PRIu64 ",%.9g,%.9g,%u,%zu,",mm_compiler(),MM_BUILD,mm_test_name(c->test),c->size,c->threads,c->stride,c->seed,c->read_ratio,c->accesses,c->iterations,c->duration,c->warmup,c->repeat,c->memory_limit);
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
        mm_system_info info;
        mm_collect_system_info(&info);
        for (unsigned j=0;j<c->repeat;++j) {
            csv_row(c,r,j,-1,&r->samples[j],&info);
            for (unsigned i=0;i<c->threads;++i) csv_row(c,r,j,(int)i,&r->workers[(size_t)j*c->threads+i],&info);
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
