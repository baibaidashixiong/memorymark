#include "mm.h"
#include <stdlib.h>
#include <stdatomic.h>
#ifdef __linux__
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
