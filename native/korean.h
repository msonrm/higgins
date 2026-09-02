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

/* 合成中の音節。-1 = 無し（TS の null）。
   ★`rollback_coda` / `rollback_pending` = 直前に付けた받침を**戻せるか**と、その戻し先。
     ★★**겹받침も戻す。** GIME は「겹받침は 2 回の明示的な받침入力で作るものだから
       分解しない」として除外していたが、**それが「우울해요」→「우욿해요」の原因**
       だった（実機・2026-09-02）—— 「울」のあとに ㅎ の行が母音より先に着くと
       겹받침 ㅀ が出来てしまい、母音が来ても解けない。
     ★戻していい理由: **巻き戻せる状態は「子音を押したまま」の間しか続かない**
       （離すと machine 側の pending が落ちる）。つまり巻き戻しが起きるのは
       **その子音が次の音節の頭だった**ときだけで、意図した겹받침は必ず一度離す。
     ★`rollback_pending` は**成功した받침のあとだけ**立てる —— 失敗した받침
       （겹받침にならない組み合わせ）のあとに戻すと、**前の音節の받침まで消える**。 */
typedef struct { int onset, nucleus, coda, rollback_coda, rollback_pending; } KoComposer;

void ko_init(KoComposer *k);
void ko_commit(KoComposer *k);                 /* 確定 = 状態を捨てる */
int  ko_composing(const KoComposer *k);
uint16_t ko_current(const KoComposer *k);      /* 合成中の音節（0 = 無し） */

/* GpAction を字にする。戻り値 1 = 出力あり（*out に音節・*replace に差し替え字数）。
 * ★**ここが唯一の適用点**。本体と検査で別々に書くと、片方でしか通らない値が出る。 */
int ko_apply(KoComposer *k, const GpAction *a, uint16_t *out, int *replace);

/* 行インジケータに出す字（互換ジャモ）。日本語の gp_row_char に当たる。 */
uint16_t ko_row_char(int row);

/* rs↑ = 받침があれば받침、無ければ초성をサイクル（GIME の handleKoreanOnsetCycle）。 */
int ko_cycle(KoComposer *k, uint16_t *out, int *replace);
/* rs→ = ㅏ/ㅓ 付加の複合母音（GIME の handleKoreanVowelAddAEo）。 */
int ko_add_aeo(KoComposer *k, uint16_t *out, int *replace);

#endif
