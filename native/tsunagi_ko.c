#include "tsunagi_ko.h"
#include "text.h"
#include "tsunagi_ko_tables.h"

enum { KO_BUF = 4096 };
static const uint16_t MARK = 0x00B7;

/* 末尾の音節の종성 index。0 = 받침なし。ハングル以外は 0 とみなす。 */
static int batchim(uint16_t c)
{
    return (c >= 0xAC00 && c <= 0xD7A3) ? (int)((c - 0xAC00) % 28) : 0;
}

/* 代表形の 조사 を直前の語に合わせた形で書く。戻り値 = 書いた字数。 */
static int realize(uint16_t *dst, int o, int max, const uint16_t *tok, int len)
{
    if (len == 1 && o > 0) {
        for (int k = 0; k < KO_FORM_N; k++) {
            if (KO_FORM[k].canon != tok[0])
                continue;
            const int b = batchim(dst[o - 1]);
            /* ★ㄹ 받침（index 8）だけは「로」を取る（서울로） */
            const int with_b = b && !(KO_FORM[k].canon == 0xB85C && b == 8);
            const unsigned short *s = with_b ? KO_FORM[k].w : KO_FORM[k].n;
            const int sn = with_b ? KO_FORM[k].wlen : KO_FORM[k].nlen;
            int w = 0;
            for (int t = 0; t < sn && o + w < max; t++)
                dst[o + w++] = s[t];
            return w;
        }
    }
    int w = 0;
    for (int t = 0; t < len && o + w < max; t++)
        dst[o + w++] = tok[t];
    return w;
}

int tsunagi_ko(const char *utf8, uint16_t *dst, int max)
{
    uint16_t src[KO_BUF];
    const int n = u8_to_u16(utf8, -1, src, KO_BUF);
    int o = 0, glue = 0, first = 1;

    for (int i = 0; i < n;) {
        if (src[i] == ' ') {
            i++;
            continue;
        }
        int j = i;
        while (j < n && src[j] != ' ')
            j++;
        const int len = j - i;

        if (len == 1 && src[i] == MARK) {          /* 印そのものは出さない */
            glue = 1;
            i = j;
            continue;
        }
        if (glue && o > 0) {
            o += realize(dst, o, max, src + i, len);
        } else {
            /* ★句読点の前には空白を置かない（分かち直しで入ったもの） */
            const int is_punct = (len == 1 && (src[i] == ',' || src[i] == '.'));
            if (!first && !is_punct && o < max)
                dst[o++] = ' ';
            for (int t = 0; t < len && o < max; t++)
                dst[o++] = src[i + t];
        }
        glue = 0;
        first = 0;
        i = j;
    }
    return o;
}
