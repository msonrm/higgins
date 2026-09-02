#include "josa.h"
#include "text.h"
#include "vocab_ko.h"
#include "josa_tables.h"

enum { TOK_MAX = 12 };
static const uint16_t MARK = 0x00B7;      /* · —— 「元は 1 語だった」印 */

/* ★句読点は半角の , . に写す。ELIZA は "," "." "BUT" を delimiter として節を切る
   （eliza.cpp に焼かれていてスクリプトからは変えられない）。
   ★**韓国語は ASCII の句読点を打つ**ので `.` `,` もここに入る —— 日本語版の
   punct_of には無い。対応は tools/josa.py の PUNCT_KO と同じ。 */
static uint16_t punct_of(uint16_t c)
{
    switch (c) {
    case 0x3001: case 0xFF0C: case ',': return ',';        /* 、 ， , */
    case 0x3002: case 0xFF0E:                              /* 。 ． */
    case 0xFF01: case 0xFF1F:                              /* ！ ？ */
    case '!':    case '?':    case '.': return '.';
    default:                  return 0;
    }
}

static int is_space(uint16_t c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == 0x3000;
}

typedef struct { const uint16_t *p; int n; } Tok;

static int in_vocab(const uint16_t *s, int n)
{
    for (int k = 0; k < VOCAB_KO_N; k++) {
        if (VOCAB_KO[k].len != n)
            continue;
        const unsigned short *w = &VOCAB_KO_POOL[VOCAB_KO[k].off];
        int ok = 1;
        for (int t = 0; t < n && ok; t++)
            ok = s[t] == w[t];
        if (ok)
            return 1;
    }
    return 0;
}

/* 塊の**先頭で 1 回だけ**辞書を引く。戻り値 = 見出しの長さ（0 = 無し）。
   ★1 音節の語は塊を使い切るときだけ認める —— これが無いと「요즘」が
     「요」＋「즘」に割れ、受け皿が語頭で誤爆する（tools/josa.py の注記）。 */
static int head_of(const uint16_t *run, int n)
{
    int max = n < VOCAB_KO_MAXLEN ? n : VOCAB_KO_MAXLEN;
    for (int len = max; len >= 1; len--) {
        if (!in_vocab(run, len))
            continue;
        if (len == 1 && len != n)
            continue;
        return len;
    }
    return 0;
}

/* s[0..n) の末尾が KO_TAIL のどれかなら、その添字。★長い順に見る。 */
static int tail_at_end(const uint16_t *s, int n, int need_shorter)
{
    for (int k = 0; k < KO_TAIL_N; k++) {
        const int L = KO_TAIL[k].len;
        if (need_shorter ? (L >= n) : (L > n))
            continue;
        int ok = 1;
        for (int t = 0; t < L && ok; t++)
            ok = s[n - L + t] == KO_TAIL[k].s[t];
        if (ok)
            return k;
    }
    return -1;
}

/* 語尾のかたまりを後ろから既知の 조사・어미 に分ける（残った先頭は 1 かけら）。 */
static int split_tail_chunk(const uint16_t *s, int n, Tok *out, int max)
{
    Tok tmp[TOK_MAX];
    int m = 0;
    while (n > 0 && m < TOK_MAX) {
        const int k = tail_at_end(s, n, 0);
        if (k < 0) {
            tmp[m].p = s;
            tmp[m].n = n;
            m++;
            break;
        }
        tmp[m].p = KO_TAIL[k].c;
        tmp[m].n = KO_TAIL[k].clen;
        m++;
        n -= KO_TAIL[k].len;
    }
    int o = 0;
    for (int i = m - 1; i >= 0 && o < max; i--)    /* 後ろから積んだので戻す */
        out[o++] = tmp[i];
    return o;
}

/* 1 つの塊をトークンの列にする。
   ★**辞書引きが先、語尾剥がしが後**。逆にすると語尾で終わる語が壊れる ——
     「필요」（要る）が「필」＋「요」に割れ、キーワードが消えた（実測）。 */
static int split_run(const uint16_t *run, int n, Tok *out, int max)
{
    for (int k = 0; k < KO_CONTR_N; k++) {         /* ⓪ 不規則な縮約 */
        if (KO_CONTR[k].len != n)
            continue;
        int ok = 1;
        for (int t = 0; t < n && ok; t++)
            ok = run[t] == KO_CONTR[k].s[t];
        if (ok && max >= 2) {
            out[0].p = &KO_CONTR[k].a;
            out[0].n = 1;
            out[1].p = &KO_CONTR[k].b;
            out[1].n = 1;
            return 2;
        }
    }

    int h = head_of(run, n);                       /* ① そのままで引く */
    if (h) {
        out[0].p = run;
        out[0].n = h;
        return 1 + split_tail_chunk(run + h, n - h, out + 1, max - 1);
    }

    Tok tails[TOK_MAX];                            /* ② 語尾を剥がしてから引く */
    int tn = 0, hn = n;
    while (tn < TOK_MAX) {
        const int k = tail_at_end(run, hn, 1);     /* ★頭が残ることを要求する */
        if (k < 0)
            break;
        tails[tn].p = KO_TAIL[k].c;
        tails[tn].n = KO_TAIL[k].clen;
        tn++;
        hn -= KO_TAIL[k].len;
    }
    h = head_of(run, hn);
    int o = 0;
    if (!h) {
        if (tn == 0) {
            out[0].p = run;
            out[0].n = n;
            return 1;
        }
        out[o].p = run;
        out[o].n = hn;
        o++;
    } else {
        out[o].p = run;
        out[o].n = h;
        o++;
        o += split_tail_chunk(run + h, hn - h, out + o, max - o);
    }
    for (int i = tn - 1; i >= 0 && o < max; i--)
        out[o++] = tails[i];
    return o;
}

/* 1 トークンを書き出す（2 つ目からは前に空白を置く） */
static void put(char *dst, int max, int *o, int *first, const uint16_t *s, int n)
{
    if (!*first && *o + 1 < max)
        dst[(*o)++] = ' ';
    *first = 0;
    *o += u16_to_u8(s, n, dst + *o, max - *o);
}

int josa(const uint16_t *src, int slen, char *dst, int max)
{
    int o = 0, first = 1;
    if (max > 0)
        dst[0] = 0;

    for (int i = 0; i < slen;) {
        const uint16_t p = punct_of(src[i]);
        if (p || is_space(src[i])) {
            if (p)
                put(dst, max, &o, &first, &p, 1);
            i++;
            continue;
        }
        int j = i;
        while (j < slen && !punct_of(src[j]) && !is_space(src[j]))
            j++;
        Tok toks[TOK_MAX];
        const int tn = split_run(src + i, j - i, toks, TOK_MAX);
        for (int k = 0; k < tn; k++) {
            if (k)
                put(dst, max, &o, &first, &MARK, 1);
            put(dst, max, &o, &first, toks[k].p, toks[k].n);
        }
        i = j;
    }
    return o;
}
