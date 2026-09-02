/* GamepadEngine の状態機械の C 移植(labo web/src/gamepad/machine.ts + ops.ts)。
 *
 * 決定的な純核のみ。ボタン読み取り・時間の供給・バッファ管理はホスト側。
 * ゴールデン: native/test_input（ホストビルド）。★2026-09-02 まで**この行だけ
 *   あって test_input は存在しなかった** —— 韓国語の合成ぶんを置いた。
 *   日本語・英語は台本（*.script）が画面ごと見ている。
 * ★移植元(TS)と同じ挙動を 2 か所で持つことになるため、直すときは両方を見ること。
 */
#ifndef GP_INPUT_H
#define GP_INPUT_H

/* ★GP_ENG_DOUBLE_TAP_MS は **GIME に合わせて 400**（labo の web 版は 300）。
   二度押しの窓が狭いと SmartCaps が出しにくく、しかも**長押しに失敗した短押しが
   2 つ続くと SmartCaps になる**ので、狭いほど「効いたり効かなかったり」に見える。
   ★上流が 2 つあって値が違う（GIME 400 / web 300）。実機で打つ人が GIME を
     基準にしているので、こちらは GIME に揃える。 */
/* ★GP_ENG_LONG_PRESS_MS は **400**（GIME / labo web は 500）。
   実機で「▲ が出るまで待たされる」と言われて 300 まで縮めたら、今度は
   **単押しが ▲ になった**（2026-09-01）—— 人の「叩く」が 300ms を越えていた。
   ★★これが唯一残る時間判定なので、ここだけは余裕を持たせる。
     ★台本の「短押し」も 400ms を割るように書くこと（20 field = 340ms は短押し、
     30 field = 510ms は長押し）。**閾値を動かすと台本の「短い」の意味も動く。**
   ★二度押しの窓（GP_ENG_DOUBLE_TAP_MS）は**廃止した** —— 下の GpMachine の注記。 */
enum { GP_CHORD_WINDOW_MS = 300, GP_ENG_LONG_PRESS_MS = 400 };

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
    /* 英語のシフト。★**3 つは同時に立たない**（下の巡回で必ず 1 つだけになる）——
       立てるようにしていたら、△ が字で解けた瞬間に ⇧ が現れる、という形で実機に出た。 */
    int engShiftNext, engCapsLock, engSmartCaps, engLTHolding, engLTPressTime;
    /* 韓国語。★korAllReleased は**初期値が真** —— 何も押していない状態から
       D-pad を押したら받침ではなく音節の始まり、という区別に要る（machine.ts と同じ）。 */
    int korAllReleased, korPatchimRollback;
} GpMachine;

/* 言語（席）。★もとは `int english` の真偽値だったが、韓国語で 3 つ目が来たので
   enum に昇格させた。main.c 側の `english` は段階 4 で片づける。 */
enum { GP_JA = 0, GP_EN = 1, GP_KO = 2 };

/* 韓国語の 3 つは**字ではなく指示**（合成は korean.c が持つ）——
   同じ音節でも「新しく置く / 받침を足す / 母音を替える」で意味が違うため。 */
enum { GPA_KANA, GPA_YOUON, GPA_KO_SYLLABLE, GPA_KO_PATCHIM, GPA_KO_ADD_I };

typedef struct {
    int type;
    unsigned short text[2];  /* GPA_KANA のみ */
    int tlen;
    int replace;
    /* 韓国語（GPA_KO_*）。revert = いまの받침を戻してから付け直す */
    int onset, nucleus, coda, revert;
} GpAction;

void gp_init(GpMachine *s);
/* 1 フレームを処理してアクションを out に詰める(最大 4)。戻り値 = 個数。
 * 呼び出し後にホストが gp_sync_prev を呼ぶこと(resolver.stepFrame と同じ順)。 */
int gp_step(GpMachine *s, int lang, const GpFrame *f, GpAction out[4]);
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
