#include "mm.h"
#include <stdlib.h>
#include <time.h>
double mm_now(void) {
#ifdef _WIN32
    LARGE_INTEGER n, f;
    if (!QueryPerformanceFrequency(&f) || !QueryPerformanceCounter(&n)) abort();
    return (double)n.QuadPart / (double)f.QuadPart;
#else
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) { perror("clock_gettime"); abort(); }
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
#endif
}
