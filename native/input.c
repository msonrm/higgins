/* GamepadEngine 状態機械の C 移植。挙動は machine.ts / ops.ts と同一(変更しない)。 */
#include "input.h"
#include "tables.h"
#include "korean_tables.h"

/* RT 連打サイクル: 1打=ん, 2打=を, 3打=んを, 4打で ん へ戻る */
static const unsigned short RT_CYCLE[3][2] = {{0x3093, 0}, {0x3092, 0}, {0x3093, 0x3092}};
static const int RT_CYCLE_LEN[3] = {1, 1, 2};

void gp_init(GpMachine *s)
{
    static const GpMachine zero;
    *s = zero;
    s->prevVowelIndex = -1;
    s->korAllReleased = 1;      /* ★真から始める（machine.ts の初期値） */
}

void gp_sync_prev_kana(GpMachine *s, const GpFrameKana *f)
{
    s->prevConsonantCount = f->consonantCount;
    s->prevVowelPressed = f->vowelNow;
    s->prevRow = f->row;
    s->prevVowelIndex = f->vowel;
    s->prevLT = f->ltNow;
    s->prevRT = f->rtNow;
}

void gp_consume_rt(GpMachine *s) { s->rtUsed = 1; }
void gp_break_rt_cycle(GpMachine *s) { s->rtCycleStep = 0; }
unsigned short gp_row_char(int row) { return (row >= 0 && row < 10) ? GP_KANA[row][0] : 0x3042; }
int gp_row_chars_en(int row, unsigned short *out, int max)
{
    if (row < 0 || row >= 10)
        row = 0;
    int n = 0;
    for (int v = 1; v < 5 && n < max; v++)     /* 面ボタン（左・上・右・下）の順 */
        if (GP_ENG[row][v])
            out[n++] = GP_ENG[row][v];
    return n;
}

unsigned short gp_cell(int english, int row, int vowel)
{
    if (row < 0 || row >= 10 || vowel < 0 || vowel >= 5)
        return 0;
    return english ? GP_ENG[row][vowel] : GP_KANA[row][vowel];
}

static void push_kana1(GpAction *a, unsigned short c, int replace)
{
    a->type = GPA_KANA;
    a->text[0] = c;
    a->text[1] = 0;
    a->tlen = 1;
    a->replace = replace;
}

static int step_japanese(GpMachine *s, const GpFrameKana *f, GpAction out[4])
{
    int n = 0;
    int v = f->vowel < 0 ? 0 : f->vowel;

    if (f->vowelNow) {
        unsigned short ch = (f->row >= 0 && f->row < 10) ? GP_KANA[f->row][v] : 0;
        if (ch) {
            int rowChanged = f->row != s->prevRow;
            int vowelChanged = f->vowel != s->prevVowelIndex;
            if (!s->prevVowelPressed) {
                push_kana1(&out[n++], ch, 0);
                s->eagerSet = 1;
                s->eagerCharLen = 1;
                s->eagerTime = f->now;
                s->rtCycleStep = 0;
            } else if (rowChanged || vowelChanged) {
                int consonantReleased = rowChanged && f->consonantCount < s->prevConsonantCount;
                if (!consonantReleased) {
                    int rep = (s->eagerSet && (f->now - s->eagerTime) < GP_CHORD_WINDOW_MS)
                        ? s->eagerCharLen : 0;
                    push_kana1(&out[n++], ch, rep);
                    s->eagerSet = 1;
                    s->eagerCharLen = 1;
                    s->eagerTime = f->now;
                    s->rtCycleStep = 0;
                }
            }
        }
    }
    if (s->prevVowelPressed && !f->vowelNow)
        s->eagerSet = 0;

    /* LT: 拗音後置シフト / LT+RT = っ */
    if (f->ltNow && !s->prevLT)
        s->rtDuringLT = 0;
    if (f->ltNow && f->rtNow) {
        s->rtDuringLT = 1;
        s->rtUsed = 1;
    }
    if (!f->ltNow && s->prevLT) {
        if (s->rtDuringLT)
            push_kana1(&out[n++], 0x3063 /* っ */, 0);
        else
            out[n++].type = GPA_YOUON;
        s->rtDuringLT = 0;
        s->rtCycleStep = 0;
    }

    /* RT 単押し → 連打サイクル */
    if (f->rtNow && !s->prevRT)
        s->rtUsed = 0;
    if (!f->rtNow && s->prevRT) {
        if (!s->rtUsed && !f->ltNow && !s->prevLT) {
            int prevStep = s->rtCycleStep;
            int nextStep = prevStep >= 3 ? 1 : prevStep + 1;
            GpAction *a = &out[n++];
            a->type = GPA_KANA;
            a->text[0] = RT_CYCLE[nextStep - 1][0];
            a->text[1] = RT_CYCLE[nextStep - 1][1];
            a->tlen = RT_CYCLE_LEN[nextStep - 1];
            a->replace = prevStep == 0 ? 0 : RT_CYCLE_LEN[prevStep - 1];
            s->rtCycleStep = nextStep;
        } else {
            s->rtCycleStep = 0;
        }
        s->rtUsed = 0;
    }
    return n;
}

static int is_alpha(unsigned short c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int step_english(GpMachine *s, const GpFrameKana *f, GpAction out[4])
{
    int n = 0;
    int v = f->vowel < 0 ? 0 : f->vowel;

    if (f->vowelNow) {
        unsigned short ch = (f->row >= 0 && f->row < 10) ? GP_ENG[f->row][v] : 0;
        if (ch) {
            if (s->engCapsLock || s->engSmartCaps || s->engShiftNext) {
                if (ch >= 'a' && ch <= 'z')
                    ch -= 0x20;
                if (s->engShiftNext)
                    s->engShiftNext = 0;
                /* ★SmartCaps は英字以外で解ける。★**CapsLock は落とさない** ——
                   明示して掛けた固定は、明示した操作（もう一度の長押し）でしか解けない
                   のが筋（GIME はそうしている。labo の web 版は CapsLock まで落とす）。 */
                if (s->engSmartCaps && !is_alpha(ch))
                    s->engSmartCaps = 0;
            }
            int rowChanged = f->row != s->prevRow;
            int vowelChanged = f->vowel != s->prevVowelIndex;
            if (!s->prevVowelPressed) {
                push_kana1(&out[n++], ch, 0);
                s->eagerSet = 1;
                s->eagerCharLen = 1;
                s->eagerTime = f->now;
            } else if (rowChanged || vowelChanged) {
                int consonantReleased = rowChanged && f->consonantCount < s->prevConsonantCount;
                if (!consonantReleased) {
                    int rep = (s->eagerSet && (f->now - s->eagerTime) < GP_CHORD_WINDOW_MS)
                        ? s->eagerCharLen : 0;
                    push_kana1(&out[n++], ch, rep);
                    s->eagerSet = 1;
                    s->eagerCharLen = 1;
                    s->eagerTime = f->now;
                }
            }
        }
    }
    if (s->prevVowelPressed && !f->vowelNow)
        s->eagerSet = 0;

    /* LT: **単押しで巡回**（なし → ⇧ 1 字だけ → △ 続く → なし）/ 長押しで ▲ をトグル。
     *
     * ★**二度押しの時間窓はやめた**（実機の指摘・2026-09-01）。理由が 3 つ揃った:
     *   - ゲームパッドには**見えるシフトキーが無い**ので、時間で二度押しを狙えない
     *   - ★**△ と ⇧ に抜け道が無かった** —— ▲ だけが長押しで解除でき、
     *     △ は空白か記号を打つまで、⇧ は次の字を打つまで消せなかった
     *   - ★**同時に立ちうる**ので、△ が字で解けた瞬間に ⇧ が現れることがあった
     * ★巡回なら**どの状態からも単押しだけで抜けられ**、しかも 1 つしか立たない。
     *   ソフトウェアキーボードのシフトキー（叩くたび 一時 → 固定 → 解除）と同じ形。
     * ★★これは GIME / labo web の**どちらとも違う**（あちらは単押し / 二度押し /
     *   長押しの 3 通り）。**画面のキーではなく肩ボタンだから**、という理由で分けた。
     */
    if (f->ltNow && !s->prevLT) {
        s->engLTPressTime = f->now;
        s->engLTHolding = 1;
    }
    if (f->ltNow && s->engLTHolding && (f->now - s->engLTPressTime) >= GP_ENG_LONG_PRESS_MS) {
        s->engCapsLock = !s->engCapsLock;   /* 押している最中に発火する（印がすぐ出る） */
        s->engSmartCaps = 0;
        s->engShiftNext = 0;
        s->engLTHolding = 0;                /* 1 回だけ */
    }
    if (!f->ltNow && s->prevLT) {
        if (s->engLTHolding) {              /* 閾値未到達 = 単押し */
            if (s->engCapsLock) {           /* ▲ → なし（長押しでなくても降りられる） */
                s->engCapsLock = 0;
            } else if (s->engSmartCaps) {   /* △ → なし */
                s->engSmartCaps = 0;
            } else if (s->engShiftNext) {   /* ⇧ → △ */
                s->engShiftNext = 0;
                s->engSmartCaps = 1;
            } else {                        /* なし → ⇧ */
                s->engShiftNext = 1;
            }
        }
        s->engLTHolding = 0;
    }

    /* RT: "0" */
    if (f->rtNow && !s->prevRT)
        s->rtUsed = 0;
    if (!f->rtNow && s->prevRT) {
        if (!s->rtUsed && !f->ltNow)
            push_kana1(&out[n++], '0', 0);
        s->rtUsed = 0;
    }
    return n;
}

/* 韓国語: ハングルの合成入力（labo web の stepKorean の移植）。
 *
 * 左手＝子音行 + 右手＝母音の同時押しで 1 音節。★**母音を伴わない D-pad は받침**
 * になる（2 벌식 の作法）。LT は ㅇ받침 —— 行 0（中立 = 何も押していない）は
 * D-pad では表せないため。
 * ★合成そのものは korean.c が持つ。ここは「何をするか」を決めるだけ。
 */
static int step_korean(GpMachine *s, const GpFrameKana *f, GpAction out[4])
{
    int n = 0;
    const int v = f->vowel >= 0 ? f->vowel : 0;
    const int dpad = f->consonantCount > 0;
    const int prevDpad = s->prevConsonantCount > 0;

    /* ★★母音が来たら直前の받침を取り消す（GIME の「받침巻き戻し」）。
       子音（LB / D-pad）が母音より先に着くと**いったん받침として適用される**ので、
       母音の到着で「あれは新しい音節の頭だった」と分かる ——
       実機の指摘（2026-09-02）: 「마」のあとに ㅁ の行 + 母音を打つと
       **맘마** になっていた（正しくは 마마）。
       ★★**web の stepKorean にはこの処理が無い**（`revertCoda` は
         korean-composer.ts に**定義だけあって一度も呼ばれていない**）。
         GIME の Swift / Kotlin にしかなかった —— 移植元を web にしたので落ちていた。
       ★겹받침のときは巻き戻さない、という判断は korean.c が持つ（あちらが
         直前の받침を覚えている）。ここは「母音が来た」ことだけを伝える。 */
    if (f->vowelNow && s->korPatchimRollback) {
        if (n < 4) {
            out[n].type = GPA_KO_REVERT;
            out[n].replace = 1;
            n++;
        }
        s->korPatchimRollback = 0;
    }

    if (f->vowelNow) {
        const int onset = KO_ONSET_FOR_ROW[f->row >= 0 && f->row < 10 ? f->row : 0];
        const int nucleus = f->rtNow ? KO_NUCLEUS_SHIFTED[v] : KO_NUCLEUS_BASE[v];
        const int rowChanged = f->row != s->prevRow;
        const int vowelChanged = f->vowel != s->prevVowelIndex;
        int emit = 0, replace = 0;

        if (!s->prevVowelPressed) {
            emit = 1;
        } else if (rowChanged || vowelChanged) {
            /* ★子音を離しての行変化は「打ち直し」ではないので出さない（日本語と同じ判断） */
            const int released = rowChanged && f->consonantCount < s->prevConsonantCount;
            if (!released) {
                emit = 1;
                replace = s->eagerSet && (f->now - s->eagerTime) < GP_CHORD_WINDOW_MS;
            }
        }
        if (emit && n < 4) {
            out[n].type = GPA_KO_SYLLABLE;
            out[n].onset = onset;
            out[n].nucleus = nucleus;
            out[n].replace = replace;
            n++;
            s->eagerSet = 1;
            s->eagerTime = f->now;
            s->korAllReleased = 0;
            s->korPatchimRollback = 0;
            if (f->rtNow)
                s->rtUsed = 1;
        }
    }
    if (s->prevVowelPressed && !f->vowelNow)
        s->eagerSet = 0;

    if (!dpad && !f->vowelNow && !f->ltNow) {      /* 全部離した */
        s->korAllReleased = 1;
        s->korPatchimRollback = 0;
    }
    if (dpad && f->consonantCount < s->prevConsonantCount && s->korPatchimRollback)
        s->korPatchimRollback = 0;

    if (!f->vowelNow && dpad) {
        if (!prevDpad && s->korAllReleased && n < 4) {          /* 新しく押した → 받침 */
            out[n].type = GPA_KO_PATCHIM;
            out[n].coda = KO_CODA_FOR_ROW[f->row >= 0 && f->row < 10 ? f->row : 0];
            n++;
            s->korPatchimRollback = 1;
            s->korAllReleased = 0;
        } else if (f->row != s->prevRow && s->korPatchimRollback && n < 4) {
            /* ★押したまま行を変えた → 戻してから付け直す（押し間違いの直し） */
            out[n].type = GPA_KO_PATCHIM;
            out[n].coda = KO_CODA_FOR_ROW[f->row >= 0 && f->row < 10 ? f->row : 0];
            out[n].revert = 1;
            n++;
        }
    }

    /* LT = ㅇ받침（行 0 は D-pad では押せない） */
    if (f->ltNow && !s->prevLT && !f->vowelNow && !dpad && s->korAllReleased && n < 4) {
        out[n].type = GPA_KO_PATCHIM;
        out[n].coda = KO_CODA_FOR_ROW[0];
        n++;
        s->korPatchimRollback = 1;     /* ★こちらも巻き戻せる（GIME と同じ） */
        s->korAllReleased = 0;
    }

    /* RT = 単押しのリリースで ㅣ 付加の複合母音（押しながらは母音の層の切替） */
    if (f->rtNow && !s->prevRT)
        s->rtUsed = 0;
    if (!f->rtNow && s->prevRT) {
        if (!s->rtUsed && !f->ltNow && n < 4)
            out[n++].type = GPA_KO_ADD_I;
        s->rtUsed = 0;
    }
    return n;
}

int gp_step_kana(GpMachine *s, int lang, const GpFrameKana *f, GpAction out[4])
{
    for (int i = 0; i < 4; i++) {
        static const GpAction zero;
        out[i] = zero;                  /* ★毎回ゼロにする（前の指示が残ると黙って効く） */
    }
    switch (lang) {
    case GP_EN: return step_english(s, f, out);
    case GP_KO: return step_korean(s, f, out);
    default:    return step_japanese(s, f, out);
    }
}

/* ---- ops.ts: 合成末尾への解決 ---- */

int gp_resolve_youon(const unsigned short *tail, int n, unsigned short out[2], int *replace)
{
    if (n == 0)
        return 0;
    unsigned short last = tail[n - 1];
    for (int i = 0; i < GP_YOUON_N; i++) {
        if (GP_YOUON[i].from == last) {
            out[0] = GP_YOUON[i].to[0];
            out[1] = GP_YOUON[i].to[1];
            *replace = 1;
            return GP_YOUON[i].to[1] ? 2 : 1;
        }
    }
    out[0] = 0x3063;                   /* 対象外 → っ を追加 */
    *replace = 0;
    return 1;
}

/* 1 字を 清音→濁音→半濁音→清音 のサイクルで次へ。非対象は 0。 */
static unsigned short cycle_dakuten(unsigned short c)
{
    for (int i = 0; i < GP_HANDAKU_N; i++)             /* 半濁音 → 清音 */
        if (GP_HANDAKU[i].on == c)
            return GP_HANDAKU[i].sei;
    for (int i = 0; i < GP_DAKU_N; i++)                /* 濁音 → 半濁音 or 清音 */
        if (GP_DAKU[i].on == c) {
            unsigned short sei = GP_DAKU[i].sei;
            for (int k = 0; k < GP_HANDAKU_N; k++)
                if (GP_HANDAKU[k].sei == sei)
                    return GP_HANDAKU[k].on;
            return sei;
        }
    for (int i = 0; i < GP_DAKU_N; i++)                /* 清音 → 濁音 */
        if (GP_DAKU[i].sei == c)
            return GP_DAKU[i].on;
    return 0;
}

int gp_resolve_dakuten(const unsigned short *tail, int n, unsigned short out[2], int *replace)
{
    if (n == 0)
        return 0;
    unsigned short last = tail[n - 1];
    /* 末尾が小書き ゃゅょ → 直前の基字に適用し、小書きは残して 2 字差し替え */
    if ((last == 0x3083 || last == 0x3085 || last == 0x3087) && n >= 2) {
        unsigned short cycled = cycle_dakuten(tail[n - 2]);
        if (!cycled)
            return 0;
        out[0] = cycled;
        out[1] = last;
        *replace = 2;
        return 2;
    }
    unsigned short cycled = cycle_dakuten(last);
    if (!cycled)
        return 0;
    out[0] = cycled;
    *replace = 1;
    return 1;
}

/* ---- デーヴァナーガリー ----
 *
 * ★★**席によってスティックの役が違う。** かなの席は LS がキャレット移動だが、
 *   ここでは **LS が varga（子音群）を選ぶ**。RS も母音・修飾子・backspace・句読点で
 *   埋まるので、キャレット移動は **RT + LS** へ退避している（GIME の実機判断）。
 *   ★**頻度で決まっている** —— 実測でヒンディー語の子音 128 個のうち varga の切替が
 *   45 回・非 varga が 44 個（34%）。入れ替えると**子音の 69% で RT を噛む**。
 *
 * ★**RT は「修飾の入口」**で、押している間の LS / L3 は別の意味を持ち、
 *   ★**何にも使われずに離されたときだけ** halant になる。
 *   その判定（rtUsedForCursor）は**ここが持つ**が、カーソル移動と改行そのものは
 *   席に依存しない操作なので main.c が持つ（呼んだら deva_consume_rt を呼ぶこと）。
 *
 * ★★**varga の latch は「前置シフト」** —— 左手側の出力（D-pad の子音 / LB の鼻音）を
 *   1 つ出したら **latch は中立（प varga）へ戻る**。次の子音は再度 LS を倒して指定する。
 *   ★**押しっぱなしにできないから latch なのであって、モードではない**（左親指で
 *     LS と D-pad を同時に操作できない、という物理制約から来ている）。
 *   ★★**移植で 2026-09-03 までこの消費が落ちていた** —— GIME は Swift も Kotlin も
 *     子音 emission と鼻音 emission の 2 か所で `devaLsDir = .neutral` を書いている。
 *     ★しかも `test_input.c` に「latch は残る」というゴールデンを**実装を追って**
 *     書いてしまっていたので、**差が仕様として固定され、検査は緑のままだった**。
 *   ★`prevLsDir` は触らない（GIME と同じ）—— 同方向へ latch し直すには、
 *     一度 LS を中立へ戻してから倒す。L3 が latch を落とすのも同じ形。
 */
#include "devanagari_tables.h"

void deva_machine_init(DevaMachine *s)
{
    static const DevaMachine zero;
    *s = zero;
    s->prevVowel = -1;
    s->prevDpad = -1;
}

void deva_consume_rt(DevaMachine *s) { s->rtUsedForCursor = 1; }

/* LS の向き（0=中立 1=↑ 2=→ 3=↓ 4=←）→ varga の番号。
   ★★**並びが違う。** varnamala を時計回りに載せてあるので
     ↑=क(0) →=च(1) ↓=ट(2) ←=त(3) **中立=प(4)** ——
     「中立が 0」ではない（LS を倒さない状態にいちばん頻度の高い प を置くため）。 */
static int varga_of(int lsDir)
{
    switch (lsDir) {
    case 1:  return 0;                 /* ↑ = कवर्ग */
    case 2:  return 1;                 /* → = चवर्ग */
    case 3:  return 2;                 /* ↓ = टवर्ग */
    case 4:  return 3;                 /* ← = तवर्ग */
    default: return 4;                 /* 中立 = पवर्ग */
    }
}

unsigned short deva_head_char(const DevaMachine *s, int ltNow)
{
    if (s->nonVarga)
        return ltNow ? DEVA_SIBILANT[0] : DEVA_SEMIVOWEL[0];
    return DEVA_VARGA_HEAD[varga_of(s->lsLatch)];
}

static int deva_push(GpAction out[4], int n, int op, unsigned short ch, unsigned short alt)
{
    if (n >= 4)
        return n;
    out[n].type = GPA_DEVA;
    out[n].deva_op = op;
    out[n].deva_ch = ch;
    out[n].deva_alt = alt;
    return n + 1;
}

int gp_step_deva(DevaMachine *s, const GpFrameDeva *f, GpAction out[4])
{
    int n = 0;
    for (int i = 0; i < 4; i++) {
        static const GpAction zero;
        out[i] = zero;                 /* ★毎回ゼロにする（前の指示が残ると黙って効く） */
    }

    /* ★RT の押下エッジで「カーソルに使った」を先に落とす —— LS の処理より前に
       やることで、同じフレームで RT press と LS edge が起きても順序が合う
       （GIME の注記。実機で踏んだ順序の問題）。 */
    if (f->rtNow && !s->prevRT)
        s->rtUsedForCursor = 0;

    /* --- LS: RT 押下中はキャレット移動 / それ以外は varga の latch --- */
    if (f->lsDir && f->lsDir != s->prevLsDir) {
        if (f->rtNow) {
            /* ★横は -1/+1、縦は -2/+2。★**ここで「RT を使った」を立てる** ——
               立てないと、離したときに halant が出る。 */
            static const short DIR[5] = { 0, -2, 1, 2, -1 };   /* 中立/↑/→/↓/← */
            n = deva_push(out, n, DEVA_OP_CURSOR, (unsigned short)DIR[f->lsDir], 0);
            s->rtUsedForCursor = 1;
        } else {
            s->lsLatch = (s->lsLatch == f->lsDir) ? 0 : f->lsDir;
        }
    }

    /* --- L3: RT 押下中は改行 / それ以外は非 varga 層 --- */
    if (f->l3Now && !s->prevL3 && f->rtNow) {
        n = deva_push(out, n, DEVA_OP_COMMIT, 0, 0);
        s->rtUsedForCursor = 1;
    } else if (f->l3Now && !s->prevL3) {
        s->nonVarga = !s->nonVarga;
        /* ★latch も落とす —— 押し込みながら傾けた方向が次の click まで残る、
           という事故を防ぐ（GIME の注記）。 */
        s->lsLatch = 0;
    }

    /* --- LT + RT の同時押し: visarga。★halant を抑止する --- */
    if (f->ltNow && f->rtNow && !(s->prevLT && s->prevRT)) {
        n = deva_push(out, n, DEVA_OP_VISARGA, 0, 0);
        s->rtUsedForCursor = 1;
    }

    /* --- D-pad: 子音 --- */
    if (f->dpad >= 0 && f->dpad != s->prevDpad) {
        unsigned short c;
        if (s->nonVarga) {
            c = f->ltNow ? DEVA_SIBILANT[f->dpad] : DEVA_SEMIVOWEL[f->dpad];
            /* ★**1 子音で自動 OFF**（one-shot）。連続して非 varga を打つなら
               もう一度 L3 を押す（GIME の実機判断）。 */
            s->nonVarga = 0;
        } else {
            c = DEVA_VARGA[varga_of(s->lsLatch)][f->dpad];
        }
        n = deva_push(out, n, DEVA_OP_CONSONANT, c, 0);
        s->lsLatch = 0;                /* ★前置シフト（下の注記）。非 varga でも消す */
    }

    /* --- LB: その varga の鼻音。★非 varga 層とは独立（GIME の注記）--- */
    if (f->lbNow && !s->prevLb) {
        n = deva_push(out, n, DEVA_OP_CONSONANT, DEVA_VARGA[varga_of(s->lsLatch)][4], 0);
        s->lsLatch = 0;                /* ★前置シフト（下の注記） */
    }

    /* --- 面ボタン（RB を含む 0..4）: 母音。
           ★matra と独立母音の**両方**を渡す —— どちらになるかは合成器が決める。
           ★vowel==0（RB）は LT と組むと nukta（母音ではない）。 --- */
    if (f->vowel >= 0 && f->vowel != s->prevVowel) {
        if (f->vowel == 0 && f->ltNow) {
            n = deva_push(out, n, DEVA_OP_NUKTA, 0, 0);
        } else {
            const unsigned short m = f->ltNow && DEVA_VOWEL_MATRA_LT[f->vowel]
                                     ? DEVA_VOWEL_MATRA_LT[f->vowel]
                                     : DEVA_VOWEL_MATRA[f->vowel];
            const unsigned short v = f->ltNow && DEVA_VOWEL_INDEP_LT[f->vowel]
                                     ? DEVA_VOWEL_INDEP_LT[f->vowel]
                                     : DEVA_VOWEL_INDEP[f->vowel];
            if (m || v)
                n = deva_push(out, n, DEVA_OP_VOWEL, m, v);
        }
    }

    /* --- RS↑: anusvara ↔ chandrabindu / RS→: 長母音への昇格 --- */
    if (f->rsUpNow && !s->prevRsUp)
        n = deva_push(out, n, DEVA_OP_ANUSVARA, 0, 0);
    if (f->rsRightNow && !s->prevRsRight)
        n = deva_push(out, n, DEVA_OP_LONG, 0, 0);

    /* --- RT を離した: 何にも使われていなければ halant --- */
    if (!f->rtNow && s->prevRT) {
        if (!s->rtUsedForCursor)
            n = deva_push(out, n, DEVA_OP_HALANT, 0, 0);
        s->rtUsedForCursor = 0;
    }
    return n;
}

void gp_sync_prev_deva(DevaMachine *s, const GpFrameDeva *f)
{
    s->prevLsDir = f->lsDir;
    s->prevDpad = f->dpad;
    s->prevLb = f->lbNow;
    s->prevVowel = f->vowel;
    s->prevLT = f->ltNow;
    s->prevRT = f->rtNow;
    s->prevL3 = f->l3Now;
    s->prevRsUp = f->rsUpNow;
    s->prevRsRight = f->rsRightNow;
}
