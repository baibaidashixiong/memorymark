#include "mm.h"
#include <inttypes.h>

static void json_quoted(const char *text) {
    putchar('"');
    for (;*text;++text) {
        unsigned char c=(unsigned char)*text;
        if (c=='"' || c=='\\') { putchar('\\'); putchar(c); }
        else if (c<32) printf("\\u%04x",c);
        else putchar(c);
    }
    putchar('"');
}

static void process_json_architecture(void) {
    mm_system_info info;
    mm_collect_system_info(&info);
    printf("{\"cpu_model\":"); json_quoted(info.cpu_model);
    printf(",\"kernel\":"); json_quoted(info.kernel);
    printf(",\"machine\":"); json_quoted(info.machine);
    printf(",\"pointer_bits\":%u,\"page_size_bytes\":%zu}",info.pointer_bits,info.page_size);
}

void mm_process_output_begin(const mm_config *c) {
    if (c->format==1) {
        printf("{\"schema_version\":1,\"test\":\"process\",\"tool_version\":\"%s\",\"architecture\":",MM_VERSION);
        process_json_architecture();
        printf(",\"levels\":[");
    } else if (c->format==2) {
        puts("version,requested_processes,created_processes,capacity_limited,threads_per_process,private_per_worker,shared_bytes,thread_stack,iterations,baseline_pss,idle_pss,idle_rss,loaded_pss,loaded_rss,fixed_overhead_pss,stack_reserved,stack_pss,total_accesses,wakeup_min_ms,wakeup_median_ms,wakeup_max_ms,elapsed_seconds,rlimit_nproc,rlimit_as");
    } else {
        printf("MemoryMark %s process-level memory benchmark | %s %s | %s\n",MM_VERSION,mm_os(),mm_arch(),MM_BUILD);
        puts("Footprint stages are aggregate PSS/RSS across parent and child processes; shared pages are counted in each PSS.");
    }
}

void mm_process_output_level(const mm_config *c, const mm_process_level *r, int first) {
    if (c->format==1) {
        if (!first) putchar(',');
        printf("{\"requested\":%u,\"created\":%u,\"capacity_limited\":%s,\"threads_per_process\":%u,\"private_per_worker\":%zu,\"shared_bytes\":%zu,\"thread_stack\":%zu,\"iterations\":%" PRIu64 ",\"footprint_available\":%s,\"baseline\":{\"pss\":%zu,\"rss\":%zu},\"idle\":{\"pss\":%zu,\"rss\":%zu,\"fixed_overhead_pss\":%zu,\"fixed_overhead_per_process\":%.9g},\"loaded\":{\"pss\":%zu,\"rss\":%zu,\"private_bytes\":%zu,\"shared_bytes\":%zu},\"thread_stacks\":{\"reserved\":%zu,\"resident_pss\":%zu},\"work\":{\"total_accesses\":%" PRIu64 ",\"elapsed_seconds\":%.9g},\"ipc_wakeup_ms\":{\"min\":%.9g,\"median\":%.9g,\"max\":%.9g},\"rlimits\":{\"nproc\":%zu,\"address_space\":%zu},\"children\":[",r->requested,r->created,r->capacity_limited?"true":"false",r->threads_per_process,r->private_per_worker,r->shared_bytes,r->thread_stack,r->iterations,r->footprint_available?"true":"false",r->baseline_pss,r->baseline_rss,r->idle_pss,r->idle_rss,r->fixed_overhead_pss,r->created?(double)r->fixed_overhead_pss/r->created:0,r->loaded_pss,r->loaded_rss,r->private_per_worker*r->threads_per_process*r->created,r->shared_bytes,r->stack_reserved,r->stack_pss,r->total_accesses,r->elapsed_seconds,r->wakeup_min_ms,r->wakeup_median_ms,r->wakeup_max_ms,r->rlimit_nproc,r->rlimit_as);
        for (unsigned i=0;i<r->created;++i) {
            const mm_process_result *child=&r->children[i];
            if (i) putchar(',');
            printf("{\"pid\":%d,\"ok\":%s,\"workers\":%u,\"private_bytes\":%zu,\"shared_bytes\":%zu,\"idle_pss\":%zu,\"idle_rss\":%zu,\"loaded_pss\":%zu,\"loaded_rss\":%zu,\"stack_reserved\":%zu,\"stack_pss\":%zu,\"accesses\":%" PRIu64 ",\"wakeup_ms\":%.9g,\"work_seconds\":%.9g,\"checksum\":\"%016" PRIx64 "\"}",child->pid,child->ok?"true":"false",child->workers,child->private_bytes,child->shared_bytes,child->idle_pss,child->idle_rss,child->loaded_pss,child->loaded_rss,child->stack_reserved,child->stack_pss,child->accesses,child->wakeup_ms,child->work_seconds,child->checksum);
        }
        printf("]}");
    } else if (c->format==2) {
        printf("%s,%u,%u,%s,%u,%zu,%zu,%zu,%" PRIu64 ",%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%" PRIu64 ",%.9g,%.9g,%.9g,%.9g,%zu,%zu\n",MM_VERSION,r->requested,r->created,r->capacity_limited?"true":"false",r->threads_per_process,r->private_per_worker,r->shared_bytes,r->thread_stack,r->iterations,r->baseline_pss,r->idle_pss,r->idle_rss,r->loaded_pss,r->loaded_rss,r->fixed_overhead_pss,r->stack_reserved,r->stack_pss,r->total_accesses,r->wakeup_min_ms,r->wakeup_median_ms,r->wakeup_max_ms,r->elapsed_seconds,r->rlimit_nproc,r->rlimit_as);
    } else {
        printf("\nProcesses: requested=%u created=%u capacity-limited=%s | workers/process=%u\n",r->requested,r->created,r->capacity_limited?"yes":"no",r->threads_per_process);
        printf("Private/worker=%zu bytes | shared=%zu bytes | requested stack=%zu bytes | iterations=%" PRIu64 "\n",r->private_per_worker,r->shared_bytes,r->thread_stack,r->iterations);
        printf("PSS/RSS: baseline=%zu/%zu | idle=%zu/%zu | loaded=%zu/%zu bytes\n",r->baseline_pss,r->baseline_rss,r->idle_pss,r->idle_rss,r->loaded_pss,r->loaded_rss);
        printf("Fixed idle overhead: total=%zu bytes | per-process=%0.1f bytes\n",r->fixed_overhead_pss,r->created?(double)r->fixed_overhead_pss/r->created:0);
        printf("Thread stacks: reserved=%zu bytes | resident PSS=%zu bytes\n",r->stack_reserved,r->stack_pss);
        printf("IPC wakeup: min=%0.3f ms | median=%0.3f ms | max=%0.3f ms\n",r->wakeup_min_ms,r->wakeup_median_ms,r->wakeup_max_ms);
        printf("Work: accesses=%" PRIu64 " | elapsed=%0.6f s | RLIMIT_NPROC=%zu | RLIMIT_AS=%zu\n",r->total_accesses,r->elapsed_seconds,r->rlimit_nproc,r->rlimit_as);
        for (unsigned i=0;i<r->created;++i) {
            const mm_process_result *child=&r->children[i];
            printf("  pid=%d workers=%u idle-PSS=%zu loaded-PSS=%zu stack-PSS=%zu wake=%.3fms checksum=%016" PRIx64 "\n",child->pid,child->workers,child->idle_pss,child->loaded_pss,child->stack_pss,child->wakeup_ms,child->checksum);
        }
    }
}

void mm_process_output_end(const mm_config *c) {
    if (c->format==1) puts("\n]}");
}
