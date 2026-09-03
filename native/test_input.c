/* 入力の状態機械を SDL を開かずに走らせる（test_reply の入力版）。
 *
 * ★`input.h` は前から「ゴールデン: labo の 35 件を test_input で照合する」と
 *   書いていたが、**test_input は存在しなかった**（2026-09-02 に気づいた）。
 *   まず韓国語ぶんを置く —— 日本語・英語は台本（*.script）が画面ごと見ている。
 *
 *     ./test_input        → 場面ごとに「押した順 → 出た字」を出す
 *
 * ★合成は本体と**同じ korean.c**（ko_apply）を通す。別に書くと、片方の経路でしか
 *   通らない値が必ず出る。
 */
#include <stdio.h>
#include <string.h>

#include "input.h"
#include "devanagari.h"
#include "korean.h"
#include "text.h"

/* 1 フレーム。now は ms。vowel は -1 = 未選択。 */
typedef struct { int now, row, vowel, vowelNow, lt, rt, cc; } Step;

typedef struct { const char *name; const Step *steps; int n; } Scene;

/* 何も押していない状態（全部離す） */
#define REL(t) { t, 0, -1, 0, 0, 0, 0 }

/* 안녕 = 아 → 받침 ㄴ → 녀 → 받침 ㅇ(LT) */
static const Step S_ANNYEONG[] = {
    { 0,   0, 3, 1, 0, 0, 0 },     /* ㅇ + ㅏ */
    REL(50),
    { 100, 2, -1, 0, 0, 0, 1 },    /* D-pad ㄴ 単独 → 받침 */
    REL(150),
    /* ★RT は**母音より先に押す**。同じフレームで押すと、その場で rtUsed が
       0 に戻り、離したときに ㅣ 付加が発火して 녀 が 녜 になる（実測）。
       machine.ts も同じ順序なので、これは移植の差ではなく**押し方の作法**。 */
    { 180, 2, -1, 0, 0, 1, 1 },    /* RT を先に押さえる */
    { 200, 2, 1, 1, 0, 1, 1 },     /* ㄴ + RT 層の ㅕ */
    REL(250),
    { 300, 0, -1, 0, 1, 0, 0 },    /* LT → ㅇ받침 */
    REL(350),
};

/* 한국 = 하 → 받침 ㄴ → 구 → 받침 ㄱ */
static const Step S_HANGUK[] = {
    { 0,   9, 3, 1, 0, 0, 1 },     /* ㅎ + ㅏ */
    REL(50),
    { 100, 2, -1, 0, 0, 0, 1 },    /* 받침 ㄴ */
    REL(150),
    { 200, 1, 4, 1, 0, 0, 1 },     /* ㄱ + ㅜ */
    REL(250),
    { 300, 1, -1, 0, 0, 0, 1 },    /* 받침 ㄱ */
    REL(350),
};

/* 앉 = 아 → 받침 ㄴ → 받침 ㅈ（겹받침 ㄵ） */
static const Step S_ANJ[] = {
    { 0,   0, 3, 1, 0, 0, 0 },
    REL(50),
    { 100, 2, -1, 0, 0, 0, 1 },    /* ㄴ */
    REL(150),
    { 200, 8, -1, 0, 0, 0, 1 },    /* ㅈ → 겹받침 */
    REL(250),
};

/* 애 = 아 → RT 単押しで ㅣ 付加 */
static const Step S_AE[] = {
    { 0,   0, 3, 1, 0, 0, 0 },
    REL(50),
    { 100, 0, -1, 0, 0, 1, 0 },    /* RT 押す */
    REL(150),                       /* 離す → ㅣ 付加 */
};

/* 押したまま行を変える = 받침の付け直し（ㄴ → ㄹ） */
static const Step S_REDO[] = {
    { 0,   0, 3, 1, 0, 0, 0 },
    REL(50),
    { 100, 2, -1, 0, 0, 0, 1 },    /* 받침 ㄴ */
    { 150, 4, -1, 0, 0, 0, 1 },    /* 押したまま ㄹ へ */
    REL(200),
};

/* ★実機の指摘（2026-09-02）: 「마」のあとに ㅁ の行 + 母音を打つと 맘마 になっていた。
   子音が母音より先に着くと**いったん받침として適用される**ので、母音の到着で
   巻き戻さなければならない。正しくは 마마。 */
static const Step S_MAMA[] = {
    { 0,   5, 3, 1, 0, 0, 1 },     /* L1 + 右面 = ㅁ + ㅏ → 마 */
    REL(50),
    { 100, 5, -1, 0, 0, 0, 1 },    /* L1 だけ先に着く → いったん받침 ㅁ（맘） */
    { 130, 5, 3, 1, 0, 0, 1 },     /* 母音が来た → 巻き戻して新しい音節 */
    REL(180),
};

/* ★★実機の指摘（2026-09-02）: 「우울해요」が「우욿해요」になる。
   「울」のあとに ㅎ の行が母音より先に着くと겹받침 ㅀ ができてしまう。
   ★**겹받침も巻き戻す** —— GIME はここを除外していたが、それがこの穴の原因だった。 */
static const Step S_UUL[] = {
    { 0,   0, 4, 1, 0, 0, 0 },     /* ㅇ + ㅜ → 우 */
    REL(50),
    { 100, 0, 4, 1, 0, 0, 0 },     /* もう一度 → 우우 */
    REL(150),
    { 200, 4, -1, 0, 0, 0, 1 },    /* 받침 ㄹ → 우울 */
    REL(250),
    { 300, 9, -1, 0, 0, 0, 2 },    /* L1+↓ = ㅎ が先に着く → 겹받침 ㅀ（우욿） */
    { 330, 9, 3, 1, 0, 0, 2 },     /* 母音 → 巻き戻して 우울 + 하 */
    REL(380),
};

/* ★意図した겹받침は残る —— **一度離す**ので巻き戻せる状態が終わっている。
   ★離したあとにもう一度 ㅈ を押しても겹받침は成立しないので、そこで
     「戻せる状態」が落ちる（落とさないと前の音節の받침まで戻してしまう）。 */
static const Step S_ANJJA[] = {
    { 0,   0, 3, 1, 0, 0, 0 },     /* 아 */
    REL(50),
    { 100, 2, -1, 0, 0, 0, 1 },    /* 받침 ㄴ → 안 */
    REL(150),
    { 200, 8, -1, 0, 0, 0, 2 },    /* L1+→ = ㅈ → 겹받침 ㄵ → 앉 */
    REL(250),                       /* ★ここで離す = 意図した겹받침 */
    { 300, 8, -1, 0, 0, 0, 2 },    /* 次の音節の頭が先に着く（겹받침は不成立） */
    { 330, 8, 3, 1, 0, 0, 2 },     /* 母音 → 앉 はそのまま + 자 */
    REL(380),
};

/* ★★**LB と十字キーが同じフレームに来なくても壊れない**（2026-09-04・実機の指摘）。
   「당」のあと「신」を打つとき、ㅅ は **row 7 = LB + ↑**。左手の 2 つが
   ★**1 フレームずれると `다신` になっていた**（당 の 받침 ㅇ が消える）。
   ★順序は関係ない —— LB が先でも十字キーが先でも同じように壊れた。
   ★原因は `rollback_coda` が古いまま使われること。入力層は**받침が実際に
     付いたかを知らない**ので、成立しない組み合わせで何も出なかったときにずれる。
   ★上流（GIME）は「付いたときだけ巻き戻し可能」にしている。 */
static const Step S_DANGSIN_LB[] = {
    { 0,   3, 3, 1, 0, 0, 1 },     /* ㄷ + ㅏ → 다 */
    REL(50),
    { 100, 0, -1, 0, 1, 0, 0 },    /* LT = ㅇ받침 → 당 */
    REL(150),
    { 200, 5, -1, 0, 0, 0, 1 },    /* ★LB だけが先に着く（row 5 = ㅁ）*/
    { 215, 7, -1, 0, 0, 0, 2 },    /*    そのあと ↑ で row 7 = ㅅ */
    { 230, 7, 0,  1, 0, 1, 2 },    /* RT + RB = ㅣ → 시 */
    REL(280),
    { 330, 2, -1, 0, 0, 0, 1 },    /* 받침 ㄴ → 신 */
    REL(380),
};

/* ★同じことを逆の順（十字キーが先）でも見る。**どちらでも壊れていた**。 */
static const Step S_DANGSIN_DPAD[] = {
    { 0,   3, 3, 1, 0, 0, 1 },
    REL(50),
    { 100, 0, -1, 0, 1, 0, 0 },
    REL(150),
    { 200, 2, -1, 0, 0, 0, 1 },    /* ★↑ が先（row 2 = ㄴ）*/
    { 215, 7, -1, 0, 0, 0, 2 },
    { 230, 7, 0,  1, 0, 1, 2 },
    REL(280),
    { 330, 2, -1, 0, 0, 0, 1 },
    REL(380),
};

/* ★★**RT があとから着いても `이` になる**（2026-09-04・実機の指摘）。
   RT は母音の**層の切替**（ㅡ→ㅣ）なので、面ボタンを押したままなら同じ音節を
   差し替えるのが筋。★これが無いと `으` が出たまま作り直されず、RT を離したところで
   「単押し」と見なされて **ㅣ が足され `의`** になっていた
   （実機:「RT と RB の同時押しが `이` になりにくく `의` になりがち」）。
   ★**離したときは作り直さない** —— 指を離す順は自由であってほしいので、
     `이` を押さえたまま RT を先に離しても `으` へ戻らない（下の 4 番目）。 */
static const Step S_I_SIMUL[]   = { { 0, 0, 0, 1, 0, 1, 0 }, REL(60) };
static const Step S_I_RB_FIRST[] = { { 0, 0, 0, 1, 0, 0, 0 },
                                     { 20, 0, 0, 1, 0, 1, 0 }, REL(60) };
static const Step S_I_RT_FIRST[] = { { 0, 0, -1, 0, 0, 1, 0 },
                                     { 20, 0, 0, 1, 0, 1, 0 }, REL(60) };
static const Step S_I_RT_OFF[]   = { { 0, 0, 0, 1, 0, 1, 0 },
                                     { 40, 0, 0, 1, 0, 0, 0 }, REL(60) };

/* ★★**RT を押しながらの母音に、ㅣ が余計に足されていた**（2026-09-04）。
   ★下の `여` は RT + □ = `ㅕ`。★直す前は **`예`**（ㅕ + ㅣ）になっていた ——
     母音の処理が立てた「RT を使った」を、**押下エッジのリセットが後ろから
     打ち消していた**ので、離したときに「単押しだった」と見なされていた。
   ★★**ずっと壊れていたが、`ㅣ + ㅣ` や `ㅛ + ㅣ` が成立しない**ぶん、母音によっては
     表に出なかった（`이` `요` は無事に見えた）。★**症状の一致 ≠ 正しさ**。
   ★機能そのもの（RT 単押しの ㅣ 付加）は生きている —— `S_OI` がそれを見る。 */
static const Step S_YEO[]     = { { 0, 0, 1, 1, 0, 1, 0 }, REL(60) };
static const Step S_YEO_LATE[] = { { 0, 0, 1, 1, 0, 0, 0 },
                                   { 20, 0, 1, 1, 0, 1, 0 }, REL(60) };
static const Step S_OI[]      = { { 0, 0, 2, 1, 0, 0, 0 }, REL(60),
                                  { 100, 0, -1, 0, 0, 1, 0 }, REL(140) };

static const Scene SCENES[] = {
    { "안녕",             S_ANNYEONG, (int)(sizeof S_ANNYEONG / sizeof *S_ANNYEONG) },
    { "한국",             S_HANGUK,   (int)(sizeof S_HANGUK / sizeof *S_HANGUK) },
    { "겹받침 앉",        S_ANJ,      (int)(sizeof S_ANJ / sizeof *S_ANJ) },
    { "ㅣ付加 애",        S_AE,       (int)(sizeof S_AE / sizeof *S_AE) },
    { "받침の付け直し",   S_REDO,     (int)(sizeof S_REDO / sizeof *S_REDO) },
    { "★당신（LB が先）", S_DANGSIN_LB,   (int)(sizeof S_DANGSIN_LB / sizeof *S_DANGSIN_LB) },
    { "★당신（十字が先）", S_DANGSIN_DPAD, (int)(sizeof S_DANGSIN_DPAD / sizeof *S_DANGSIN_DPAD) },
    { "★이（同時）",        S_I_SIMUL,    (int)(sizeof S_I_SIMUL / sizeof *S_I_SIMUL) },
    { "★이（RB が先）",     S_I_RB_FIRST, (int)(sizeof S_I_RB_FIRST / sizeof *S_I_RB_FIRST) },
    { "★이（RT が先）",     S_I_RT_FIRST, (int)(sizeof S_I_RT_FIRST / sizeof *S_I_RT_FIRST) },
    { "★이（RT を先に離す）", S_I_RT_OFF, (int)(sizeof S_I_RT_OFF / sizeof *S_I_RT_OFF) },
    { "★여（RT + □）",      S_YEO,      (int)(sizeof S_YEO / sizeof *S_YEO) },
    { "★여（□ が先）",       S_YEO_LATE, (int)(sizeof S_YEO_LATE / sizeof *S_YEO_LATE) },
    { "★외（RT 単押しの ㅣ 付加）", S_OI, (int)(sizeof S_OI / sizeof *S_OI) },
    { "巻き戻し 마마",    S_MAMA,     (int)(sizeof S_MAMA / sizeof *S_MAMA) },
    { "겹받침も戻す 우울하", S_UUL,   (int)(sizeof S_UUL / sizeof *S_UUL) },
    { "겹받침は残す 앉자", S_ANJJA,   (int)(sizeof S_ANJJA / sizeof *S_ANJJA) },
};

static void show(const char *label, const uint16_t *buf, int n)
{
    char u8[256];
    u16_to_u8(buf, n, u8, (int)sizeof u8);
    printf("%s%s\n", label, u8);
}

int main(void)
{
    for (unsigned i = 0; i < sizeof SCENES / sizeof *SCENES; i++) {
        GpMachine gm;
        KoComposer ko;
        uint16_t buf[64];
        int len = 0;
        gp_init(&gm);
        ko_init(&ko);

        printf("--- %s ---\n", SCENES[i].name);
        for (int t = 0; t < SCENES[i].n; t++) {
            const Step *st = &SCENES[i].steps[t];
            GpFrameKana f = { st->now, st->row, st->vowel, st->vowelNow,
                          st->lt, st->rt, st->cc };
            GpAction a[4];
            const int an = gp_step_kana(&gm, GP_KO, &f, a);
            for (int k = 0; k < an; k++) {
                uint16_t ch;
                int rep = 0;
                if (!ko_apply(&ko, &a[k], &ch, &rep))
                    continue;
                len -= rep;                       /* 差し替えぶんを戻す */
                if (len < 0)
                    len = 0;
                if (len < (int)(sizeof buf / sizeof *buf))
                    buf[len++] = ch;
            }
            gp_sync_prev_kana(&gm, &f);
        }
        show("  ", buf, len);
    }

    /* 場面の外: rs↑ の子音サイクルと rs→ の複合母音（本体では main.c が呼ぶ） */
    {
        KoComposer ko;
        uint16_t buf[8], ch;
        int rep, len = 0;
        ko_init(&ko);
        GpAction a = { 0 };
        a.type = GPA_KO_SYLLABLE;
        a.onset = 0;                              /* ㄱ */
        a.nucleus = 0;                            /* ㅏ */
        ko_apply(&ko, &a, &ch, &rep);
        buf[len++] = ch;
        printf("--- 초성サイクル（rs↑）---\n");
        show("  ", buf, len);
        for (int i = 0; i < 3; i++) {
            if (!ko_cycle(&ko, &ch, &rep))
                continue;
            buf[len - rep] = ch;
            len += 1 - rep;
            show("  → ", buf, len);
        }

        ko_init(&ko);
        len = 0;
        a.onset = 11;                             /* ㅇ */
        a.nucleus = 8;                            /* ㅗ */
        ko_apply(&ko, &a, &ch, &rep);
        buf[len++] = ch;
        if (ko_add_aeo(&ko, &ch, &rep))
            buf[len - rep] = ch, len += 1 - rep;
        printf("--- 複合母音（rs→）---\n");
        show("  오 → ", buf, len);
    }

    /* ---- デーヴァナーガリーの合成 ----
     *
     * ★上流（DevanagariComposer.swift）に**検査が無い**ので、期待値は実装を追って
     *   書いた。★それだけだと「書いた本人が採点する」ので、**実際の単語で裏を取る**
     *   —— 音韻として正しいかは、出た並びが語として読めるかで分かる。
     */
    {
        Deva d;
        uint16_t buf[16];
        int len = 0;

        /* 出た字を積む。★replace は「直前の何字を差し替えるか」。 */
        #define EMIT(call) do { \
            DevaOut o = (call); \
            if (o.ok) { \
                len -= o.replace; \
                if (len < 0) len = 0; \
                for (int q = 0; q < o.len; q++) buf[len++] = o.text[q]; \
            } \
        } while (0)

        printf("--- デーヴァナーガリー: matra は子音の後 ---\n");
        deva_init(&d); len = 0;
        EMIT(deva_consonant(&d, 0x0915));         /* क */
        EMIT(deva_matra(&d, 0x093F));             /* ि */
        show("  क + ि → ", buf, len);            /* कि = ki */

        printf("--- ★auto-conjunct しない（नम が न्म にならない）---\n");
        deva_init(&d); len = 0;
        EMIT(deva_consonant(&d, 0x0928));         /* न */
        EMIT(deva_consonant(&d, 0x092E));         /* म */
        show("  न + म → ", buf, len);            /* नम = nama（namaste の頭） */

        printf("--- conjunct は halant を明示して作る ---\n");
        deva_init(&d); len = 0;
        EMIT(deva_consonant(&d, 0x0915));         /* क */
        EMIT(deva_halant(&d));                    /* ् */
        EMIT(deva_consonant(&d, 0x0937));         /* ष */
        show("  क + ् + ष → ", buf, len);       /* क्ष = kṣa */

        printf("--- 長母音への昇格（rs→）---\n");
        deva_init(&d); len = 0;
        EMIT(deva_consonant(&d, 0x0915));
        EMIT(deva_long_shift(&d));                /* inherent schwa → ा */
        show("  क + rs→ → ", buf, len);          /* का = kā */
        deva_init(&d); len = 0;
        EMIT(deva_consonant(&d, 0x0915));
        EMIT(deva_matra(&d, 0x093F));             /* ि */
        EMIT(deva_long_shift(&d));                /* ि → ी（置換） */
        show("  कि + rs→ → ", buf, len);        /* की = kī */

        printf("--- anusvara ↔ chandrabindu のトグル（rs↑）---\n");
        deva_init(&d); len = 0;
        EMIT(deva_consonant(&d, 0x0915));
        EMIT(deva_toggle_anusvara(&d));
        show("  1 回 → ", buf, len);              /* कं */
        EMIT(deva_toggle_anusvara(&d));
        show("  2 回 → ", buf, len);              /* कँ */
        EMIT(deva_toggle_anusvara(&d));
        show("  3 回 → ", buf, len);              /* क（一周して外れる） */

        printf("--- nukta は重ね押しできない ---\n");
        deva_init(&d); len = 0;
        EMIT(deva_consonant(&d, 0x0915));
        EMIT(deva_nukta(&d));
        show("  क + nukta → ", buf, len);        /* क़ */
        EMIT(deva_nukta(&d));                     /* ★2 回目は no-op */
        show("  もう一度 → ", buf, len);          /* 変わらない */

        printf("--- ★子音が無ければ matra は入らない（呼ぶ側が独立母音へ落とす）---\n");
        deva_init(&d); len = 0;
        {
            DevaOut o = deva_matra(&d, 0x093F);
            printf("  matra 単独: ok=%d（0 なら独立母音へ）\n", o.ok);
            EMIT(deva_vowel(&d, 0x0907));         /* इ */
        }
        show("  → ", buf, len);                   /* इ */

        printf("--- 実際の語で裏を取る ---\n");
        deva_init(&d); len = 0;
        EMIT(deva_consonant(&d, 0x092E));         /* म */
        EMIT(deva_matra(&d, 0x0941));             /* ु */
        EMIT(deva_consonant(&d, 0x091D));         /* झ */
        EMIT(deva_matra(&d, 0x0947));             /* े */
        show("  मुझे（私に）→ ", buf, len);

        deva_init(&d); len = 0;
        EMIT(deva_consonant(&d, 0x0939));         /* ह */
        EMIT(deva_matra(&d, 0x0942));             /* ू */
        EMIT(deva_toggle_anusvara(&d));           /* ं */
        EMIT(deva_toggle_anusvara(&d));           /* → ँ */
        show("  हूँ（です・1 人称）→ ", buf, len);

        /* ★★candra（`ॉ` / `ॅ`）—— 英語由来語（डॉक्टर / कॉलेज）に軒並み付く。
           ★**素の子音に付ける**のと、**母音記号を差し替える**のは別の道なので両方見る。
           ★上流（GIME）には 2026-09-03 まで無かった。移植の抜けではなく
             **入力方式そのものの穴**だったので、GIME 側に足してから焼き直している。 */
        deva_init(&d); len = 0;
        EMIT(deva_consonant(&d, 0x0921));         /* ड */
        EMIT(deva_candra(&d));                    /* ★素の子音 → ॉ を足す */
        show("  डॉ（ドクターの頭）→ ", buf, len);

        deva_init(&d); len = 0;
        EMIT(deva_consonant(&d, 0x0915));         /* क */
        EMIT(deva_matra(&d, 0x0947));             /* े */
        EMIT(deva_candra(&d));                    /* ★े → ॅ に差し替え */
        show("  कॅ（े を差し替える）→ ", buf, len);

        deva_init(&d); len = 0;
        EMIT(deva_vowel(&d, 0x0913));             /* ओ */
        EMIT(deva_candra(&d));                    /* ★独立母音 → ऑ */
        EMIT(deva_candra(&d));                    /* ★2 回目は no-op（戻り先が無い） */
        show("  ऑ（独立母音・2 回目は効かない）→ ", buf, len);
        #undef EMIT
    }

    /* ---- デーヴァナーガリーの入力（LS の latch × D-pad）----
     *
     * ★合成器だけでなく**押した順**を見る。韓国語で実機 1 巡に出た 3 件は全部
     *   ここ（合成ではなく入力）だった。
     */
    {
        /* 1 フレーム。lsDir: 0=中立 1=↑ 2=→ 3=↓ 4=← / dpad: 0..3 / -1 */
        typedef struct { int ls, dpad, lb, vowel, lt, rt, l3, rsUp, rsRight; } DStep;
        DevaMachine dm;
        Deva d;
        uint16_t buf[16];
        int len;

        #define RUN(name, steps) do { \
            deva_machine_init(&dm); deva_init(&d); len = 0; \
            for (unsigned q = 0; q < sizeof (steps) / sizeof *(steps); q++) { \
                const DStep *x = &(steps)[q]; \
                GpFrameDeva fr = { q * 20, x->ls, x->dpad, x->lb, x->vowel, \
                                   x->lt, x->rt, x->l3, x->rsUp, x->rsRight }; \
                GpAction a[4]; \
                const int an = gp_step_deva(&dm, &fr, a); \
                for (int k = 0; k < an; k++) { \
                    DevaOut o; \
                    switch (a[k].deva_op) { \
                    case DEVA_OP_CONSONANT: o = deva_consonant(&d, a[k].deva_ch); break; \
                    case DEVA_OP_VOWEL: \
                        o = deva_matra(&d, a[k].deva_ch); \
                        if (!o.ok) o = deva_vowel(&d, a[k].deva_alt); \
                        break; \
                    case DEVA_OP_HALANT:   o = deva_halant(&d); break; \
                    case DEVA_OP_NUKTA:    o = deva_nukta(&d); break; \
                    case DEVA_OP_ANUSVARA: o = deva_toggle_anusvara(&d); break; \
                    case DEVA_OP_VISARGA:  o = deva_visarga(&d); break; \
                    case DEVA_OP_LONG:     o = deva_long_shift(&d); break; \
                    case DEVA_OP_CANDRA:   o = deva_candra(&d); break; \
                    /* ★カーソル移動と改行は本体（main.c）の仕事なので、ここでは字が出ない。 \
                       ★★**default に落とさない** —— 落とすと、新しい op を足したときに \
                         黙って別の処理になる（実際に踏んだ: CURSOR が long_shift に \
                         なって `क` が `का` になっていた）。 */ \
                    default:               o = (DevaOut){ { 0, 0 }, 0, 0, 0 }; break; \
                    } \
                    if (o.ok) { \
                        len -= o.replace; if (len < 0) len = 0; \
                        for (int z = 0; z < o.len; z++) buf[len++] = o.text[z]; \
                    } \
                } \
                gp_sync_prev_deva(&dm, &fr); \
            } \
            printf("--- %s ---\n", name); \
            show("  ", buf, len); \
        } while (0)

        /* 何も押していない 1 フレーム（エッジを作るために挟む） */
        #define DREL { 0, -1, 0, -1, 0, 0, 0, 0, 0 }

        /* किः LS↑ で k varga を latch → D-pad↑ で क → □（i）で ि */
        static const DStep KI[] = {
            { 1, -1, 0, -1, 0, 0, 0, 0, 0 },   /* LS↑ = k varga を latch */
            DREL,
            { 0, 0, 0, -1, 0, 0, 0, 0, 0 },    /* D-pad↑ = 無気無声 → क */
            DREL,
            { 0, -1, 0, 3, 0, 0, 0, 0, 0 },    /* ○（右）= i → ि */
            DREL,
        };
        RUN("LS↑ + D-pad↑ + ○ → कि", KI);

        /* ★★**latch は子音 1 つで消える**（前置シフト・GIME と同じ）。
           2 つ目の D-pad→ は latch が中立へ戻ったあとなので **प varga** から出る。
           ★★ここは 2026-09-03 まで「latch は残る → कख」という期待を書いていた ——
             **移植で消費の 1 行が落ちていたのを、実装を追って正典にしていた**。
             上流（Swift / Kotlin）はどちらも子音・鼻音の 2 か所で latch を落とす。 */
        static const DStep LATCH[] = {
            { 1, -1, 0, -1, 0, 0, 0, 0, 0 },
            DREL,
            { 0, 0, 0, -1, 0, 0, 0, 0, 0 },    /* क（ここで latch が消える） */
            DREL,
            { 0, 1, 0, -1, 0, 0, 0, 0, 0 },    /* D-pad→ = 中立 = प varga の 2 番目 → फ */
            DREL,
        };
        RUN("★latch は子音 1 つで消える（前置シフト）→ कफ", LATCH);

        /* ★**鼻音（LB）でも消える** —— 上流が latch を落とすのは 2 か所で、
             こちらがもう 1 か所。★片方だけ直すと「D-pad なら消えるが LB では残る」
             という、いちばん説明しにくい形になる。 */
        static const DStep LATCH_LB[] = {
            { 1, -1, 0, -1, 0, 0, 0, 0, 0 },   /* LS↑ = क varga を latch */
            DREL,
            { 0, -1, 1, -1, 0, 0, 0, 0, 0 },   /* LB = その varga の鼻音 → ङ */
            DREL,
            { 0, 0, 0, -1, 0, 0, 0, 0, 0 },    /* D-pad↑ = 中立 = प varga の頭 → प */
            DREL,
        };
        RUN("★鼻音でも latch は消える → ङप", LATCH_LB);

        /* ★中立 = प varga（LS を倒さないまま打てる ―― いちばん頻度の高い置き方） */
        static const DStep NEUTRAL[] = {
            { 0, 0, 0, -1, 0, 0, 0, 0, 0 },    /* D-pad↑ → प */
            DREL,
            { 0, -1, 1, -1, 0, 0, 0, 0, 0 },   /* LB = 鼻音 → म */
            DREL,
        };
        RUN("★中立は प varga / LB は鼻音 → पम", NEUTRAL);

        /* ★★**LT + RS↑ = candra / LT 無しは anusvara**（同じ RS↑ に 2 つ）。
           ★ここが**打鍵の側**の検査 —— 合成器だけ見ても「LT を見ているか」は分からない。 */
        static const DStep CANDRA[] = {
            { 0, 2, 0, -1, 0, 0, 0, 0, 0 },    /* D-pad↓ = 中立(प varga) の 3 番目 → ब */
            DREL,
            { 0, -1, 0, -1, 1, 0, 0, 1, 0 },   /* LT + RS↑ → ॉ */
            DREL,
        };
        RUN("★LT + RS↑ = candra → बॉ", CANDRA);

        static const DStep ANUSVARA[] = {
            { 0, 2, 0, -1, 0, 0, 0, 0, 0 },    /* ब */
            DREL,
            { 0, -1, 0, -1, 0, 0, 0, 1, 0 },   /* ★LT 無しの RS↑ は今までどおり anusvara */
            DREL,
        };
        RUN("★LT 無しの RS↑ は anusvara のまま → बं", ANUSVARA);

        /* 非 varga: L3 で層に入り、D-pad↑ で य。★1 子音で自動 OFF */
        static const DStep NONVARGA[] = {
            { 0, -1, 0, -1, 0, 0, 1, 0, 0 },   /* L3 = 非 varga 層へ */
            DREL,
            { 0, 0, 0, -1, 0, 0, 0, 0, 0 },    /* D-pad↑ → य（そして層が閉じる） */
            DREL,
            { 0, 0, 0, -1, 0, 0, 0, 0, 0 },    /* ★もう一度 → 층は閉じているので प */
            DREL,
        };
        RUN("★非 varga は 1 子音で閉じる → यप", NONVARGA);

        /* 非 varga + LT = 摩擦音の層 */
        static const DStep SIBILANT[] = {
            { 0, -1, 0, -1, 0, 0, 1, 0, 0 },
            DREL,
            { 0, 0, 0, -1, 1, 0, 0, 0, 0 },    /* LT + D-pad↑ → श */
            DREL,
        };
        RUN("LT で摩擦音の層 → श", SIBILANT);

        /* conjunct: क → RT tap（halant）→ ष */
        static const DStep CONJ[] = {
            { 1, -1, 0, -1, 0, 0, 0, 0, 0 },
            DREL,
            { 0, 0, 0, -1, 0, 0, 0, 0, 0 },    /* क */
            DREL,
            { 0, -1, 0, -1, 0, 1, 0, 0, 0 },   /* RT を押す */
            DREL,                              /* ★離した時に halant */
            { 0, -1, 0, -1, 0, 0, 1, 0, 0 },   /* L3 = 非 varga */
            DREL,
            { 0, 1, 0, -1, 1, 0, 0, 0, 0 },    /* LT + D-pad→ → ष */
            DREL,
        };
        RUN("RT tap で halant → क्ष", CONJ);

        /* ★★RT + LS はキャレット移動（main.c が持つ）—— 離しても halant は出ない */
        static const DStep RTLS[] = {
            { 1, -1, 0, -1, 0, 0, 0, 0, 0 },
            DREL,
            { 0, 0, 0, -1, 0, 0, 0, 0, 0 },    /* क */
            DREL,
            { 0, -1, 0, -1, 0, 1, 0, 0, 0 },   /* RT を押す */
            { 2, -1, 0, -1, 0, 1, 0, 0, 0 },   /* RT 押下中に LS→ = カーソル移動 */
            DREL,                              /* ★離しても halant は出ない */
        };
        RUN("★RT+LS はカーソル移動 → halant が出ない → क", RTLS);
        #undef RUN
        #undef DREL
    }
    return 0;
}
