/* 整形（shape_run）の検査 —— デーヴァナーガリーが並べ替えと合字を出すか。
 *
 * ★**ここが「1 対 1 でない」ことを目で確かめる唯一の場所。** 画素で見ると
 *   「それらしく出ている」で通ってしまうので、字と画の対応（cluster）を数で見る。
 */
#include <stdio.h>
#include <string.h>
#include "glyph.h"
#include "text.h"

static int ng;

static int shape(const char *u8, Shaped *sh, int max, int *nchar)
{
    uint16_t s[64];
    const int n = u8_to_u16(u8, -1, s, 64);
    if (nchar) *nchar = n;
    return shape_run(s, n, sh, max);
}

static void show(const char *label, const char *u8)
{
    Shaped sh[128];
    int n = 0;
    const int m = shape(u8, sh, 128, &n);
    printf("  %-20s 字 %2d → 画 %2d  cluster:", label, n, m);
    for (int i = 0; i < m; i++)
        printf(" %u%s", sh[i].cluster, (sh[i].dx || sh[i].dy) ? "*" : "");
    printf("\n");
}

static void want(const char *label, const char *u8, int glyphs, const char *note)
{
    Shaped sh[128];
    int n = 0;
    const int m = shape(u8, sh, 128, &n);
    if (m == glyphs) {
        printf("  ✓ %-28s 字 %d → 画 %d（%s）\n", label, n, m, note);
    } else {
        printf("  ★%-28s 字 %d → 画 %d（%s を期待）\n", label, n, m, glyphs, note);
        ng = 1;
    }
}

int main(void)
{
    glyph_init();

    puts("\n--- 主面（日本語・英語）は 1 対 1 のまま ---");
    want("ひらがな", "あいう", 3, "整形を通さない");
    want("英字", "hi", 2, "★合字を出さない");
    show("英字の中身", "hi");

    puts("\n--- デーヴァナーガリー ---");
    show("क（素の子音）", "क");
    show("कि（i 母音）", "कि");
    show("क्ष（結合）", "क्ष");
    show("मुझे", "मुझे");
    show("हूँ", "हूँ");
    show("मुझे बताइए", "मुझे बताइए");

    puts("\n--- 数で縛る ---");
    want("क", "क", 1, "1 字 1 画");
    want("क्ष = क + ् + ष", "क्ष", 1, "★3 字が 1 画に畳まれる");
    want("混在（あ + क）", "अ", 1, "面が変わっても数は合う");

    puts(ng ? "\n★食い違いがある" : "\n--- すべて通った ---");
    return ng;
}
