#include "korean.h"
#include "korean_tables.h"

static uint16_t compose(int onset, int nucleus, int coda)
{
    return (uint16_t)(0xAC00 + (onset * 21 + nucleus) * 28 + (coda < 0 ? 0 : coda));
}

static int lookup(const unsigned char (*tbl)[2], int n, int from)
{
    for (int i = 0; i < n; i++)
        if (tbl[i][0] == from)
            return tbl[i][1];
    return -1;
}

uint16_t ko_row_char(int row)
{
    return KO_ROW_CHAR[row >= 0 && row < 10 ? row : 0];
}

void ko_init(KoComposer *k)
{
    k->onset = k->nucleus = k->coda = k->rollback_coda = -1;
    k->rollback_pending = 0;
}

void ko_commit(KoComposer *k) { ko_init(k); }

int ko_composing(const KoComposer *k) { return k->onset >= 0 && k->nucleus >= 0; }

uint16_t ko_current(const KoComposer *k)
{
    return ko_composing(k) ? compose(k->onset, k->nucleus, k->coda) : 0;
}

/* 초성 + 중성 で新しい音節を始める（前の音節はそこで確定する）。 */
static int syllable(KoComposer *k, int onset, int nucleus, uint16_t *out)
{
    k->onset = onset;
    k->nucleus = nucleus;
    k->coda = -1;
    k->rollback_coda = -1;
    k->rollback_pending = 0;
    *out = compose(onset, nucleus, -1);
    return 1;
}

/* 받침を足す。★既に받침があれば겹받침を試み、成立しなければ**何も出さない**
   （TS の inputPatchim が null を返すのと同じ —— 無理に上書きしない）。 */
static int patchim(KoComposer *k, int coda, uint16_t *out, int *replace)
{
    if (!ko_composing(k))
        return 0;
    const int before = k->coda;
    if (k->coda >= 0) {
        for (int i = 0; i < KO_DOUBLE_CODA_N; i++)
            if (KO_DOUBLE_CODA[i][0] == k->coda && KO_DOUBLE_CODA[i][1] == coda) {
                k->coda = KO_DOUBLE_CODA[i][2];
                k->rollback_coda = before;
                k->rollback_pending = 1;
                *out = compose(k->onset, k->nucleus, k->coda);
                *replace = 1;
                return 1;
            }
        /* ★★成立しない組み合わせ（앉 に さらに ㅈ 等）。**戻せる状態を落とす** ——
           ここで落とさないと、次に母音が来たときに**前の音節の받침まで戻して**しまう。 */
        k->rollback_pending = 0;
        return 0;
    }
    k->coda = coda;
    k->rollback_coda = before;
    k->rollback_pending = 1;
    *out = compose(k->onset, k->nucleus, k->coda);
    *replace = 1;
    return 1;
}

int ko_apply(KoComposer *k, const GpAction *a, uint16_t *out, int *replace)
{
    *replace = a->replace;
    switch (a->type) {
    case GPA_KO_SYLLABLE:
        return syllable(k, a->onset, a->nucleus, out);
    case GPA_KO_PATCHIM:
        /* ★row を変えたときは、いま付いている받침を**戻してから**付け直す
           （stepKorean が revert を立てて知らせる）。差し替えは 1 字のまま。
           ★戻す先は -1 ではなく `rollback_coda` —— 겹받침を作っていた場合、
             1 つ目の받침まで消してしまうと元に戻せない。
           ★★**戻せるのは `rollback_pending` のときだけ**（2026-09-04・実機の指摘）。
             ★入力層（input.c）は**받침が実際に付いたかを知らない** ―― 指示を出した
             時点でフラグを立てるので、`patchim()` が「成立しない組み合わせ」で
             何も出さなかった場合にずれる。そのまま戻すと **`rollback_coda` が古い**まま
             使われ、**前の音節の받침が消える**。
             ★実測: `당` のあと `신` を打つのに LB と ↑ が**同じフレームに来ないと**
             `다신` になっていた（LB 先でも十字キー先でも同じ）。
             ★★上流（GIME）は `if case .added = inputPatchim { patchimRollbackActive = true }`
             と**成功したときだけ**立てている。層を分けたときにこの結び付きが落ちた
             —— varga の latch で踏んだのと同じ形。 */
        if (a->revert && k->rollback_pending)
            k->coda = k->rollback_coda;
        return patchim(k, a->coda, out, replace);
    case GPA_KO_REVERT:
        /* ★母音が来た = 直前の받침は「新しい音節の頭」だった。
           ★**겹받침も戻す**（戻し先は 1 つ目の받침）。korean.h の注記を参照。 */
        if (!ko_composing(k) || !k->rollback_pending)
            return 0;
        k->coda = k->rollback_coda;
        k->rollback_pending = 0;
        *out = compose(k->onset, k->nucleus, k->coda);
        *replace = 1;
        return 1;
    case GPA_KO_ADD_I: {
        if (!ko_composing(k))
            return 0;
        const int to = lookup(KO_ADD_I, KO_ADD_I_N, k->nucleus);
        if (to < 0)
            return 0;
        k->nucleus = to;
        *out = compose(k->onset, k->nucleus, k->coda);
        *replace = 1;
        return 1;
    }
    default:
        return 0;
    }
}

int ko_cycle(KoComposer *k, uint16_t *out, int *replace)
{
    if (!ko_composing(k))
        return 0;
    /* ★받침があれば받침が先（GIME と同じ順）。無ければ초성。 */
    if (k->coda >= 0) {
        const int to = lookup(KO_CODA_CYCLE, KO_CODA_CYCLE_N, k->coda);
        if (to < 0)
            return 0;
        k->coda = to;
    } else {
        const int to = lookup(KO_ONSET_CYCLE, KO_ONSET_CYCLE_N, k->onset);
        if (to < 0)
            return 0;
        k->onset = to;
    }
    *out = compose(k->onset, k->nucleus, k->coda);
    *replace = 1;
    return 1;
}

int ko_add_aeo(KoComposer *k, uint16_t *out, int *replace)
{
    if (!ko_composing(k))
        return 0;
    const int to = lookup(KO_ADD_AEO, KO_ADD_AEO_N, k->nucleus);
    if (to < 0)
        return 0;
    k->nucleus = to;
    *out = compose(k->onset, k->nucleus, k->coda);
    *replace = 1;
    return 1;
}
