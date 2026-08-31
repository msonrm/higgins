/* 日本語出力の空白を繋ぐ —— wakachi の対。
 *
 * ELIZA のテンプレートはトークン列なので、そのまま出すと「謝ら ない で ください」と
 * 空白だらけになる。埋め戻し（スロット）を含む以上テンプレートを 1 トークンで書く
 * ことはできないので、★出力側で繋ぐ。
 * ★ただし全部の空白を消すと、打った英文が壊れる（「I am sad」→「Iamsad」）。
 *   **日本語文字に隣接する空白だけ**を消す。
 *
 * ★出力が uint16_t なのは、次が画面（hist_line）だから。
 */
#ifndef HG_TSUNAGI_H
#define HG_TSUNAGI_H
#include <stdint.h>

/* ELIZA の応答（UTF-8）→ 画面に出す字。戻り値 = 字数。 */
int tsunagi(const char *utf8, uint16_t *dst, int max);

#endif
