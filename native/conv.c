#include "conv.h"
#include "hechima.h"
#include "text.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    uint16_t key[CONV_STR_MAX];
    int key_n;
    uint16_t cand[CONV_CAND_MAX][CONV_STR_MAX];
    int cand_n[CONV_CAND_MAX];
    int ncand;
} ConvSeg;

static struct {
    uint16_t kana[CONV_STR_MAX];
    int kana_n;
    ConvSeg seg[CONV_SEG_MAX];
    int nseg;
    int focus;
    int pick[CONV_SEG_MAX];
    int active;
} g;

static int g_ready;

/* ---- JSON（この形に特化した手書きのスキャナ）----
 *
 *   {"segments":[{"key":"かな","candidates":["候補",..],"costs":[..]}]}
 *
 * ★汎用のパーサは持たない。形は上流（labo の serialize_segments_of）が決めていて、
 *   変われば下が 0 を返す ＝ **変換できない**とすぐ分かる。黙って半分動くことはない。
 * ★エスケープは上流の json_escape に合わせて \" \\ \n \r \t \uXXXX だけ
 *   （\u は制御文字にしか使われない）。日本語は UTF-8 のまま流れてくる。
 */

/* 文字列を 1 つ読む。p は開き " を指していること。戻り値 = 閉じ " の次 / 0 = 失敗 */
static const char *read_str(const char *p, char *buf, int max, int *len)
{
    if (!p || *p != '"')
        return 0;
    p++;
    int o = 0;
    while (*p && *p != '"') {
        if (*p == '\\') {
            p++;
            char c = 0;
            switch (*p) {
            case 'n': c = '\n'; p++; break;
            case 'r': c = '\r'; p++; break;
            case 't': c = '\t'; p++; break;
            case 0:   return 0;
            case 'u': {
                int v = 0;
                p++;
                for (int i = 0; i < 4 && *p; i++, p++) {
                    const char h = *p;
                    v = v * 16 + (h >= '0' && h <= '9' ? h - '0'
                                : h >= 'a' && h <= 'f' ? h - 'a' + 10
                                : h >= 'A' && h <= 'F' ? h - 'A' + 10 : 0);
                }
                c = (char)v;                  /* 上流は < 0x20 にしか使わない */
                break;
            }
            default:  c = *p; p++; break;     /* \" \\ \/ */
            }
            if (o < max - 1)
                buf[o++] = c;
        } else {
            if (o < max - 1)
                buf[o++] = *p;
            p++;
        }
    }
    if (*p != '"')
        return 0;
    buf[o] = 0;
    *len = o;
    return p + 1;
}

/* 知らないフィールドの値を飛ばす（costs など。★後方互換の追加に備える） */
static const char *skip_value(const char *p)
{
    if (*p == '[') {
        while (*p && *p != ']')
            p++;
        return *p ? p + 1 : p;
    }
    if (*p == '"') {
        char b[8];
        int l;
        const char *q = read_str(p, b, sizeof b, &l);
        return q ? q : p + 1;
    }
    while (*p && *p != ',' && *p != '}')
        p++;
    return p;
}

static int parse_segments(const char *json)
{
    if (!json || !*json)
        return 0;
    const char *p = strstr(json, "\"segments\":[");
    if (!p)
        return 0;
    p += 12;
    g.nseg = 0;

    while (*p && *p != ']') {
        if (*p != '{') {
            p++;
            continue;
        }
        p++;
        ConvSeg *s = (g.nseg < CONV_SEG_MAX) ? &g.seg[g.nseg] : 0;
        if (s) {
            s->ncand = 0;
            s->key_n = 0;
        }
        while (*p && *p != '}') {
            if (*p != '"') {
                p++;
                continue;
            }
            char name[16];
            int nl;
            const char *q = read_str(p, name, sizeof name, &nl);
            if (!q)
                return 0;
            while (*q == ':' || *q == ' ')
                q++;
            char buf[CONV_STR_MAX * 4];
            int bl;
            if (strcmp(name, "key") == 0) {
                q = read_str(q, buf, sizeof buf, &bl);
                if (!q)
                    return 0;
                if (s)
                    s->key_n = u8_to_u16(buf, bl, s->key, CONV_STR_MAX);
            } else if (strcmp(name, "candidates") == 0) {
                if (*q != '[')
                    return 0;
                q++;
                while (*q && *q != ']') {
                    if (*q != '"') {
                        q++;
                        continue;
                    }
                    q = read_str(q, buf, sizeof buf, &bl);
                    if (!q)
                        return 0;
                    if (s && s->ncand < CONV_CAND_MAX) {
                        s->cand_n[s->ncand] =
                            u8_to_u16(buf, bl, s->cand[s->ncand], CONV_STR_MAX);
                        s->ncand++;
                    }
                }
                if (*q == ']')
                    q++;
            } else {
                q = skip_value(q);
            }
            p = q;
        }
        if (*p == '}')
            p++;
        if (s && s->key_n > 0 && s->ncand > 0)
            g.nseg++;
    }
    return g.nseg > 0;
}

/* ---- 起動 ---- */

int conv_init(const char *data_path, const char *profile_dir)
{
    if (profile_dir && *profile_dir)
        hechima_set_profile(profile_dir);   /* ★init より前 */
    g_ready = hechima_init(data_path) == 0;
    return g_ready;
}

int conv_ready(void) { return g_ready; }
int conv_active(void) { return g.active; }

/* ---- 変換 ---- */

int conv_start(const uint16_t *kana, int n)
{
    if (!g_ready || n <= 0)
        return 0;
    if (n > CONV_STR_MAX)
        n = CONV_STR_MAX;
    char u8[CONV_STR_MAX * 4];
    u16_to_u8(kana, n, u8, (int)sizeof u8);
    if (!parse_segments(hechima_convert(u8, CONV_CAND_MAX)))
        return 0;
    for (int i = 0; i < n; i++)
        g.kana[i] = kana[i];
    g.kana_n = n;
    for (int i = 0; i < CONV_SEG_MAX; i++)
        g.pick[i] = 0;
    g.focus = 0;
    g.active = 1;
    return 1;
}

void conv_next(void)
{
    if (!g.active)
        return;
    const ConvSeg *s = &g.seg[g.focus];
    if (s->ncand > 0)
        g.pick[g.focus] = (g.pick[g.focus] + 1) % s->ncand;
}

void conv_prev(void)
{
    if (!g.active)
        return;
    const ConvSeg *s = &g.seg[g.focus];
    if (s->ncand > 0)
        g.pick[g.focus] = (g.pick[g.focus] + s->ncand - 1) % s->ncand;
}

void conv_move(int d)
{
    if (!g.active)
        return;
    const int nf = g.focus + d;
    if (nf >= 0 && nf < g.nseg)
        g.focus = nf;
}

int conv_resize(int d)
{
    if (!g.active || !d)
        return 0;
    /* ★"" = 伸縮不能（先頭を縮め切った / 末尾を伸ばし切った）＝ 現状維持 */
    const char *json = hechima_resize(g.focus, d, CONV_CAND_MAX);
    if (!json || !*json)
        return 0;
    if (!parse_segments(json))
        return 0;
    /* ★文節の切り方が変わったので選び直し（TS 版も同じ）。focus は残す */
    for (int i = 0; i < CONV_SEG_MAX; i++)
        g.pick[i] = 0;
    if (g.focus >= g.nseg)
        g.focus = g.nseg - 1;
    return 1;
}

void conv_cancel(void)
{
    g.active = 0;
    g.nseg = 0;
    g.focus = 0;
}

int conv_text(uint16_t *out, int max, int *from, int *to)
{
    int o = 0;
    for (int i = 0; i < g.nseg; i++) {
        const ConvSeg *s = &g.seg[i];
        const int pi = g.pick[i] < s->ncand ? g.pick[i] : 0;
        if (i == g.focus && from)
            *from = o;
        for (int t = 0; t < s->cand_n[pi] && o < max; t++)
            out[o++] = s->cand[pi][t];
        if (i == g.focus && to)
            *to = o;
    }
    return o;
}

int conv_cand_count(void)
{
    return g.active ? g.seg[g.focus].ncand : 0;
}

int conv_cand_index(void)
{
    return g.active ? g.pick[g.focus] : 0;
}

int conv_cand(int i, uint16_t *out, int max)
{
    if (!g.active || i < 0 || i >= g.seg[g.focus].ncand)
        return -1;
    const ConvSeg *s = &g.seg[g.focus];
    int n = s->cand_n[i];
    if (n > max)
        n = max;
    for (int t = 0; t < n; t++)
        out[t] = s->cand[i][t];
    return n;
}

int conv_commit(uint16_t *out, int max)
{
    const int n = conv_text(out, max, 0, 0);

    /* ★学習させる。sizes = 各文節のよみ字数、values = 選んだ値（タブ区切り）。
       上流が「既定の分節と同じなら境界は学習しない」を判定するので、
       こちらは**常に送ってよい**（hechima_learn のコメント）。 */
    char kana[CONV_STR_MAX * 4];
    char sizes[CONV_SEG_MAX * 4];
    char values[CONV_SEG_MAX * CONV_STR_MAX];
    u16_to_u8(g.kana, g.kana_n, kana, (int)sizeof kana);
    int so = 0, vo = 0;
    for (int i = 0; i < g.nseg; i++) {
        const ConvSeg *s = &g.seg[i];
        const int pi = g.pick[i] < s->ncand ? g.pick[i] : 0;
        so += snprintf(sizes + so, sizeof sizes - (size_t)so, "%s%d",
                       i ? "," : "", s->key_n);
        if (i && vo < (int)sizeof values - 1)
            values[vo++] = '\t';
        vo += u16_to_u8(s->cand[pi], s->cand_n[pi], values + vo,
                        (int)sizeof values - vo);
    }
    hechima_learn(kana, sizes, values);

    conv_cancel();
    return n;
}
