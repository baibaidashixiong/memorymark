#include "mm.h"
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
const char *mm_test_name(mm_test t) {
    static const char *names[] = {"seq-read", "seq-write", "mix", "random-read", "random-write", "latency", "all"};
    return names[t];
}
void mm_defaults(mm_config *c) {
    memset(c, 0, sizeof(*c));
    c->test = SEQ_READ; c->size = 8*1024*1024; c->stride = 8;
    c->threads = 1; c->repeat = 3; c->read_ratio = 50; c->seed = 1;
    c->duration = .1; c->warmup = .02; c->scale = 2;
    c->min_size = 4096; c->max_size = 64*1024*1024;
    c->memory_limit = (size_t)1024*1024*1024;
}
static int integer(const char *s, uint64_t *v) {
    char *end;
    unsigned long long n;
    if (!*s || *s < '0' || *s > '9') return -1;
    errno = 0; n = strtoull(s, &end, 10);
    if (errno || *end) return -1;
    *v = (uint64_t)n; return 0;
}
int mm_size(const char *s, size_t *v) {
    char *end;
    uint64_t factor = 1, n;
    if (!*s || *s < '0' || *s > '9') return -1;
    errno = 0; n = strtoull(s, &end, 10);
    if (errno) return -1;
    if (*end) {
        switch (*end++) {
        case 'K': case 'k': factor = 1024; break;
        case 'M': case 'm': factor = 1024*1024; break;
        case 'G': case 'g': factor = (uint64_t)1024*1024*1024; break;
        default: return -1;
        }
        if (!strcmp(end,"iB") || !strcmp(end,"B")) end += strlen(end);
        if (*end) return -1;
    }
    if (!n || n > SIZE_MAX / factor) return -1;
    *v = (size_t)(n*factor); return 0;
}
static int real_value(const char *s, double *v) {
    char *end;
    errno = 0; *v = strtod(s,&end);
    return !*s || *end || errno || !mm_isfinite(*v) ? -1 : 0;
}
void mm_help(void) {
    puts("MemoryMark " MM_VERSION " - logical memory traffic benchmark\n"
         "Usage: memorymark                         Run the fixed score suite\n"
         "       memorymark --score [--json]       Same, with explicit mode/output\n"
         "       memorymark --score-quick           Fast diagnostic-only score smoke\n"
         "       memorymark --test TYPE [options] Run detailed diagnostics\n"
         "  --score             Fixed v2 profile: 1 thread, 128M/thread, 0.5s x 5\n"
         "  --score-quick       Diagnostic profile for CI, not for hardware comparison\n"
         "  --test seq-read|seq-write|mix|random-read|random-write|latency|all\n"
         "  --size 8M           Working set PER THREAD (K/M/G = binary)\n"
         "  --threads 1         Threads, 1..256; private working sets\n"
         "  --thread-sweep      Run 1,2,...,threads; report scaling efficiency\n"
         "  --iterations N      Exact kernel passes per round (overrides duration)\n"
         "  --duration 0.1      Target seconds per round, 0.001..3600\n"
         "  --warmup 0.02       Minimum warmup seconds (one pass even if zero)\n"
         "  --repeat 3          Independent measured rounds, 1..100\n"
         "  --seed 1            Reproducible unsigned 64-bit seed\n"
         "  --stride 8          Byte stride, multiple of 8, <= working set\n"
         "  --accesses N        Random/latency accesses per pass (default: slots)\n"
         "  --read-ratio 50     Mix read percentage, integer 0..100\n"
         "  --sweep             Sweep working set sizes\n"
         "  --min-size 4K --max-size 64M --scale 2\n"
         "  --step SIZE         Linear sweep instead of multiplicative scale\n"
         "  --memory-limit 1G   Aggregate allocation budget incl. scratch\n"
         "  --cpu-affinity none|CPU,CPU,...  Explicit worker CPU list\n"
         "  --json | --csv      Machine-readable stdout (errors on stderr)\n"
         "  --help | --version\n"
         "Cached writes are NOT DRAM writeback; bandwidth is logical GB/s.\n"
         "Latency is serial dependent-load ns/access, not pure DRAM latency.");
}
int mm_parse(int argc, char **argv, mm_config *c) {
    mm_defaults(c);
    int test_seen=0, score=argc==1, quick=0, profile_override=0;
    for (int i=1; i<argc; ++i) {
        const char *k=argv[i], *v;
        uint64_t n;
        if (!strcmp(k,"--help")) { mm_help(); return 1; }
        if (!strcmp(k,"--version")) { puts("MemoryMark " MM_VERSION); return 1; }
        if (!strcmp(k,"--score")) { if (quick) goto invalid; score=1; continue; }
        if (!strcmp(k,"--score-quick")) { if (score) goto invalid; score=1; quick=1; continue; }
        if (!strcmp(k,"--json") || !strcmp(k,"--csv")) {
            if (c->format) goto invalid;
            c->format = !strcmp(k,"--json") ? 1 : 2; continue;
        }
        if (!strcmp(k,"--sweep")) { c->sweep=1; profile_override=1; continue; }
        if (!strcmp(k,"--thread-sweep")) { c->thread_sweep=1; profile_override=1; continue; }
        if (++i == argc) { fprintf(stderr,"Missing value for %s\n",k); return -1; }
        v=argv[i];
        if (!strcmp(k,"--test")) {
            int found=0;
            for (int t=SEQ_READ;t<=ALL;++t) if (!strcmp(v,mm_test_name((mm_test)t))) { c->test=(mm_test)t; found=1; }
            if (!found) goto invalid;
            test_seen=1;
        } else if (!strcmp(k,"--size")) { if (mm_size(v,&c->size)) goto invalid; profile_override=1;
        } else if (!strcmp(k,"--stride")) { if (mm_size(v,&c->stride)) goto invalid; profile_override=1;
        } else if (!strcmp(k,"--min-size")) { if (mm_size(v,&c->min_size)) goto invalid; c->sweep=1; profile_override=1;
        } else if (!strcmp(k,"--max-size")) { if (mm_size(v,&c->max_size)) goto invalid; c->sweep=1; profile_override=1;
        } else if (!strcmp(k,"--step")) { if (mm_size(v,&c->step)) goto invalid; c->sweep=1; profile_override=1;
        } else if (!strcmp(k,"--memory-limit")) { if (mm_size(v,&c->memory_limit)) goto invalid;
        } else if (!strcmp(k,"--accesses")) { if (integer(v,&n) || !n || n>SIZE_MAX/sizeof(size_t)) goto invalid; c->accesses=(size_t)n; profile_override=1;
        } else if (!strcmp(k,"--threads")) { if (integer(v,&n) || !n || n>MM_MAX_THREADS) goto invalid; c->threads=(unsigned)n; profile_override=1;
        } else if (!strcmp(k,"--repeat")) { if (integer(v,&n) || !n || n>MM_MAX_REPEAT) goto invalid; c->repeat=(unsigned)n; profile_override=1;
        } else if (!strcmp(k,"--iterations")) { if (integer(v,&c->iterations) || !c->iterations) goto invalid; profile_override=1;
        } else if (!strcmp(k,"--seed")) { if (integer(v,&c->seed)) goto invalid; profile_override=1;
        } else if (!strcmp(k,"--read-ratio")) { if (integer(v,&n) || n>100) goto invalid; c->read_ratio=(unsigned)n; profile_override=1;
        } else if (!strcmp(k,"--duration")) { if (real_value(v,&c->duration) || c->duration<.001 || c->duration>3600) goto invalid; profile_override=1;
        } else if (!strcmp(k,"--warmup")) { if (real_value(v,&c->warmup) || c->warmup<0 || c->warmup>3600) goto invalid; profile_override=1;
        } else if (!strcmp(k,"--scale")) { if (real_value(v,&c->scale) || c->scale<=1 || c->scale>1024) goto invalid; c->sweep=1; profile_override=1;
        } else if (!strcmp(k,"--cpu-affinity")) {
            const char *p=v;
            c->ncpus=0;
            if (!strcmp(v,"none")) continue;
            while (*p) {
                char *end;
                unsigned long cpu;
                if (*p<'0' || *p>'9' || c->ncpus==MM_MAX_THREADS) goto invalid;
                errno=0; cpu=strtoul(p,&end,10);
                if (errno || cpu>INT_MAX || (*end && *end!=',')) goto invalid;
                c->cpus[c->ncpus++]=(int)cpu;
                if (!*end) break;
                p=end+1;
                if (!*p) goto invalid;
            }
            if (!c->ncpus) goto invalid;
        } else { fprintf(stderr,"Unknown option: %s\n",k); return -1; }
        continue;
invalid:
        fprintf(stderr,"Invalid value or conflicting option: %s\n",k); return -1;
    }
    if (score && (test_seen || profile_override)) {
        fprintf(stderr,"--score uses a fixed profile; do not combine it with --test or workload tuning options\n"); return -1;
    }
    if (score && c->ncpus>1) {
        fprintf(stderr,"--score accepts at most one CPU affinity because its fixed profile uses one worker\n"); return -1;
    }
    if (score && c->memory_limit < (size_t)256*1024*1024) {
        fprintf(stderr,"--score requires a memory limit of at least 256M\n"); return -1;
    }
    c->score=score;
    c->quick=quick;
    size_t smallest=c->sweep ? c->min_size : c->size;
    if (c->size%8 || c->min_size%8 || c->max_size%8 || c->step%8 ||
        c->stride%8 || c->stride>smallest || c->min_size>c->max_size ||
        (!score && c->ncpus && (unsigned)c->ncpus<c->threads)) {
        fprintf(stderr,"Sizes/stride must be multiples of 8; stride <= smallest size; min <= max; CPU list must cover all workers\n"); return -1;
    }
    return 0;
}
