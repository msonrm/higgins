/* デーヴァナーガリー akshara 合成 —— GIME の DevanagariComposer.swift の移植。
 *
 * ★**音韻の前提**（上流の頭書きより）:
 *   - 子音は固有の schwa (a) を背負う（`क` 単独で "ka"）
 *   - conjunct は virama (`्`) で schwa を殺して作る（`क` + `्` + `ष` = `क्ष`）
 *   - 母音 matra が来ると schwa を置換して akshara 確定（`क` + `ि` = `कि`）
 *   - anusvara / chandrabindu / visarga は cluster 末尾の修飾
 *   - nukta (`़`) は子音に直後置
 *
 * ★★**子音連続は auto-conjunct しない。** `नम`（namaste の頭）は न と म が別の
 *   akshara で、両方 inherent schwa 付き。自動で conjunct にすると `न्म` になり誤り。
 *   ITRANS / Google Hindi IME と同じく、**conjunct は halant（RT）を明示的に打つ**。
 *
 * ★出力は「何字出して、直前の何字を置き換えるか」の対。置換が要るのは、
 *   長母音への昇格（`ि` → `ी`）と anusvara ↔ chandrabindu のトグルがあるため。
 */
#ifndef HG_DEVANAGARI_H
#define HG_DEVANAGARI_H

enum { DEVA_BUF_MAX = 32 };

/* 合成の状態。★上流の DevaState と同じ 6 つ。 */
typedef enum {
    DEVA_EMPTY = 0,      /* 空 / cluster 未開始 */
    DEVA_CONSONANT,      /* 末尾が子音（+nukta 可）で inherent schwa を背負う */
    DEVA_MATRA,          /* matra が付いて akshara 確定済み */
    DEVA_VOWEL,          /* 独立母音で cluster 確定済み */
    DEVA_HALANT,         /* 明示 halant で子音単独（次の子音は新 cluster） */
    DEVA_MODIFIER,       /* anusvara / chandrabindu / visarga 付与済み */
} DevaState;

typedef struct {
    unsigned short buf[DEVA_BUF_MAX];
    int len;
    DevaState state;
    int lastHasNukta;    /* 直前の子音に nukta が付いているか（重ね押し防止） */
} Deva;

/* 出力。★`ok` が 0 なら何も起きなかった（呼ぶ側が別の道へ落とす）。 */
typedef struct {
    unsigned short text[2];
    int len;
    int replace;         /* 直前の何字を置き換えるか */
    int ok;
} DevaOut;

void deva_init(Deva *d);
int  deva_is_composing(const Deva *d);

DevaOut deva_consonant(Deva *d, unsigned short c);
DevaOut deva_matra(Deva *d, unsigned short m);          /* 子音の後だけ。ok=0 なら独立母音へ */
DevaOut deva_vowel(Deva *d, unsigned short v);
DevaOut deva_halant(Deva *d);
DevaOut deva_nukta(Deva *d);
DevaOut deva_toggle_anusvara(Deva *d);
DevaOut deva_visarga(Deva *d);
DevaOut deva_long_shift(Deva *d);
DevaOut deva_backspace(Deva *d);
void    deva_commit(Deva *d);

/* 字の分類（U+0900..U+097F）。★上流の判定と同じ範囲。 */
int deva_is_consonant(unsigned short c);
int deva_is_matra(unsigned short c);
int deva_is_vowel(unsigned short c);

#endif
