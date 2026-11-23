#pragma once
#include <stdint.h>
int apk_open(const char *path);
int apk_find(const char *name, uint32_t *off, uint32_t *size);
int apk_find_raw(const char *resname, uint32_t *off, uint32_t *size);
void *apk_read(uint32_t off, uint32_t size);
