/* native/gen_devanagari.py が labo の GIME（Swift）から生成。手で編集しない。
 * 正典 = Sources/GIME/GamepadResolver.swift（配置）
 *        Sources/GIME/DevanagariComposer.swift（長短の対）
 * ★web にデーヴァナーガリーの実装は無いので、**実装のある側**を正典にしている。 */
#ifndef HG_DEVANAGARI_TABLES_H
#define HG_DEVANAGARI_TABLES_H

/* varga（LS 方向: 中立=प / ↑=क / →=च / ↓=ट / ←=त）→ 子音 5 個。
   並びは varnamala 順 = [無気無声, 有気無声, 無気有声, 有気有声, 鼻音]。
   ★D-pad ↑→↓← が index 0..3、LB が index 4（鼻音）。 */
enum { DEVA_VARGA_N = 5, DEVA_STOP_N = 5 };
static const unsigned short DEVA_VARGA[DEVA_VARGA_N][DEVA_STOP_N] = {
    { 0x0915, 0x0916, 0x0917, 0x0918, 0x0919 },
    { 0x091A, 0x091B, 0x091C, 0x091D, 0x091E },
    { 0x091F, 0x0920, 0x0921, 0x0922, 0x0923 },
    { 0x0924, 0x0925, 0x0926, 0x0927, 0x0928 },
    { 0x092A, 0x092B, 0x092C, 0x092D, 0x092E },
};

/* varga の見出し（行インジケータに出す ―― 日本語の「あ」に当たる）。
   ★無気無声（index 0）を代表にする ―― varnamala の頭。 */
static const unsigned short DEVA_VARGA_HEAD[5] = { 0x0915, 0x091A, 0x091F, 0x0924, 0x092A };

/* 非 varga 層（L3 で入る）。LT 離し = 半母音 / LT 押し = 摩擦音 + ह。
   ★D-pad ↑→↓← が index 0..3。 */
static const unsigned short DEVA_SEMIVOWEL[4] = { 0x092F, 0x0930, 0x0932, 0x0935 };
static const unsigned short DEVA_SIBILANT[4]  = { 0x0936, 0x0937, 0x0938, 0x0939 };

/* 面ボタン（vowel index: 0=RB / 1=□左 / 2=△上 / 3=○右 / 4=✕下）→ 母音。
   ★**matra を先に試し、子音が無ければ独立母音**に落ちる（Composer が決める）。
   0 = そのボタンに母音は無い。 */
static const unsigned short DEVA_VOWEL_INDEP[5] = { 0x0913, 0x090F, 0x0905, 0x0907, 0x0909 };
static const unsigned short DEVA_VOWEL_MATRA[5] = { 0x094B, 0x0947, 0x093E, 0x093F, 0x0941 };
/* LT 同時押しの拡張（面ボタンが足りないぶんを補う）。 */
static const unsigned short DEVA_VOWEL_INDEP_LT[5] = { 0, 0, 0, 0, 0x090B };
static const unsigned short DEVA_VOWEL_MATRA_LT[5] = { 0, 0, 0, 0, 0x0943 };

/* 短 → 長（RS → の post-shift）。{短, 長} の対。 */
static const unsigned short DEVA_VOWEL_LONG[6][2] = {
    { 0x0905, 0x0906 },
    { 0x0907, 0x0908 },
    { 0x0909, 0x090A },
    { 0x090B, 0x0960 },
    { 0x090F, 0x0910 },
    { 0x0913, 0x0914 },
};
static const unsigned short DEVA_MATRA_LONG[5][2] = {
    { 0x093F, 0x0940 },
    { 0x0941, 0x0942 },
    { 0x0943, 0x0944 },
    { 0x0947, 0x0948 },
    { 0x094B, 0x094C },
};

#endif
