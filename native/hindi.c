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

/* ★★`लेकिन`（しかし）は原典の `BUT` にあたる —— 原典は BUT を節の切れ目にしている
   ので、ヒンディー語でも同じ働きをさせる（`,` に写す）。写さないと後半の語が勝って
   前半の告白が素通りする。★`पर` は後置詞と同形なので入れない。
   ★tools/hindi_in.py の BUT と対。片方だけ増やさないこと。 */
static int is_but(const uint16_t *w, int n)
{
    static const uint16_t LEKIN[] = { 0x0932, 0x0947, 0x0915, 0x093F, 0x0928 };   /* लेकिन */
    static const uint16_t MAGAR[] = { 0x092E, 0x0917, 0x0930 };                   /* मगर */
    if (n == 5) {
        for (int i = 0; i < 5; i++)
            if (w[i] != LEKIN[i])
                return 0;
        return 1;
    }
    if (n == 3) {
        for (int i = 0; i < 3; i++)
            if (w[i] != MAGAR[i])
                return 0;
        return 1;
    }
    return 0;
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
    /* ★語ごとに見て `लेकिन` / `मगर` を `,` に置き換える（語として独立しているときだけ） */
    int w = 0;
    for (int i = 0; i <= o; i++) {
        if (i < o && buf[i] != ' ')
            continue;
        if (is_but(buf + w, i - w)) {
            buf[w] = ',';
            /* 後ろを 1 字ぶんへ詰める */
            const int drop = (i - w) - 1;
            for (int k = w + 1; k + drop < o; k++)
                buf[k] = buf[k + drop];
            o -= drop;
            i -= drop;
        }
        w = i + 1;
    }
    return u16_to_u8(buf, o, dst, max);
}
