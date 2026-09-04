/* デーヴァナーガリー akshara 合成 —— DevanagariComposer.swift の移植。
 * 規則の正典は Swift。ここは手順だけを写す（表は gen_devanagari.py が焼く）。 */
#include "devanagari.h"
#include "devanagari_tables.h"

enum {
    VIRAMA = 0x094D,       /* ् */
    NUKTA = 0x093C,        /* ़ */
    ANUSVARA = 0x0902,     /* ं */
    CHANDRABINDU = 0x0901, /* ँ */
    VISARGA = 0x0903,      /* ः */
    AA_MATRA = 0x093E,     /* ा */
    CANDRA_O = 0x0949,     /* ॉ ―― 素の子音に付ける candra（डॉक्टर の ड） */
};

int deva_is_consonant(unsigned short c) { return c >= 0x0915 && c <= 0x0939; }
int deva_is_matra(unsigned short c)     { return c >= 0x093E && c <= 0x094C; }
int deva_is_vowel(unsigned short c)
{
    return (c >= 0x0904 && c <= 0x0914) || c == 0x0960 || c == 0x0961;
}

void deva_init(Deva *d)
{
    d->len = 0;
    d->state = DEVA_EMPTY;
    d->lastHasNukta = 0;
}

int deva_is_composing(const Deva *d) { return d->len > 0; }

void deva_commit(Deva *d) { deva_init(d); }

/* ---- 内部 ---- */

static const DevaOut NONE = { { 0, 0 }, 0, 0, 0 };

static DevaOut put(Deva *d, unsigned short c, int replace)
{
    DevaOut o = NONE;
    if (d->len >= DEVA_BUF_MAX)
        return o;              /* ★器が満ちたら黙って伸ばさない（呼ぶ側が確定する） */
    d->buf[d->len++] = c;
    o.text[0] = c;
    o.len = 1;
    o.replace = replace;
    o.ok = 1;
    return o;
}

/* buffer の末尾から状態を計算し直す（backspace / トグル解除のあと）。 */
static DevaState recompute(const Deva *d)
{
    if (d->len == 0)
        return DEVA_EMPTY;
    const unsigned short last = d->buf[d->len - 1];
    if (last == VIRAMA)
        return DEVA_HALANT;
    if (last == ANUSVARA || last == CHANDRABINDU || last == VISARGA)
        return DEVA_MODIFIER;
    if (last == NUKTA)
        return DEVA_CONSONANT;
    if (deva_is_matra(last))
        return DEVA_MATRA;
    if (deva_is_vowel(last))
        return DEVA_VOWEL;
    if (deva_is_consonant(last))
        return DEVA_CONSONANT;
    return DEVA_EMPTY;
}

/* ---- 入力 ---- */

/* 子音。★前の状態にかかわらず単純に足す（auto-conjunct しない）。
   直前が DEVA_HALANT なら virama + 子音 の並びになり、**描画の側で** conjunct になる。 */
DevaOut deva_consonant(Deva *d, unsigned short c)
{
    DevaOut o = put(d, c, 0);
    if (o.ok) {
        d->state = DEVA_CONSONANT;
        d->lastHasNukta = 0;
    }
    return o;
}

/* matra。★子音の後だけ。inherent schwa を置き換えて akshara を閉じる。
   子音が無ければ ok=0 —— 呼ぶ側が独立母音へ落とす。 */
DevaOut deva_matra(Deva *d, unsigned short m)
{
    if (d->state != DEVA_CONSONANT)
        return NONE;
    DevaOut o = put(d, m, 0);
    if (o.ok)
        d->state = DEVA_MATRA;
    return o;
}

/* 独立母音。★常に新しい cluster を始める。 */
DevaOut deva_vowel(Deva *d, unsigned short v)
{
    DevaOut o = put(d, v, 0);
    if (o.ok)
        d->state = DEVA_VOWEL;
    return o;
}

/* 打ち方の図用: 印の字（halant / nukta / visarga / anusvara / chandrabindu / candra）。
 * ★★**合成器が使うのと同じ定数から返す**（`gp_cell` と同じ趣旨の窓）——
 *   図が字を持つと、片方を直したときに**図だけ古くなる**。
 * ★candra は母音で ॉ / ॅ に分かれるので、代表（ॉ）を返す。
 */
unsigned short deva_mark_char(int mark)
{
    switch (mark) {
    case DEVA_MARK_HALANT:  return VIRAMA;
    case DEVA_MARK_NUKTA:   return NUKTA;
    case DEVA_MARK_VISARGA: return VISARGA;
    case DEVA_MARK_CANDRA:  return DEVA_VOWEL_CANDRA[0][1];   /* ★表から引く（ॉ） */
    default:                return 0;
    }
}

/* 明示 halant。★子音の直後だけ意味を持つ。 */
DevaOut deva_halant(Deva *d)
{
    if (d->state != DEVA_CONSONANT)
        return NONE;
    DevaOut o = put(d, VIRAMA, 0);
    if (o.ok)
        d->state = DEVA_HALANT;
    return o;
}

/* nukta。★子音の直後だけ・重ね押し不可。 */
DevaOut deva_nukta(Deva *d)
{
    if (d->state != DEVA_CONSONANT || d->lastHasNukta)
        return NONE;
    DevaOut o = put(d, NUKTA, 0);
    if (o.ok)
        d->lastHasNukta = 1;
    return o;
}

/* anusvara ↔ chandrabindu のトグル。★cluster 末尾の修飾なので、
   子音の後 / matra の後 / 独立母音の後 のどれでも付けられる。 */
DevaOut deva_toggle_anusvara(Deva *d)
{
    if (d->len == 0)
        return NONE;
    const unsigned short last = d->buf[d->len - 1];
    if (last == ANUSVARA) {
        d->len--;
        DevaOut o = put(d, CHANDRABINDU, 1);
        if (o.ok)
            d->state = DEVA_MODIFIER;
        return o;
    }
    if (last == CHANDRABINDU) {
        /* ★トグルを一周して外す。状態は**末尾から計算し直す** ―― 外した先が
           子音なのか matra なのかは、覚えているのではなく見れば分かる。 */
        d->len--;
        d->state = recompute(d);
        DevaOut o = NONE;
        o.replace = 1;
        o.ok = 1;
        return o;
    }
    if (d->state == DEVA_CONSONANT || d->state == DEVA_MATRA || d->state == DEVA_VOWEL) {
        DevaOut o = put(d, ANUSVARA, 0);
        if (o.ok)
            d->state = DEVA_MODIFIER;
        return o;
    }
    return NONE;
}

DevaOut deva_visarga(Deva *d)
{
    if (d->state != DEVA_CONSONANT && d->state != DEVA_MATRA && d->state != DEVA_VOWEL)
        return NONE;
    DevaOut o = put(d, VISARGA, 0);
    if (o.ok)
        d->state = DEVA_MODIFIER;
    return o;
}

/* 直前の母音 / matra を長い形へ（RS → の post-shift）。
 *   - inherent schwa（子音のまま）… `ा` を足して "Cā" にする
 *   - 短い matra … 対応する長い matra に置き換える
 *   - 短い独立母音 … 対応する長い独立母音に置き換える
 *   - すでに長い / 対象外 … ok=0
 */
DevaOut deva_long_shift(Deva *d)
{
    if (d->len == 0)
        return NONE;
    if (d->state == DEVA_CONSONANT) {
        DevaOut o = put(d, AA_MATRA, 0);
        if (o.ok)
            d->state = DEVA_MATRA;
        return o;
    }
    const unsigned short last = d->buf[d->len - 1];
    for (unsigned i = 0; i < sizeof DEVA_MATRA_LONG / sizeof *DEVA_MATRA_LONG; i++)
        if (DEVA_MATRA_LONG[i][0] == last) {
            d->len--;
            return put(d, DEVA_MATRA_LONG[i][1], 1);
        }
    for (unsigned i = 0; i < sizeof DEVA_VOWEL_LONG / sizeof *DEVA_VOWEL_LONG; i++)
        if (DEVA_VOWEL_LONG[i][0] == last) {
            d->len--;
            return put(d, DEVA_VOWEL_LONG[i][1], 1);
        }
    return NONE;
}

/* 直前の母音 / matra を candra 形へ（LT + RS↑ の post-shift）。
 * ★英語由来語に軒並み付く（`डॉक्टर` `कॉलेज` `ऑफिस` `कॉफ़ी`）ので日常語の綴りに要る。
 *   - inherent schwa（子音のまま）… `ॉ` を足して "Cŏ" にする
 *   - matra … 対応する candra 形へ置き換える（`ा` `ो` → `ॉ` / `े` → `ॅ`）
 *   - 独立母音 … 同じく置き換える（`अ` `आ` `ओ` → `ऑ` / `ए` → `ऍ`）
 *   - すでに candra / 対象外 … ok=0
 * ★★**anusvara の巡回には混ぜない**（上流の判断）—— あちらは末尾へ**足す**操作で
 *   「もう一度で外れる」が成り立つが、candra は**置換**なので戻り先が無い。
 *   ★形は `deva_long_shift` と同じ（どちらも「後から差し替える」）。 */
DevaOut deva_candra(Deva *d)
{
    if (d->len == 0)
        return NONE;
    if (d->state == DEVA_CONSONANT) {
        DevaOut o = put(d, CANDRA_O, 0);
        if (o.ok)
            d->state = DEVA_MATRA;
        return o;
    }
    const unsigned short last = d->buf[d->len - 1];
    for (unsigned i = 0; i < sizeof DEVA_MATRA_CANDRA / sizeof *DEVA_MATRA_CANDRA; i++)
        if (DEVA_MATRA_CANDRA[i][0] == last) {
            d->len--;
            return put(d, DEVA_MATRA_CANDRA[i][1], 1);
        }
    for (unsigned i = 0; i < sizeof DEVA_VOWEL_CANDRA / sizeof *DEVA_VOWEL_CANDRA; i++)
        if (DEVA_VOWEL_CANDRA[i][0] == last) {
            d->len--;
            return put(d, DEVA_VOWEL_CANDRA[i][1], 1);
        }
    return NONE;
}

/* 1 字戻す。★状態は末尾から計算し直す。 */
DevaOut deva_backspace(Deva *d)
{
    if (d->len == 0)
        return NONE;
    d->len--;
    d->lastHasNukta = (d->len > 0 && d->buf[d->len - 1] == NUKTA);
    d->state = recompute(d);
    DevaOut o = NONE;
    o.replace = 1;
    o.ok = 1;
    return o;
}
