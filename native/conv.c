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
    /* ★pick は**表示リスト上の位置**（追加候補を含む）。
       表示リスト = [追加候補 addl 個][本来の候補 ncand 個] */
    int pick[CONV_SEG_MAX];
    int addl[CONV_SEG_MAX];            /* 追加候補の展開数（0..2） */
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
                        const int cn =
                            u8_to_u16(buf, bl, s->cand[s->ncand], CONV_STR_MAX);
                        /* ★BMP の外（絵文字など）を含む候補は**捨てる**。
                           殻は字を uint16_t で持つので描けず、u8_to_u16 が
                           U+FFFD に落としている ＝ 並べても豆腐になる。
                           ★**出せない候補を候補として並べるのは嘘**
                           （使っていないフォントを名乗るのと同じ形）。
                           mozc の「さびしい」には実際に絵文字が入っていた。 */
                        int ok = cn > 0;
                        for (int t = 0; t < cn && ok; t++)
                            ok = s->cand[s->ncand][t] != 0xFFFD;
                        if (ok) {
                            s->cand_n[s->ncand] = cn;
                            s->ncand++;
                        }
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

/* ---- 追加候補（ひらがな / カタカナ）----
 *
 * ★hechima（`web/src/hechima/session.ts` の addl*）と同じ作法。
 *   注目文節の**よみ**から作り、**候補の前に段階的に現れる** ——
 *   1 番目で ↑ を押すと ひらがな、もう一度で カタカナ。
 *   並びは `[カタカナ, ひらがな]` で、展開は**末尾から**（ひらがなが先に出る）。
 * ★文字種の注釈（「カタカナ」）は出さない（実機の指摘・2026-08-31。要らない）。
 */

static uint16_t to_kata(uint16_t c)
{
    return (c >= 0x3041 && c <= 0x3096) ? (uint16_t)(c + 0x60) : c;
}

/* 追加候補は何個ありうるか（1 or 2）。よみが既に全部カタカナなら ひらがな だけ。 */
static int addl_count(int f)
{
    const ConvSeg *s = &g.seg[f];
    for (int i = 0; i < s->key_n; i++)
        if (s->key[i] >= 0x3041 && s->key[i] <= 0x3096)
            return 2;                  /* ひらがなが混じる = カタカナ候補が別物になる */
    return 1;
}

/* 表示リストの i 番目（i < addl）の追加候補を取り出す */
static int addl_at(int f, int i, uint16_t *out, int max)
{
    const ConvSeg *s = &g.seg[f];
    const int n = addl_count(f);
    const int k = (n - g.addl[f]) + i; /* 0 = カタカナ / 1 = ひらがな */
    const int kata = (n == 2 && k == 0);
    int len = s->key_n < max ? s->key_n : max;
    for (int t = 0; t < len; t++)
        out[t] = kata ? to_kata(s->key[t]) : s->key[t];
    return len;
}

/* 文節 f でいま選ばれている文字列 */
static int seg_selected(int f, uint16_t *out, int max)
{
    const ConvSeg *s = &g.seg[f];
    const int p = g.pick[f];
    if (p < g.addl[f])
        return addl_at(f, p, out, max);
    const int k = p - g.addl[f];
    const int ki = (k >= 0 && k < s->ncand) ? k : 0;
    int n = s->cand_n[ki] < max ? s->cand_n[ki] : max;
    for (int t = 0; t < n; t++)
        out[t] = s->cand[ki][t];
    return n;
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
        g.pick[i] = g.addl[i] = 0;
    g.focus = 0;
    g.active = 1;
    return 1;
}

/* ★端で止まる（巡回しない）。追加候補は「先頭のさらに前」にあるので、
   巡回すると展開の途中で末尾へ飛んで迷子になる。 */
void conv_next(void)
{
    if (!g.active)
        return;
    const int f = g.focus;
    const int n = g.addl[f] + g.seg[f].ncand;
    if (g.pick[f] + 1 < n)
        g.pick[f]++;
}

void conv_prev(void)
{
    if (!g.active)
        return;
    const int f = g.focus;
    if (g.pick[f] > 0) {
        g.pick[f]--;
        return;
    }
    /* ★先頭でさらに ↑ = 追加候補（ひらがな → カタカナ）を段階展開。
       pick は 0 のまま ＝ 新しく現れた先頭を指す。 */
    if (g.addl[f] < addl_count(f))
        g.addl[f]++;
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
        g.pick[i] = g.addl[i] = 0;
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
        uint16_t buf[CONV_STR_MAX];
        const int n = seg_selected(i, buf, CONV_STR_MAX);
        if (i == g.focus && from)
            *from = o;
        for (int t = 0; t < n && o < max; t++)
            out[o++] = buf[t];
        if (i == g.focus && to)
            *to = o;
    }
    return o;
}

int conv_cand_count(void)
{
    return g.active ? g.addl[g.focus] + g.seg[g.focus].ncand : 0;
}

int conv_cand_index(void)
{
    return g.active ? g.pick[g.focus] : 0;
}

int conv_cand(int i, uint16_t *out, int max)
{
    if (!g.active || i < 0 || i >= conv_cand_count())
        return -1;
    const int f = g.focus;
    if (i < g.addl[f])
        return addl_at(f, i, out, max);
    const ConvSeg *s = &g.seg[f];
    const int k = i - g.addl[f];
    int n = s->cand_n[k] < max ? s->cand_n[k] : max;
    for (int t = 0; t < n; t++)
        out[t] = s->cand[k][t];
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
        uint16_t buf[CONV_STR_MAX];
        const int n = seg_selected(i, buf, CONV_STR_MAX);
        so += snprintf(sizes + so, sizeof sizes - (size_t)so, "%s%d",
                       i ? "," : "", g.seg[i].key_n);
        if (i && vo < (int)sizeof values - 1)
            values[vo++] = '\t';
        vo += u16_to_u8(buf, n, values + vo, (int)sizeof values - vo);
    }
    hechima_learn(kana, sizes, values);

    conv_cancel();
    return n;
}
