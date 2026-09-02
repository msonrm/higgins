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

static const Scene SCENES[] = {
    { "안녕",             S_ANNYEONG, (int)(sizeof S_ANNYEONG / sizeof *S_ANNYEONG) },
    { "한국",             S_HANGUK,   (int)(sizeof S_HANGUK / sizeof *S_HANGUK) },
    { "겹받침 앉",        S_ANJ,      (int)(sizeof S_ANJ / sizeof *S_ANJ) },
    { "ㅣ付加 애",        S_AE,       (int)(sizeof S_AE / sizeof *S_AE) },
    { "받침の付け直し",   S_REDO,     (int)(sizeof S_REDO / sizeof *S_REDO) },
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
            GpFrame f = { st->now, st->row, st->vowel, st->vowelNow,
                          st->lt, st->rt, st->cc };
            GpAction a[4];
            const int an = gp_step(&gm, GP_KO, &f, a);
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
            gp_sync_prev(&gm, &f);
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
    return 0;
}
