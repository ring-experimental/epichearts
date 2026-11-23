#include "platform.h"
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
void *host_valloc(uintptr_t hint, size_t size) {
    void *p = hint ? VirtualAlloc((void *)hint, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE) : NULL;
    if (!p) p = VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    return p;
}
int host_mkdir(const char *path) { return _mkdir(path); }
uint64_t host_ticks_ms(void) { return GetTickCount64(); }
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
void *host_valloc(uintptr_t hint, size_t size) {
    void *p = mmap((void *)hint, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? NULL : p;
}
int host_mkdir(const char *path) { return mkdir(path, 0755); }
uint64_t host_ticks_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)(ts.tv_nsec / 1000000);
}
#endif
