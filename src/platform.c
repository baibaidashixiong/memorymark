#include "mm.h"
#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#include <sys/utsname.h>
#endif
#ifdef __linux__
#include <dirent.h>
#include <sched.h>
#endif
const char *mm_os(void) {
#ifdef _WIN32
    return "Windows";
#elif defined(__linux__)
    return "Linux";
#else
    return "POSIX";
#endif
}
const char *mm_arch(void) {
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "aarch64";
#else
    return "other";
#endif
}
const char *mm_compiler(void) {
#ifdef __clang__
    return "Clang " __clang_version__;
#elif defined(__GNUC__)
    return "GCC " __VERSION__;
#elif defined(_MSC_VER)
    return "MSVC";
#else
    return "unknown";
#endif
}
static void copy_info(char *dst, size_t cap, const char *src) {
    size_t n;
    if (!cap) return;
    if (!src || !*src) src="unknown";
    n=strlen(src);
    if (n>=cap) n=cap-1;
    memcpy(dst,src,n);
    dst[n]='\0';
}
#ifndef _WIN32
static size_t cache_size(int name) {
    long value=sysconf(name);
    return value>0 ? (size_t)value : 0;
}
#endif
#ifdef __linux__
static void linux_cpu_model(char *dst, size_t cap) {
    static const char *keys[]={"model name","Hardware","Processor","cpu model"};
    FILE *f=fopen("/proc/cpuinfo","r");
    if (!f) return;
    char line[512];
    while (fgets(line,sizeof(line),f)) {
        char *key_line=line;
        while (*key_line && isspace((unsigned char)*key_line)) ++key_line;
        for (size_t i=0;i<sizeof(keys)/sizeof(keys[0]);++i) {
            size_t n=strlen(keys[i]);
            char *colon=key_line+n;
            if (strncmp(key_line,keys[i],n)) continue;
            while (*colon && isspace((unsigned char)*colon)) ++colon;
            if (*colon==':') {
                char *value=colon+1;
                while (*value && isspace((unsigned char)*value)) ++value;
                char *end=value+strlen(value);
                while (end>value && isspace((unsigned char)end[-1])) --end;
                *end='\0';
                if (*value) copy_info(dst,cap,value);
                fclose(f);
                return;
            }
        }
    }
    fclose(f);
}
static unsigned linux_allowed_cpus(void) {
    cpu_set_t set;
    CPU_ZERO(&set);
    if (sched_getaffinity(0,sizeof(set),&set)==0) return (unsigned)CPU_COUNT(&set);
    return 0;
}
static unsigned linux_numa_nodes(void) {
    DIR *dir=opendir("/sys/devices/system/node");
    unsigned count=0;
    if (!dir) return 0;
    struct dirent *entry;
    while ((entry=readdir(dir)) != NULL) {
        if (!strncmp(entry->d_name,"node",4) && isdigit((unsigned char)entry->d_name[4])) ++count;
    }
    closedir(dir);
    return count;
}
#endif
void mm_collect_system_info(mm_system_info *info) {
    uint16_t endian_probe=1;
    memset(info,0,sizeof(*info));
    copy_info(info->cpu_model,sizeof(info->cpu_model),"unknown");
    copy_info(info->kernel,sizeof(info->kernel),"unknown");
    copy_info(info->machine,sizeof(info->machine),mm_arch());
    copy_info(info->endianness,sizeof(info->endianness),*(unsigned char *)&endian_probe ? "little" : "big");
    info->pointer_bits=(unsigned)(sizeof(void*)*CHAR_BIT);
#ifdef _WIN32
    SYSTEM_INFO si;
    char model[sizeof(info->cpu_model)];
    GetNativeSystemInfo(&si);
    info->page_size=si.dwPageSize;
    info->allowed_cpus=si.dwNumberOfProcessors;
    if (GetEnvironmentVariableA("PROCESSOR_IDENTIFIER",model,sizeof(model))) copy_info(info->cpu_model,sizeof(info->cpu_model),model);
    {
        ULONG highest=0;
        if (GetNumaHighestNodeNumber(&highest) && highest<UINT_MAX) info->numa_nodes=(unsigned)highest+1;
    }
#else
    struct utsname u;
    if (uname(&u)==0) {
        copy_info(info->kernel,sizeof(info->kernel),u.release);
        copy_info(info->machine,sizeof(info->machine),u.machine);
    }
    if (sysconf(_SC_PAGESIZE)>0) info->page_size=(size_t)sysconf(_SC_PAGESIZE);
    if (sysconf(_SC_NPROCESSORS_ONLN)>0) info->allowed_cpus=(unsigned)sysconf(_SC_NPROCESSORS_ONLN);
#ifdef _SC_LEVEL1_DCACHE_SIZE
    info->l1d_cache=cache_size(_SC_LEVEL1_DCACHE_SIZE);
#endif
#ifdef _SC_LEVEL2_CACHE_SIZE
    info->l2_cache=cache_size(_SC_LEVEL2_CACHE_SIZE);
#endif
#ifdef _SC_LEVEL3_CACHE_SIZE
    info->l3_cache=cache_size(_SC_LEVEL3_CACHE_SIZE);
#endif
#ifdef __linux__
    {
        unsigned allowed=linux_allowed_cpus();
        if (allowed) info->allowed_cpus=allowed;
        info->numa_nodes=linux_numa_nodes();
        linux_cpu_model(info->cpu_model,sizeof(info->cpu_model));
    }
#endif
#endif
}
void *mm_alloc(size_t bytes) {
#ifdef _WIN32
    return _aligned_malloc(bytes, 64);
#else
    void *p = NULL;
    if (posix_memalign(&p, 64, bytes)) return NULL;
    return p;
#endif
}
void mm_free(void *p) {
#ifdef _WIN32
    _aligned_free(p);
#else
    free(p);
#endif
}
int mm_bind(int cpu) {
    if (cpu < 0) return 0;
#ifdef __linux__
    cpu_set_t set;
    if (cpu >= CPU_SETSIZE) return -1;
    CPU_ZERO(&set);
    CPU_SET((unsigned)cpu, &set);
    return pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
#elif defined(_WIN32)
    if (cpu >= (int)(sizeof(DWORD_PTR) * 8)) return -1;
    return SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1 << cpu) ? 0 : -1;
#else
    return -1;
#endif
}
void mm_store_fence(void) {
    /* Orders cached stores; this does not flush dirty lines to DRAM. */
    atomic_thread_fence(memory_order_seq_cst);
}
#ifdef _WIN32
typedef struct { void *(*fn)(void *); void *arg; } launch;
static DWORD WINAPI trampoline(LPVOID p) {
    launch x = *(launch *)p;
    free(p);
    x.fn(x.arg);
    return 0;
}
int mm_thread_start(mm_thread *t, void *(*fn)(void *), void *arg) {
    launch *x = malloc(sizeof(*x));
    if (!x) return -1;
    x->fn = fn; x->arg = arg;
    *t = CreateThread(NULL, 0, trampoline, x, 0, NULL);
    if (!*t) { free(x); return -1; }
    return 0;
}
int mm_thread_join(mm_thread t) {
    int rc = WaitForSingleObject(t, INFINITE) == WAIT_OBJECT_0 ? 0 : -1;
    if (!CloseHandle(t)) rc = -1;
    return rc;
}
int mm_mutex_init(mm_mutex *m) { InitializeCriticalSection(m); return 0; }
int mm_cond_init(mm_cond *c) { InitializeConditionVariable(c); return 0; }
void mm_lock(mm_mutex *m) { EnterCriticalSection(m); }
void mm_unlock(mm_mutex *m) { LeaveCriticalSection(m); }
void mm_wait(mm_cond *c, mm_mutex *m) { if (!SleepConditionVariableCS(c,m,INFINITE)) abort(); }
void mm_broadcast(mm_cond *c) { WakeAllConditionVariable(c); }
void mm_mutex_destroy(mm_mutex *m) { DeleteCriticalSection(m); }
void mm_cond_destroy(mm_cond *c) { (void)c; }
#else
int mm_thread_start(mm_thread *t, void *(*fn)(void *), void *arg) { return pthread_create(t,NULL,fn,arg); }
int mm_thread_join(mm_thread t) { return pthread_join(t,NULL); }
int mm_mutex_init(mm_mutex *m) { return pthread_mutex_init(m,NULL); }
int mm_cond_init(mm_cond *c) { return pthread_cond_init(c,NULL); }
void mm_lock(mm_mutex *m) { if (pthread_mutex_lock(m)) abort(); }
void mm_unlock(mm_mutex *m) { if (pthread_mutex_unlock(m)) abort(); }
void mm_wait(mm_cond *c, mm_mutex *m) { if (pthread_cond_wait(c,m)) abort(); }
void mm_broadcast(mm_cond *c) { if (pthread_cond_broadcast(c)) abort(); }
void mm_mutex_destroy(mm_mutex *m) { if (pthread_mutex_destroy(m)) abort(); }
void mm_cond_destroy(mm_cond *c) { if (pthread_cond_destroy(c)) abort(); }
#endif
