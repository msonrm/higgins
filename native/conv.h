/* 変換セッション —— hechima（mozc）を C から使う最小の層。
 *
 * ★TS 版（labo の `web/src/hechima/session.ts`）は編集キーの二重経路・確定アンドゥ・
 *   再変換・ユーザー辞書まで持つが、ここは**打って・変換して・確定する**だけ。
 * ★操作の作法は GamepadEngine（labo の `web/src/gamepad/engine.ts`）に揃える ——
 *   LS↓ 変換/次候補・LS↑ 前候補・LS←→ 文節移動・**RT+LS←→ 文節伸縮**。
 *
 * ★変換が使えなくても Higgins は動く（かなのまま ELIZA へ渡る）。
 *   辞書 19MB を積めない機種でも起動はする、という形にしておく。
 */
#ifndef HG_CONV_H
#define HG_CONV_H
#include <stdint.h>

enum { CONV_SEG_MAX = 24, CONV_CAND_MAX = 9, CONV_STR_MAX = 128 };

/* エンジンを起こす。1 = 使える */
int conv_init(const char *data_path, const char *profile_dir);
int conv_ready(void);

int conv_active(void);                  /* 変換中か */

int conv_start(const uint16_t *kana, int n);   /* 変換開始。1 = 変換できた */
void conv_next(void);                   /* 次候補 */
void conv_prev(void);                   /* 前候補 */
void conv_move(int d);                  /* 文節移動（-1 / +1） */
int  conv_resize(int d);                /* いまの文節を伸縮（-1 / +1）。1 = 変わった */
void conv_cancel(void);                 /* 変換をやめる */

/* いまの見た目。focus の文節が [*from, *to) に来る（0 を渡してもよい）。 */
int conv_text(uint16_t *out, int max, int *from, int *to);

/* いまの文節の候補を 1 つ取り出す（候補窓用）。戻り値 = 字数 / -1 = 無い */
int conv_cand(int i, uint16_t *out, int max);
int conv_cand_count(void);
int conv_cand_index(void);

/* 確定して学習する。戻り値 = 字数 */
int conv_commit(uint16_t *out, int max);

#endif
