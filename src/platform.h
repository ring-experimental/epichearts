/* Small OS abstraction so the host builds for Windows (MSVC x86) and Android (NDK, armeabi-v7a / x86) */
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#define host_strdup _strdup
#else
#include <string.h>
#define host_strdup strdup
#endif

/* reserve+commit read/write memory, preferably at hint */
void *host_valloc(uintptr_t hint, size_t size);
int host_mkdir(const char *path);
uint64_t host_ticks_ms(void);
