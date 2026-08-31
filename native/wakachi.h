/* 日本語の簡易分かち書き —— ELIZA スクリプト自身のキーワードを辞書にする。
 *
 * ELIZA の split_user_input() は空白区切り前提で、4 言語のうち日本語だけがそれを
 * 満たさない。形態素解析器を持ち込む前に、★スクリプトが既に持っている語で切る。
 * 要点は ★**「キーワードは必ず切り出せる」ことが構造的に保証される**こと。
 *
 * ★**語彙を集める規則は tools/wakachi.py（collect_vocab）が正典**で、その出力を
 *   tools/gen_vocab.py が vocab.h に焼く。ここに来ているのは**最長一致だけ**。
 *
 * ★入力が uint16_t なのは、殻の合成バッファ（打った字）がそれだから。
 *   出力が UTF-8 なのは、ELIZA が std::string で話すから。境界は 1 回だけ。
 */
#ifndef HG_WAKACHI_H
#define HG_WAKACHI_H
#include <stdint.h>

/* 打った字 → 空白区切りの UTF-8。戻り値 = 書いたバイト数（NUL 終端する）。 */
int wakachi(const uint16_t *src, int slen, char *dst, int max);

#endif
