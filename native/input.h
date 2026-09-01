/* GamepadEngine の状態機械の C 移植(labo web/src/gamepad/machine.ts + ops.ts)。
 *
 * 決定的な純核のみ。ボタン読み取り・時間の供給・バッファ管理はホスト側。
 * ゴールデン: labo の 35 件を test_input(ホストビルド)で照合する。
 * ★移植元(TS)と同じ挙動を 2 か所で持つことになるため、直すときは両方を見ること。
 */
#ifndef GP_INPUT_H
#define GP_INPUT_H

/* ★GP_ENG_DOUBLE_TAP_MS は **GIME に合わせて 400**（labo の web 版は 300）。
   二度押しの窓が狭いと SmartCaps が出しにくく、しかも**長押しに失敗した短押しが
   2 つ続くと SmartCaps になる**ので、狭いほど「効いたり効かなかったり」に見える。
   ★上流が 2 つあって値が違う（GIME 400 / web 300）。実機で打つ人が GIME を
     基準にしているので、こちらは GIME に揃える。
   ★GP_ENG_LONG_PRESS_MS は **300**（GIME / labo web はどちらも 500）。
     実機で「▲ が出るまで待たされる」と言われて縮めた。 */
enum { GP_CHORD_WINDOW_MS = 300, GP_ENG_LONG_PRESS_MS = 300, GP_ENG_DOUBLE_TAP_MS = 400 };

/* 1 フレーム分の派生入力。vowel: -1 = 未選択 */
typedef struct {
    int now;                 /* ms */
    int row;                 /* 子音行 0..9 */
    int vowel;               /* 母音 0..4 / -1 */
    int vowelNow, ltNow, rtNow;
    int consonantCount;
} GpFrame;

typedef struct {
    int prevVowelPressed, prevRow, prevVowelIndex, prevConsonantCount, prevLT, prevRT;
    int eagerSet, eagerCharLen, eagerTime;
    int rtUsed, rtDuringLT, rtCycleStep;
    int engShiftNext, engCapsLock, engSmartCaps, engLTHolding, engLTPressTime, engLastLTRelease;
} GpMachine;

enum { GPA_KANA, GPA_YOUON };

typedef struct {
    int type;
    unsigned short text[2];  /* GPA_KANA のみ */
    int tlen;
    int replace;
} GpAction;

void gp_init(GpMachine *s);
/* 1 フレームを処理してアクションを out に詰める(最大 4)。戻り値 = 個数。
 * 呼び出し後にホストが gp_sync_prev を呼ぶこと(resolver.stepFrame と同じ順)。 */
int gp_step(GpMachine *s, int english, const GpFrame *f, GpAction out[4]);
void gp_sync_prev(GpMachine *s, const GpFrame *f);
/* 拗音後置シフト / 濁点トグルを合成末尾に解決する。
 * 戻り値 = 出力長(0 = 無反応)。*replace = 末尾から差し替える字数。 */
int gp_resolve_youon(const unsigned short *tail, int n, unsigned short out[2], int *replace);
int gp_resolve_dakuten(const unsigned short *tail, int n, unsigned short out[2], int *replace);
/* RT を「使用済み」にする(リリース時の「ん」を抑止。resolver.consumeRt 相当) */
void gp_consume_rt(GpMachine *s);
/* RT 連打サイクルを断つ(resolver.action 相当 — スティック等の出力前に呼ぶ) */
void gp_break_rt_cycle(GpMachine *s);
/* 行インジケータ用: その行のあ段の字。 */
unsigned short gp_row_char(int row);
/* 英語の行インジケータ: その行の**面ボタンの字を全部**（3〜4 字。`abc` / `pqrs`）。
   ★1 字では**インジケータとして機能しない**（実機の指摘・2026-09-01）—— 押す前に
     「この行に何があるか」が見えないと支えにならない。
   ★列 0（R1 = 数字）は入れない —— あれは行によらず 1 つのキーなので覚えられる。
   戻り値 = 書いた字数。 */
int gp_row_chars_en(int row, unsigned short *out, int max);
/* 入力表の 1 マス(row 0..9 / vowel 0..4)。0 = そのマスは空。
   ★図を描く側が表を持たないための窓。**写すと、図に出ている字と実際に入る字が
     ずれても誰も気づけない**(web 版で踏んだ事故) */
unsigned short gp_cell(int english, int row, int vowel);

#endif
