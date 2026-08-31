/* UTF-8 ⇄ uint16_t —— 殻の文字コードとの境界。
 *
 * ★殻は字を `uint16_t` で持つ（PS1 の 2MB RAM から来た形）。ELIZA も mozc も
 *   UTF-8 の `char *` で話すので、変換がここに要る。
 *   ★Zenmai では変換が**ビルド時**に済んでいた（gen_data.py が生成物に焼いた）ので、
 *   **実行時の変換器は Higgins の新規**。
 *
 * ★BMP の外（U+10000 以上 = 絵文字・拡張漢字）は U+FFFD に落とす。
 *   4 言語（日本語・韓国語・英語・ヒンディー語）はデーヴァナーガリーまで全部 BMP 内
 *   なので、**`uint32_t` 化は要らない**。要るようになったら、そこが殻を割る合図
 *   （PS1 は 2MB RAM なので u32 を払えない ―― vendor/VENDOR.md 参照）。
 */
#ifndef HG_TEXT_H
#define HG_TEXT_H
#include <stdint.h>

/* UTF-8 → uint16_t。slen < 0 なら NUL 終端とみなす。戻り値 = 書いた字数。
   壊れたバイト列と BMP 外は U+FFFD 1 字にする（落とさない ―― ★黙って消えると
   「打ったのに出ない」が字の問題なのか経路の問題なのか分からなくなる）。 */
int u8_to_u16(const char *src, int slen, uint16_t *dst, int max);

/* uint16_t → UTF-8。戻り値 = 書いたバイト数（dst は必ず NUL 終端する）。
   max はバイト数（終端を含む）。 */
int u16_to_u8(const uint16_t *src, int slen, char *dst, int max);

#endif
