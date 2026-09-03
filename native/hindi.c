#include "hindi.h"
#include "text.h"

#include <stddef.h>

enum { HI_BUF = 4096, NUKTA = 0x093C };

/* 合成形の nukta 字（U+0958..U+095F）→ 素の字。0 = その字ではない。
   ★並びは Unicode のまま（क़ ख़ ग़ ज़ ड़ ढ़ फ़ य़）。tools/hindi_in.py の NUKTA と対。 */
static uint16_t nukta_base(uint16_t c)
{
    static const uint16_t BASE[8] = { 0x0915, 0x0916, 0x0917, 0x091C,
                                      0x0921, 0x0922, 0x092B, 0x092F };
    return (c >= 0x0958 && c <= 0x095F) ? BASE[c - 0x0958] : 0;
}

/* ★★語まるごとの書き換え。tools/hindi_in.py の WORD と対。**片方だけ増やさないこと。**
   1. `लेकिन` / `मगर` は原典の `BUT` にあたるので `,`（節の切れ目）へ写す。
      ★`पर` は後置詞「〜の上に」と同形なので入れない。
   2. 親称（`तुम` / `तू`）は敬称（`आप`）へ寄せる。★逃げの正規化ではなく**正しい** ——
      カウンセラーは相手がくだけても敬体を崩さない。**DOCTOR は常に `आप` で話す。** */
typedef struct { const uint16_t *from; int flen; const uint16_t *to; int tlen; } WordSub;

#define W(name, ...) static const uint16_t name[] = { __VA_ARGS__ }
W(W_LEKIN, 0x0932,0x0947,0x0915,0x093F,0x0928);          /* लेकिन */
W(W_MAGAR, 0x092E,0x0917,0x0930);                        /* मगर */
W(W_COMMA, ',');
W(W_TUM,   0x0924,0x0941,0x092E);                        /* तुम */
W(W_TU,    0x0924,0x0942);                               /* तू */
W(W_AAP,   0x0906,0x092A);                               /* आप */
W(W_TUMHE, 0x0924,0x0941,0x092E,0x094D,0x0939,0x0947,0x0902);  /* तुम्हें */
W(W_TUMKO, 0x0924,0x0941,0x092E,0x0915,0x094B);          /* तुमको */
W(W_TUJHE, 0x0924,0x0941,0x091D,0x0947);                 /* तुझे */
W(W_AAPKO, 0x0906,0x092A,0x0915,0x094B);                 /* आपको */
W(W_TUMSE, 0x0924,0x0941,0x092E,0x0938,0x0947);          /* तुमसे */
W(W_TUJHSE,0x0924,0x0941,0x091D,0x0938,0x0947);          /* तुझसे */
W(W_AAPSE, 0x0906,0x092A,0x0938,0x0947);                 /* आपसे */
W(W_TUMHARA,0x0924,0x0941,0x092E,0x094D,0x0939,0x093E,0x0930,0x093E);  /* तुम्हारा */
W(W_TUMHARI,0x0924,0x0941,0x092E,0x094D,0x0939,0x093E,0x0930,0x0940);  /* तुम्हारी */
W(W_TUMHARE,0x0924,0x0941,0x092E,0x094D,0x0939,0x093E,0x0930,0x0947);  /* तुम्हारे */
W(W_AAPKA, 0x0906,0x092A,0x0915,0x093E);                 /* आपका */
W(W_AAPKI, 0x0906,0x092A,0x0915,0x0940);                 /* आपकी */
W(W_AAPKE, 0x0906,0x092A,0x0915,0x0947);                 /* आपके */
W(W_TUMNE, 0x0924,0x0941,0x092E,0x0928,0x0947);          /* तुमने */
W(W_TUNE,  0x0924,0x0942,0x0928,0x0947);                 /* तूने */
W(W_AAPNE, 0x0906,0x092A,0x0928,0x0947);                 /* आपने */
#undef W
#define SUB(f, t) { f, (int)(sizeof f / sizeof *f), t, (int)(sizeof t / sizeof *t) }
static const WordSub WORD_SUB[] = {
    SUB(W_LEKIN, W_COMMA), SUB(W_MAGAR, W_COMMA),
    SUB(W_TUM, W_AAP), SUB(W_TU, W_AAP),
    SUB(W_TUMHE, W_AAPKO), SUB(W_TUMKO, W_AAPKO), SUB(W_TUJHE, W_AAPKO),
    SUB(W_TUMSE, W_AAPSE), SUB(W_TUJHSE, W_AAPSE),
    SUB(W_TUMHARA, W_AAPKA), SUB(W_TUMHARI, W_AAPKI), SUB(W_TUMHARE, W_AAPKE),
    SUB(W_TUMNE, W_AAPNE), SUB(W_TUNE, W_AAPNE),
};
#undef SUB
enum { WORD_SUB_N = (int)(sizeof WORD_SUB / sizeof *WORD_SUB) };

/* 語 [w, e) に当たる書き換えを探す。無ければ NULL。 */
static const WordSub *word_sub(const uint16_t *w, int n)
{
    for (int k = 0; k < WORD_SUB_N; k++) {
        if (WORD_SUB[k].flen != n)
            continue;
        int i = 0;
        while (i < n && w[i] == WORD_SUB[k].from[i])
            i++;
        if (i == n)
            return &WORD_SUB[k];
    }
    return NULL;
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
    /* ★語ごとに書き換える（語として独立しているときだけ。語中は触らない）。
       ★別の配列へ組み直す —— 置換で長さが変わるので、その場で詰めるより素直。 */
    uint16_t out[HI_BUF];
    int m = 0, w = 0;
    for (int i = 0; i <= o; i++) {
        if (i < o && buf[i] != ' ')
            continue;
        const WordSub *sub = word_sub(buf + w, i - w);
        const uint16_t *src2 = sub ? sub->to : buf + w;
        const int len = sub ? sub->tlen : i - w;
        for (int k = 0; k < len && m < HI_BUF; k++)
            out[m++] = src2[k];
        if (i < o && m < HI_BUF)
            out[m++] = ' ';
        w = i + 1;
    }
    return u16_to_u8(out, m, dst, max);
}
