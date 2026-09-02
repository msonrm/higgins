/* 韓国語出力の繋ぎ —— josa の対。tsunagi の韓国語版。
 *
 * ★**日本語版と役割が違う**。日本語の tsunagi は「日本語に隣接する空白を全部消す」
 *   だが、韓国語は**空白に意味がある**（띄어쓰기）ので全部は消せない。
 *   消すのは josa が印（`·`）を付けた境目だけ。
 *
 * ★★そしてここが 조사 の**実現**の場でもある。入り口で代表形に寄せた
 *   는・가・를・와・로 を、**直前の音節の받침**を見て実際の形に戻す
 *   （당신 + 는 → 당신은 / 어머니 + 는 → 어머니는）。
 *   規則の正典は tools/tsunagi_ko.py（JOSA_FORM）。
 */
#ifndef HG_TSUNAGI_KO_H
#define HG_TSUNAGI_KO_H
#include <stdint.h>

/* ELIZA の応答（UTF-8）→ 画面に出す字。戻り値 = 字数。 */
int tsunagi_ko(const char *utf8, uint16_t *dst, int max);

#endif
