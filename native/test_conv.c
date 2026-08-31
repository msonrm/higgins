/* 変換セッション（conv.c）を SDL を開かずに走らせる。★test_reply と同じ手 ——
 * UI を作る前に「変換が成立するか」だけを確かめる。
 *
 *   ./test_conv [mozc.data] [profile dir] < よみの並び
 *
 * 1 行 = 1 よみ。出力は「変換結果  [その文節の候補…]」。
 */
#include <stdio.h>
#include <string.h>

#include "conv.h"
#include "text.h"

static void put16(const uint16_t *s, int n)
{
    char buf[4096];
    u16_to_u8(s, n, buf, (int)sizeof buf);
    fputs(buf, stdout);
}

int main(int argc, char **argv)
{
    const char *data = argc > 1 ? argv[1] : "vendor/hechima/mozc.data";
    const char *prof = argc > 2 ? argv[2] : "conf";
    if (!conv_init(data, prof)) {
        fprintf(stderr, "エンジンが起きない（辞書: %s）\n", data);
        return 1;
    }

    char line[2048];
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0])
            continue;
        uint16_t kana[CONV_STR_MAX];
        const int n = u8_to_u16(line, -1, kana, CONV_STR_MAX);
        if (!conv_start(kana, n)) {
            printf("(変換できない)\n");
            continue;
        }
        uint16_t out[CONV_STR_MAX * 2];
        int from = 0, to = 0;
        const int m = conv_text(out, (int)(sizeof out / sizeof *out), &from, &to);
        put16(out, m);
        printf("  文節[%d] 先頭=[%d,%d)  候補:", conv_cand_count(), from, to);
        for (int i = 0; i < conv_cand_count(); i++) {
            uint16_t c[CONV_STR_MAX];
            const int cn = conv_cand(i, c, CONV_STR_MAX);
            printf(" ");
            put16(c, cn);
        }
        printf("\n");
        conv_cancel();
    }
    return 0;
}
