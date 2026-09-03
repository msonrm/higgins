#include "hindi.h"
#include "text.h"

enum { HI_BUF = 4096, NUKTA = 0x093C };

/* 合成形の nukta 字（U+0958..U+095F）→ 素の字。0 = その字ではない。
   ★並びは Unicode のまま（क़ ख़ ग़ ज़ ड़ ढ़ फ़ य़）。tools/hindi_in.py の NUKTA と対。 */
static uint16_t nukta_base(uint16_t c)
{
    static const uint16_t BASE[8] = { 0x0915, 0x0916, 0x0917, 0x091C,
                                      0x0921, 0x0922, 0x092B, 0x092F };
    return (c >= 0x0958 && c <= 0x095F) ? BASE[c - 0x0958] : 0;
}

int hindi_in(const uint16_t *src, int slen, char *dst, int max)
{
    uint16_t buf[HI_BUF];
    int n = 0;
    for (int i = 0; i < slen && n + 2 < HI_BUF; i++) {
        const uint16_t base = nukta_base(src[i]);
        if (src[i] == 0x0964 || src[i] == 0x0965) {   /* ।  ॥ → . */
            buf[n++] = '.';
        } else if (base) {
            buf[n++] = base;
            buf[n++] = NUKTA;
        } else {
            buf[n++] = src[i];
        }
    }
    /* 空白を 1 つに詰める（前後も落とす）*/
    int o = 0;
    for (int i = 0; i < n; i++) {
        if (buf[i] != ' ') {
            buf[o++] = buf[i];
            continue;
        }
        if (o > 0 && buf[o - 1] != ' ')
            buf[o++] = ' ';
    }
    while (o > 0 && buf[o - 1] == ' ')
        o--;
    return u16_to_u8(buf, o, dst, max);
}
