/* ハングルの合成 —— labo web の KoreanComposer（korean-composer.ts）の C 移植。
 *
 * 音節 = 0xAC00 + (초성 * 21 + 중성) * 28 + 종성。**1 コードポイントに合成済み**
 * なので shaping は要らず、同梱フォント（Noto Sans CJK JP）がそのまま持っている。
 *
 * ★表は native/gen_korean.py が labo の TS から焼く（korean_tables.h）。
 *   ここに来ているのは**手順だけ** —— josa / wakachi と同じ切り分け。
 *
 * ★**자모 모드（ㅋㅋㅋ の類）は載せない。** あれは GIME（Swift / Kotlin）にしかなく、
 *   labo web の stepKorean は最初から持っていない。移植元を web にすることで、
 *   「載せない」が引き算ではなく**移植元の選択**で済んでいる（TODO の段階 3）。
 */
#ifndef HG_KOREAN_H
#define HG_KOREAN_H
#include <stdint.h>

#include "input.h"

/* 合成中の音節。-1 = 無し（TS の null）。 */
typedef struct { int onset, nucleus, coda; } KoComposer;

void ko_init(KoComposer *k);
void ko_commit(KoComposer *k);                 /* 確定 = 状態を捨てる */
int  ko_composing(const KoComposer *k);
uint16_t ko_current(const KoComposer *k);      /* 合成中の音節（0 = 無し） */

/* GpAction を字にする。戻り値 1 = 出力あり（*out に音節・*replace に差し替え字数）。
 * ★**ここが唯一の適用点**。本体と検査で別々に書くと、片方でしか通らない値が出る。 */
int ko_apply(KoComposer *k, const GpAction *a, uint16_t *out, int *replace);

/* rs↑ = 받침があれば받침、無ければ초성をサイクル（GIME の handleKoreanOnsetCycle）。 */
int ko_cycle(KoComposer *k, uint16_t *out, int *replace);
/* rs→ = ㅏ/ㅓ 付加の複合母音（GIME の handleKoreanVowelAddAEo）。 */
int ko_add_aeo(KoComposer *k, uint16_t *out, int *replace);

#endif
