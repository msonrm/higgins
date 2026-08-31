#include "tsunagi.h"
#include "text.h"

/* ひらがな・カタカナ・漢字・全角の句読点と記号。
   ★範囲は tools/tsunagi.py の JA と同じ 5 つ。片方だけ広げないこと。 */
static int is_ja(uint16_t c)
{
    return (c >= 0x3041 && c <= 0x309F)      /* ぁ-ゟ */
        || (c >= 0x30A0 && c <= 0x30FF)      /* ゠-ヿ */
        || (c >= 0x4E00 && c <= 0x9FFF)      /* 一-鿿 */
        || (c >= 0x3000 && c <= 0x303F)      /* 　-〿 */
        || (c >= 0xFF01 && c <= 0xFF60);     /* ！-｠ */
}

int tsunagi(const char *utf8, uint16_t *dst, int max)
{
    const int n = u8_to_u16(utf8, -1, dst, max);
    int o = 0;
    for (int i = 0; i < n;) {
        if (dst[i] != ' ') {
            dst[o++] = dst[i++];
            continue;
        }
        int j = i;
        while (j < n && dst[j] == ' ')
            j++;
        const uint16_t prev = o > 0 ? dst[o - 1] : 0;
        const uint16_t next = j < n ? dst[j] : 0;
        if (is_ja(prev) || is_ja(next)) {    /* 日本語に隣接する空白は消す */
            i = j;
            continue;
        }
        while (i < j)                        /* 英文の中の空白はそのまま */
            dst[o++] = dst[i++];
    }
    return o;
}
