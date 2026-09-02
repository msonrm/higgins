/* 韓国語の分かち直し —— 語にくっついた 조사・어미 を剥がす。wakachi の韓国語版。
 *
 * ★**日本語ほどの仕事は要らない** —— 韓国語には띄어쓰기があるので、ELIZA の
 *   split_user_input() はそのまま通る。要るのは「어머니가」から「어머니」を
 *   切り出すことだけ。
 *
 * ★**規則の正典は tools/josa.py**（TAILS / JOSA_CANON / CONTRACTIONS）で、
 *   その表を tools/gen_josa.py が josa_tables.h に、語彙を tools/gen_vocab.py が
 *   vocab_ko.h に焼く。ここに来ているのは**手順だけ**（wakachi と同じ切り分け）。
 *
 * ★繋ぎの印は独立したトークン `·`（U+00B7）。語にくっつけると、
 *   「처럼」のように 조사 でありながらキーワードでもある語が書けなくなる。
 */
#ifndef HG_JOSA_H
#define HG_JOSA_H
#include <stdint.h>

/* 打った字 → 空白区切りの UTF-8。戻り値 = 書いたバイト数（NUL 終端する）。 */
int josa(const uint16_t *src, int slen, char *dst, int max);

#endif
