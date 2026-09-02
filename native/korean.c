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

void ko_init(KoComposer *k) { k->onset = k->nucleus = k->coda = -1; }

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
    *out = compose(onset, nucleus, -1);
    return 1;
}

/* 받침を足す。★既に받침があれば겹받침を試み、成立しなければ**何も出さない**
   （TS の inputPatchim が null を返すのと同じ —— 無理に上書きしない）。 */
static int patchim(KoComposer *k, int coda, uint16_t *out, int *replace)
{
    if (!ko_composing(k))
        return 0;
    if (k->coda >= 0) {
        for (int i = 0; i < KO_DOUBLE_CODA_N; i++)
            if (KO_DOUBLE_CODA[i][0] == k->coda && KO_DOUBLE_CODA[i][1] == coda) {
                k->coda = KO_DOUBLE_CODA[i][2];
                *out = compose(k->onset, k->nucleus, k->coda);
                *replace = 1;
                return 1;
            }
        return 0;
    }
    k->coda = coda;
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
           （stepKorean が revert を立てて知らせる）。差し替えは 1 字のまま。 */
        if (a->revert)
            k->coda = -1;
        return patchim(k, a->coda, out, replace);
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
