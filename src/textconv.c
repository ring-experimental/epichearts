#include "textconv.h"
#include "cp949_table.h"

unsigned short cp949_decode2(unsigned char lead, unsigned char trail) {
    if (lead < 0x81 || lead > 0xfe || trail < 0x41 || trail > 0xfe) return 0;
    return cp949_tab[lead - 0x81][trail - 0x41];
}

size_t cp949_to_utf8(const char *in, char *out, size_t outsz) {
    const unsigned char *s = (const unsigned char *)in;
    size_t o = 0;
    if (!outsz) return 0;
    while (*s) {
        unsigned cp;
        if (*s < 0x80) cp = *s++;
        else if (s[1]) { cp = cp949_decode2(s[0], s[1]); s += 2; if (!cp) cp = 0xfffd; }
        else break;
        char b[4]; int n;
        if (cp < 0x80) { b[0] = (char)cp; n = 1; }
        else if (cp < 0x800) { b[0] = (char)(0xc0 | cp >> 6); b[1] = (char)(0x80 | (cp & 0x3f)); n = 2; }
        else { b[0] = (char)(0xe0 | cp >> 12); b[1] = (char)(0x80 | ((cp >> 6) & 0x3f)); b[2] = (char)(0x80 | (cp & 0x3f)); n = 3; }
        if (o + n >= outsz) break;
        for (int i = 0; i < n; i++) out[o++] = b[i];
    }
    out[o] = 0;
    return o;
}
