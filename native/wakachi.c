#include "wakachi.h"
#include "text.h"
#include "vocab.h"

/* ★句読点は半角の , . に写す。ELIZA は "," "." "BUT" を delimiter として節を切り、
   **キーワードのある節だけ**を使う（eliza.cpp のコンストラクタでハードコードされて
   いて、スクリプトからは変えられない）。日本語の句読点をここで写せば
   ★**エンジンを 1 行も変えずにその仕組みへ乗る** —— 引用が節単位になって短くなる、
   という副次効果もある（設計原則 2「機械の持ち分は短く」に効く）。
   ★対応は tools/wakachi.py の PUNCT と同じ 8 件。片方だけ増やさないこと。 */
static uint16_t punct_of(uint16_t c)
{
    switch (c) {
    case 0x3001: case 0xFF0C: return ',';        /* 、 ， */
    case 0x3002: case 0xFF0E:                    /* 。 ． */
    case 0xFF01: case 0xFF1F:                    /* ！ ？ */
    case '!':    case '?':    return '.';
    default:                  return 0;
    }
}

/* ★Python の str.isspace() に合わせる（全角空白も区切りとして尊重する）。 */
static int is_space(uint16_t c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == 0x3000;
}

/* 前方一致する最長の語の長さ。0 = 辞書に無い。 */
static int longest_match(const uint16_t *s, int n)
{
    int best = 0;
    if (n > VOCAB_MAXLEN)
        n = VOCAB_MAXLEN;
    for (int k = 0; k < VOCAB_N; k++) {
        const int len = VOCAB[k].len;
        if (len <= best || len > n)
            continue;
        const unsigned short *w = &VOCAB_POOL[VOCAB[k].off];
        int ok = 1;
        for (int t = 0; t < len && ok; t++)
            ok = s[t] == w[t];
        if (ok)
            best = len;
    }
    return best;
}

/* 1 トークンを書き出す（2 つ目からは前に空白を置く） */
static void put(char *dst, int max, int *o, int *first, const uint16_t *s, int n)
{
    if (!*first && *o + 1 < max)
        dst[(*o)++] = ' ';
    *first = 0;
    *o += u16_to_u8(s, n, dst + *o, max - *o);
}

int wakachi(const uint16_t *src, int slen, char *dst, int max)
{
    int o = 0, first = 1, unk = -1;             /* unk = 未知の連続の開始位置 */
    if (max > 0)
        dst[0] = 0;

    for (int i = 0; i < slen;) {
        const uint16_t p = punct_of(src[i]);
        if (p || is_space(src[i])) {
            if (unk >= 0) { put(dst, max, &o, &first, src + unk, i - unk); unk = -1; }
            if (p)
                put(dst, max, &o, &first, &p, 1);
            i++;
            continue;
        }
        const int hit = longest_match(src + i, slen - i);
        if (hit) {
            if (unk >= 0) { put(dst, max, &o, &first, src + unk, i - unk); unk = -1; }
            put(dst, max, &o, &first, src + i, hit);
            i += hit;
        } else {
            /* ★辞書に無い連続は 1 塊で残す —— 埋め戻し（スロット）に打った字面が
               そのまま入るのは、ここで切らないから。 */
            if (unk < 0)
                unk = i;
            i++;
        }
    }
    if (unk >= 0)
        put(dst, max, &o, &first, src + unk, slen - unk);
    return o;
}
