/* CP949 (the game's string encoding, GofStringUtil charset 1) <-> Unicode */
#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* one double-byte CP949 character -> UTF-16 unit, 0 if invalid */
unsigned short cp949_decode2(unsigned char lead, unsigned char trail);
/* NUL-terminated CP949 -> UTF-8 (truncated to outsz); returns bytes written */
size_t cp949_to_utf8(const char *in, char *out, size_t outsz);
#ifdef __cplusplus
}
#endif
