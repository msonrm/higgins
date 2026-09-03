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

/* ---- 席ごとの 1 フレーム ----
 *
 * ★★**構造体を席ごとに分けている**（2026-09-03）。もとは 1 つの `GpFrame` を
 *   3 席で共有していたが、デーヴァナーガリーが来て**同じ `row` が別のものを
 *   指し始めた** —— かなの席では「子音行 0..9」、デーヴァナーガリーでは
 *   「D-pad 4 方向（varga 内の stop）」。★流用すると**名前が嘘になり**、
 *   次に読む人が必ず取り違える。
 * ★韓国語の時点で既に半分そうなっていた（`korAllReleased` などは `GpMachine`
 *   側の専用フィールド）ので、**分けるのは自然な続き**。
 * ★席が増えるたびに構造体が 1 つ増えるが、**増えたことが型に出る**ほうがよい。
 */

/* かなの骨格を共有する席（日本語 / 英語 / 韓国語）。vowel: -1 = 未選択 */
typedef struct {
    int now;                 /* ms */
    int row;                 /* 子音行 0..9 */
    int vowel;               /* 母音 0..4 / -1 */
    int vowelNow, ltNow, rtNow;
    int consonantCount;
} GpFrameKana;

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

/* デーヴァナーガリーの席。★かなと**入力の骨格が違う** —— 子音は
   「LS の latch × D-pad」の**2 次元**で決まり、両スティックが他の役に埋まる。
   ★どの物理入力をどう読むかは main.c（席ごとの解釈）が決め、ここには
     「解釈した結果」だけが来る。 */
typedef struct {
    int now;                 /* ms */
    int lsDir;               /* varga: 0=中立 1=↑ 2=→ 3=↓ 4=← */
    int dpad;                /* varga 内の stop: 0..3 / -1 = 押していない */
    int lbNow;               /* LB = その varga の鼻音 */
    int vowel;               /* 面ボタン 0..4 / -1（0=RB 1=□ 2=△ 3=○ 4=✕） */
    int ltNow, rtNow;
    int l3Now;               /* L3 = 非 varga 層。★RB は vowel==0 として来る */
    int rsUpNow, rsRightNow; /* RS↑ = anusvara / RS→ = 長母音 */
} GpFrameDeva;

/* デーヴァナーガリーの状態。★かなの GpMachine とは別に持つ（席ごとに分ける方針）。
   ★lsLatch は**押しっぱなしではなく latch** —— 左親指で LS と D-pad を同時に
     操作できない物理制約から来ている（GIME の実機判断）。同方向へもう一度で中立。
   ★★**そして latch は「前置シフト」** —— 左手側の出力（D-pad 子音 / LB 鼻音）を
     1 つ出したら中立へ戻る。**モードではない**（gp_step_deva の頭書き）。
   ★rtUsedForCursor: RT は「修飾の入口」で、**何にも使われずに離されたときだけ**
     halant になる（RT+LS のキャレット移動や RT+L3 の改行に使われたら抑止）。 */
typedef struct {
    int lsLatch, nonVarga, rtUsedForCursor;
    int prevLsDir, prevDpad, prevLb, prevVowel;
    int prevLT, prevRT, prevL3, prevRsUp, prevRsRight;
} DevaMachine;

/* 言語（席）。★もとは `int english` の真偽値だったが、韓国語で 3 つ目が来たので
   enum に昇格させた。★デーヴァナーガリーは**別の入口**（gp_step_deva）を通るので、
   ここには並ばない —— 入力の骨格が違うものを同じ switch に入れない。 */
enum { GP_JA = 0, GP_EN = 1, GP_KO = 2 };

/* 韓国語の 3 つは**字ではなく指示**（合成は korean.c が持つ）——
   同じ音節でも「新しく置く / 받침を足す / 母音を替える」で意味が違うため。 */
enum { GPA_KANA, GPA_YOUON, GPA_KO_SYLLABLE, GPA_KO_PATCHIM, GPA_KO_ADD_I,
       GPA_KO_REVERT, GPA_DEVA };

/* デーヴァナーガリーの操作。★韓国語と同じ考えで**字ではなく指示**を渡す ——
   合成（schwa の置換 / 長母音への昇格 / トグル）は devanagari.c が持つ。
   ★DEVA_OP_VOWEL は「matra を先に試し、子音が無ければ独立母音」の 1 手で、
     どちらになるかは**合成器が状態を見て決める**（呼ぶ側は両方を渡すだけ）。 */
enum { DEVA_OP_CONSONANT, DEVA_OP_VOWEL, DEVA_OP_HALANT, DEVA_OP_NUKTA,
       DEVA_OP_ANUSVARA, DEVA_OP_VISARGA, DEVA_OP_LONG, DEVA_OP_CANDRA,
       /* ★キャレット移動と改行も**ここが返す**。席に依存しない操作だが、
          「RT が何に使われたか」の判定（halant を抑止するか）が入力側にあるので、
          main.c に同じ条件をもう一度書くと**二重になって必ずずれる**。
          ch に向き: CURSOR は -1/+1（横）・-2/+2（縦）。 */
       DEVA_OP_CURSOR, DEVA_OP_COMMIT };

typedef struct {
    int type;
    unsigned short text[2];  /* GPA_KANA のみ */
    int tlen;
    int replace;
    /* 韓国語（GPA_KO_*）。revert = いまの받침を戻してから付け直す */
    int onset, nucleus, coda, revert;
    /* デーヴァナーガリー（GPA_DEVA）。op = DEVA_OP_* / ch と alt は op による。
       ★VOWEL は matra（ch）と独立母音（alt）の**両方**を渡す。 */
    int deva_op;
    unsigned short deva_ch, deva_alt;
} GpAction;

void gp_init(GpMachine *s);
/* かなの席の 1 フレームを処理してアクションを out に詰める(最大 4)。戻り値 = 個数。
 * 呼び出し後にホストが gp_sync_prev_kana を呼ぶこと(resolver.stepFrame と同じ順)。
 * ★デーヴァナーガリーは gp_step_deva（別の入口）。 */
int gp_step_kana(GpMachine *s, int lang, const GpFrameKana *f, GpAction out[4]);
void gp_sync_prev_kana(GpMachine *s, const GpFrameKana *f);
/* 拗音後置シフト / 濁点トグルを合成末尾に解決する。
 * 戻り値 = 出力長(0 = 無反応)。*replace = 末尾から差し替える字数。 */
int gp_resolve_youon(const unsigned short *tail, int n, unsigned short out[2], int *replace);
int gp_resolve_dakuten(const unsigned short *tail, int n, unsigned short out[2], int *replace);
/* RT を「使用済み」にする(リリース時の「ん」を抑止。resolver.consumeRt 相当) */
void gp_consume_rt(GpMachine *s);

/* ---- デーヴァナーガリーの席（別の入口）---- */
void deva_machine_init(DevaMachine *s);
int  gp_step_deva(DevaMachine *s, const GpFrameDeva *f, GpAction out[4]);
void gp_sync_prev_deva(DevaMachine *s, const GpFrameDeva *f);
/* ★RT を「カーソルに使った」ことにする —— main.c が RT+LS でキャレットを動かしたら
   これを呼ぶ。呼ばないと**離したときに halant が出る**。 */
void deva_consume_rt(DevaMachine *s);
/* 行インジケータ用: いま選ばれている varga の見出し字（無気無声）。
   ★非 varga 層のときはその層の頭を返す。 */
unsigned short deva_head_char(const DevaMachine *s, int ltNow);
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
