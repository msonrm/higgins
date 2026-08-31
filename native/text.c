#include "text.h"

enum { REPL = 0xFFFD };

int u8_to_u16(const char *src, int slen, uint16_t *dst, int max)
{
    if (slen < 0) {
        slen = 0;
        while (src[slen])
            slen++;
    }
    int n = 0;
    for (int i = 0; i < slen && n < max;) {
        const unsigned char c = (unsigned char)src[i];
        int need, cp;
        if (c < 0x80) { need = 0; cp = c; }
        else if ((c & 0xE0) == 0xC0) { need = 1; cp = c & 0x1F; }
        else if ((c & 0xF0) == 0xE0) { need = 2; cp = c & 0x0F; }
        else if ((c & 0xF8) == 0xF0) { need = 3; cp = c & 0x07; }
        else { dst[n++] = REPL; i++; continue; }

        if (i + need >= slen) {              /* 続きのバイトが足りない（切れている） */
            dst[n++] = REPL;
            break;
        }
        int ok = 1;
        for (int k = 1; k <= need; k++) {
            const unsigned char cc = (unsigned char)src[i + k];
            if ((cc & 0xC0) != 0x80) { ok = 0; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (!ok) { dst[n++] = REPL; i++; continue; }
        i += need + 1;
        /* ★BMP の外とサロゲート単体は U+FFFD。上の header に理由。 */
        dst[n++] = (cp > 0xFFFF || (cp >= 0xD800 && cp <= 0xDFFF)) ? REPL : (uint16_t)cp;
    }
    return n;
}

int u16_to_u8(const uint16_t *src, int slen, char *dst, int max)
{
    int o = 0;
    for (int i = 0; i < slen; i++) {
        unsigned cp = src[i];
        if (cp >= 0xD800 && cp <= 0xDFFF)
            cp = REPL;
        const int need = cp < 0x80 ? 1 : cp < 0x800 ? 2 : 3;
        if (o + need >= max)               /* 終端の 1 バイトを必ず残す */
            break;
        if (need == 1) {
            dst[o++] = (char)cp;
        } else if (need == 2) {
            dst[o++] = (char)(0xC0 | (cp >> 6));
            dst[o++] = (char)(0x80 | (cp & 0x3F));
        } else {
            dst[o++] = (char)(0xE0 | (cp >> 12));
            dst[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            dst[o++] = (char)(0x80 | (cp & 0x3F));
        }
    }
    if (max > 0)
        dst[o] = 0;
    return o;
}
