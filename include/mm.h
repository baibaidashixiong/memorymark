#ifndef MM_H
#define MM_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
static inline int mm_isfinite(double value) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_isfinite(value);
#else
    return isfinite(value);
#endif
}
#ifdef _WIN32
#include <windows.h>
typedef HANDLE mm_thread;
typedef CRITICAL_SECTION mm_mutex;
typedef CONDITION_VARIABLE mm_cond;
#else
#include <pthread.h>
typedef pthread_t mm_thread;
typedef pthread_mutex_t mm_mutex;
typedef pthread_cond_t mm_cond;
#endif
#define MM_MAX_THREADS 256
#define MM_MAX_REPEAT 100
#define MM_SCORE_COMPONENTS 6
typedef enum { SEQ_READ, SEQ_WRITE, MIX, RANDOM_READ, RANDOM_WRITE, LATENCY, ALL } mm_test;
typedef struct {
    mm_test test;
    size_t size, stride, min_size, max_size, step, accesses, memory_limit;
    unsigned threads, repeat, read_ratio;
    uint64_t iterations, seed;
    double duration, warmup, scale;
    int format, sweep, thread_sweep, score, quick, cpus[MM_MAX_THREADS], ncpus;
} mm_config;
typedef struct {
    uint64_t *data;
    size_t *indices;
    size_t slots, count, stride_words, index_count;
    uint64_t checksum, sequence_hash, calls;
    size_t cursor;
} mm_memory;
typedef struct { double min, median, mean, stddev; } mm_stats;
typedef struct {
    double seconds, gbps, ns_access;
    uint64_t accesses, bytes, iterations, checksum;
} mm_sample;
typedef struct {
    mm_sample samples[MM_MAX_REPEAT];
    mm_sample *workers; /* repeat * threads; owned by caller */
    mm_stats bandwidth, latency, elapsed;
    uint64_t sequence_hash;
    double efficiency;
} mm_result;
typedef struct {
    mm_test test;
    double gbps, ns_access, million_ops, cv;
    uint64_t sequence_hash;
} mm_score_component;
typedef struct {
    char cpu_model[256];
    char kernel[128];
    char machine[64];
    char endianness[16];
    unsigned pointer_bits, allowed_cpus, numa_nodes;
    size_t page_size, l1d_cache, l2_cache, l3_cache;
} mm_system_info;
typedef struct {
    mm_score_component components[MM_SCORE_COMPONENTS];
    size_t size, memory_limit, accesses;
    unsigned threads, repeat, read_ratio;
    uint64_t seed, sequence_hash;
    double duration, warmup, score, max_cv, elapsed;
    size_t latency_stride;
    int stable, quick;
    const char *cpu;
    mm_system_info system;
} mm_score_result;
void mm_defaults(mm_config *c);
int mm_parse(int argc, char **argv, mm_config *c);
int mm_size(const char *s, size_t *value);
const char *mm_test_name(mm_test t);
void mm_help(void);
double mm_now(void);
const char *mm_os(void);
const char *mm_arch(void);
const char *mm_compiler(void);
void mm_collect_system_info(mm_system_info *info);
void *mm_alloc(size_t bytes);
void mm_free(void *ptr);
int mm_bind(int cpu);
void mm_store_fence(void);
int mm_thread_start(mm_thread *t, void *(*fn)(void *), void *arg);
int mm_thread_join(mm_thread t);
int mm_mutex_init(mm_mutex *m);
int mm_cond_init(mm_cond *c);
void mm_lock(mm_mutex *m);
void mm_unlock(mm_mutex *m);
void mm_wait(mm_cond *c, mm_mutex *m);
void mm_broadcast(mm_cond *c);
void mm_mutex_destroy(mm_mutex *m);
void mm_cond_destroy(mm_cond *c);
uint64_t mm_rng(uint64_t *state);
uint64_t mm_bounded(uint64_t *state, uint64_t bound);
int mm_memory_init(mm_memory *m, const mm_config *c, unsigned worker);
/* init owns data/indices on success; destroy also accepts a zeroed object. */
void mm_memory_destroy(mm_memory *m);
int mm_ring_valid(const mm_memory *m);
void mm_seq(mm_memory *m, const mm_config *c);
void mm_random(mm_memory *m, const mm_config *c);
void mm_latency(mm_memory *m, const mm_config *c);
void mm_kernel(mm_memory *m, const mm_config *c);
/* On success caller frees r->workers; all worker resources are already joined/freed.
 * Total seconds spans the common gate release through the last timed worker end.
 * Allocation, warmup, verification and join are outside that interval. */
int mm_run(const mm_config *c, mm_result *r);
double mm_score_from_rates(const double *rates, unsigned n);
int mm_run_score(const mm_config *c, mm_score_result *r);
void mm_score_output(const mm_config *c, const mm_score_result *r);
mm_stats mm_statistics(const double *v, unsigned n);
void mm_output_begin(const mm_config *c);
void mm_output(const mm_config *c, const mm_result *r, int first);
void mm_output_end(const mm_config *c);
#endif
