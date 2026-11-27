/* Higgins Web 版（DOM で描く）の芯。
 *
 * ★★**native/main.c の「打鍵 → 合成 → ELIZA」の道だけを写した複製。** 画面は描かない
 *   —— 1 フレームぶん進めて（hg_step）、状態を JSON で返す（hg_state）だけ。
 *   描くのは webui/main.js（HTML / CSS）。
 * ★★**native と同期しない**（2026-11-27・msonrm さんの判断）。イベントでの披露が目的で、
 *   今後は Web 版を主にする。native/main.c を直しても、ここへは自動では来ない。
 * ★それでも打鍵の状態機械・合成器・分かち書き・ELIZA・変換は **native と同じソース**
 *   （input.c / korean.c / devanagari.c / wakachi.c / josa.c / hindi.c / tsunagi*.c /
 *   conv.c / eliza_bridge.cpp）をそのままリンクする。写したのは「繋ぐ側」だけ。
 *
 * ★native との違い（割り切ったもの）:
 *   - 入力欄の中の行移動（左スティック ↑↓ / RT+LS ↑↓）は無い —— 折返しをブラウザに
 *     任せるので、行の情報が C に無い。↑↓ は本文のスクロール（イベントで JS へ）
 *   - Start は JS が受ける（メニューは HTML）。閉じたら hg_resync で押下を引き継ぐ
 *   - 打ち方の図・起動画面・ライセンスの頁は JS 側（文言は hg_ui で ui.h から渡す）
 */
#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plat.h"          /* BTN_* だけ使う（画面の関数は呼ばない） */
#include "input.h"
#include "text.h"
#include "wakachi.h"
#include "josa.h"
#include "korean.h"
#include "devanagari.h"
#include "hindi.h"
#include "tsunagi.h"
#include "tsunagi_ko.h"
#include "eliza_bridge.h"
#include "conv.h"
#include "ui.h"

extern const char _binary_script_txt_start[], _binary_script_txt_end[];
extern const char _binary_script_en_txt_start[], _binary_script_en_txt_end[];
extern const char _binary_script_ko_txt_start[], _binary_script_ko_txt_end[];
extern const char _binary_script_hi_txt_start[], _binary_script_hi_txt_end[];

/* ---- JSON を組む（出口は全部これ）----
 * ★文字列は **UTF-16 の単位で \uXXXX に落とす** —— JS の文字列は UTF-16 なので、
 *   C が持っている添字（下線・注目文節・キャレット）が**そのまま JS の添字になる**。 */
static char *js;
static size_t js_n, js_cap;

static void js_put(const char *s, size_t n)
{
    if (js_n + n + 1 > js_cap) {
        js_cap = (js_n + n + 1) * 2 + 1024;
        js = realloc(js, js_cap);
    }
    memcpy(js + js_n, s, n);
    js_n += n;
    js[js_n] = 0;
}
static void js_raw(const char *s) { js_put(s, strlen(s)); }
static void js_int(int v)
{
    char b[16];
    js_put(b, (size_t)snprintf(b, sizeof b, "%d", v));
}
static void js_u16(const uint16_t *s, int n)
{
    js_raw("\"");
    for (int i = 0; i < n; i++) {
        char b[8];
        const uint16_t c = s[i];
        if (c >= 0x20 && c < 0x7F && c != '"' && c != '\\')
            js_put((const char *)&(char){ (char)c }, 1);
        else
            js_put(b, (size_t)snprintf(b, sizeof b, "\\u%04x", c));
    }
    js_raw("\"");
}
static void js_u8(const char *s)
{
    static uint16_t t[4096];
    js_u16(t, u8_to_u16(s ? s : "", -1, t, (int)(sizeof t / sizeof *t)));
}
static void js_ch(uint16_t c) { js_u16(&c, c ? 1 : 0); }

/* ---- 席 ---- */

static int seat = SEAT_EN;
static int ui_lang = UI_EN;

static int seat_slot(void)
{
    return seat == SEAT_EN ? HG_EN : seat == SEAT_KO ? HG_KO
         : seat == SEAT_HI ? HG_HI : HG_JA;
}

static int seat_lang(void)
{
    return seat == SEAT_EN ? GP_EN : seat == SEAT_KO ? GP_KO : GP_JA;
}

static int ui_lang_for_seat(int s)
{
    return s == SEAT_EN ? UI_EN : s == SEAT_KO ? UI_KO
         : s == SEAT_HI ? UI_EN : UI_JA;
}

/* ---- 出来事（本文に積むもの）----
 * ★JS は毎フレーム hg_take で受け取って空にする。 */
static char *ev;
static size_t ev_n, ev_cap;

static void ev_begin(void)
{
    /* js を借りて組み、ev へ足す */
    js_n = 0;
    if (js) js[0] = 0;
}
static void ev_end(void)
{
    if (ev_n + js_n + 2 > ev_cap) {
        ev_cap = (ev_n + js_n + 2) * 2 + 1024;
        ev = realloc(ev, ev_cap);
    }
    if (ev_n)
        ev[ev_n++] = ',';
    memcpy(ev + ev_n, js, js_n);
    ev_n += js_n;
    ev[ev_n] = 0;
}

/* kind: "you"（打った文）/ "eliza"（返事）/ "hello"（挨拶）/ "scroll"（±1） */
static void ev_text(const char *kind, const uint16_t *s, int n)
{
    ev_begin();
    js_raw("{\"k\":\"");
    js_raw(kind);
    js_raw("\",\"t\":");
    js_u16(s, n);
    js_raw("}");
    ev_end();
}

static void ev_scroll(int d)
{
    ev_begin();
    js_raw("{\"k\":\"scroll\",\"d\":");
    js_int(d);
    js_raw("}");
    ev_end();
}

/* ELIZA の 1 行を積む。★出口は席で替わる（native の say と同じ）。 */
static void say(const char *kind, const char *utf8)
{
    static uint16_t buf[2048];
    const int max = (int)(sizeof buf / sizeof *buf);
    const int n = seat == SEAT_KO ? tsunagi_ko(utf8, buf, max)
                                  : tsunagi(utf8, buf, max);
    ev_text(kind, buf, n);
}

/* ---- 打っている字（native の main.c から）---- */

static GpMachine gm;
static KoComposer ko;
static DevaMachine dvm;
static Deva dv;
enum { COMP_MAX = 96 };
static uint16_t comp[COMP_MAX];
static int clen, caret, fixed;
static int punct_n, punct_at;

static void punct_break(void) { punct_n = 0; punct_at = 0; }

static void comp_insert(const uint16_t *t, int tn, int replace)
{
    if (fixed > caret)
        fixed = caret;
    int del = replace > caret ? caret : replace;
    for (int i = caret - del; i + del < clen; i++)
        comp[i] = comp[i + del];
    clen -= del;
    caret -= del;
    if (clen + tn > COMP_MAX)
        tn = COMP_MAX - clen;
    for (int i = clen - 1; i >= caret; i--)
        comp[i + tn] = comp[i];
    for (int i = 0; i < tn; i++)
        comp[caret + i] = t[i];
    clen += tn;
    caret += tn;
    if (seat != SEAT_JA)
        fixed = caret;
    punct_break();
}

static void comp_bs(void)
{
    if (!caret)
        return;
    for (int i = caret - 1; i + 1 < clen; i++)
        comp[i] = comp[i + 1];
    clen--;
    caret--;
    if (fixed > caret)
        fixed = caret;
    punct_break();
}

static void comp_drop_pending(void)
{
    const int del = caret - fixed;
    if (del <= 0)
        return;
    for (int i = fixed; i + del < clen; i++)
        comp[i] = comp[i + del];
    clen -= del;
    caret = fixed;
}

static void caret_set(int pos)
{
    if (pos < 0 || pos > clen)
        return;
    caret = pos;
    fixed = pos;
    ko_commit(&ko);
    deva_commit(&dv);
    punct_break();
}

/* ---- パッド → 行 / 段（native と同じ式）---- */

static int pad_row(int p)
{
    const int dir = (p & BTN_LEFT) ? 1 : (p & BTN_UP) ? 2 : (p & BTN_RIGHT) ? 3
                  : (p & BTN_DOWN) ? 4 : 0;
    return dir + ((p & BTN_L1) ? 5 : 0);
}

static int pad_vowel(int p)
{
    return (p & BTN_R1) ? 0 : (p & BTN_SQ) ? 1 : (p & BTN_TRI) ? 2
         : (p & BTN_CIR) ? 3 : (p & BTN_X) ? 4 : -1;
}

enum { LS_DELAY = 24, LS_INTERVAL = 5, PUNCT_WINDOW = 35 };

static void ls_dirs(const uint8_t axes[4], int out[4])
{
    const int dx = (int)axes[2] - 0x80, dy = (int)axes[3] - 0x80;
    const int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    out[0] = out[1] = out[2] = out[3] = 0;
    if (ax < 0x40 && ay < 0x40)
        return;
    if (ax >= ay)
        out[dx < 0 ? 0 : 1] = 1;
    else
        out[dy < 0 ? 2 : 3] = 1;
}

/* 右スティックのフリック（render.c の pad_rstick_flick と同じ）。
   ★1 フレームに 1 回だけ呼ぶ（内部にエッジの記憶がある）。 */
static int rs_held;
static int rstick_flick(const uint8_t axes[4])
{
    int dx = (int)axes[0] - 0x80, dy = (int)axes[1] - 0x80;
    int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    int dir = 0;
    if (ax >= 0x40 || ay >= 0x40)
        dir = ax >= ay ? (dx < 0 ? 1 : 3) : (dy < 0 ? 2 : 4);
    int fire = dir && !rs_held;
    rs_held = dir;
    return fire ? dir : 0;
}

static int pad_deva_dpad(int p)
{
    return (p & BTN_UP) ? 0 : (p & BTN_RIGHT) ? 1 : (p & BTN_DOWN) ? 2
         : (p & BTN_LEFT) ? 3 : -1;
}

static int pad_deva_ls(const uint8_t axes[4])
{
    int d[4];
    ls_dirs(axes, d);
    return d[2] ? 1 : d[1] ? 2 : d[3] ? 3 : d[0] ? 4 : 0;
}

static GpFrameDeva deva_frame_of(int p, const uint8_t axes[4], int rs, int ms)
{
    GpFrameDeva f = { ms, pad_deva_ls(axes), pad_deva_dpad(p), !!(p & BTN_L1),
                      pad_vowel(p), !!(p & BTN_L2), !!(p & BTN_R2), !!(p & BTN_L3),
                      rs == 2, rs == 3 };
    return f;
}

static GpFrameKana frame_of(int p, int ms)
{
    const int cc = !!(p & BTN_LEFT) + !!(p & BTN_UP) + !!(p & BTN_RIGHT)
                 + !!(p & BTN_DOWN) + !!(p & BTN_L1);
    const int vowel = pad_vowel(p);
    GpFrameKana f = { ms, pad_row(p), vowel, vowel >= 0,
                  !!(p & BTN_L2), !!(p & BTN_R2), cc };
    return f;
}

static int pad_prev;
static int fields;
static int ls_prev[4], ls_at[4];
static int cur_row;

/* 押されたままのものを「もう見た」ことにする（native の carry_over_pad）。
   ★パッドは読み直さない —— 呼ぶ側（JS）が今の値を渡す。 */
static void carry_over(int p, const uint8_t axes[4])
{
    pad_prev = p;
    GpFrameKana f = frame_of(p, 0);
    gp_sync_prev_kana(&gm, &f);
    GpFrameDeva df = deva_frame_of(p, axes, 0, 0);
    gp_sync_prev_deva(&dvm, &df);
    int ls[4];
    ls_dirs(axes, ls);
    for (int d = 0; d < 4; d++) {
        ls_prev[d] = ls[d];
        ls_at[d] = 0;
    }
    rstick_flick(axes);                /* ★倒したままのフリックも「もう見た」 */
}

static void conv_accept(void)
{
    uint16_t out[COMP_MAX * 2];
    const int n = conv_commit(out, (int)(sizeof out / sizeof *out));
    comp_insert(out, n, caret - fixed);
    fixed = caret;
}

/* ---- 送る ---- */

static void send(const uint16_t *typed, int n)
{
    static char buf[4096];
    ko_commit(&ko);
    deva_commit(&dv);
    if (seat == SEAT_HI)
        hindi_in(typed, n, buf, (int)sizeof buf);
    else if (seat == SEAT_EN)
        u16_to_u8(typed, n, buf, (int)sizeof buf);
    else if (seat == SEAT_KO)
        josa(typed, n, buf, (int)sizeof buf);
    else
        wakachi(typed, n, buf, (int)sizeof buf);
    ev_text("you", typed, n);
    say("eliza", hg_eliza_response(seat_slot(), buf));
}

static int send_line(void)
{
    if (!clen)
        return 0;
    static uint16_t typed[COMP_MAX * 2];
    const int typed_n = clen;
    for (int i = 0; i < clen; i++)
        typed[i] = comp[i];
    clen = 0;
    caret = 0;
    fixed = 0;
    gm.eagerSet = 0;
    ko_commit(&ko);
    deva_commit(&dv);
    punct_break();
    send(typed, typed_n);
    return 1;
}

/* ---- デーヴァナーガリーの席の 1 フレーム（native と同じ。行移動だけ落とした）---- */

static int step_deva_frame(int p, const uint8_t axes[4], int rs, int fields_)
{
    GpFrameDeva f = deva_frame_of(p, axes, rs, fields_ * 17);
    GpAction a[4];
    const int n = gp_step_deva(&dvm, &f, a);
    int dirty = 0;
    for (int i = 0; i < n; i++) {
        DevaOut o;
        switch (a[i].deva_op) {
        case DEVA_OP_CONSONANT:
            o = deva_consonant(&dv, a[i].deva_ch);
            break;
        case DEVA_OP_VOWEL:
            o = deva_matra(&dv, a[i].deva_ch);
            if (!o.ok)
                o = deva_vowel(&dv, a[i].deva_alt);
            break;
        case DEVA_OP_HALANT:   o = deva_halant(&dv); break;
        case DEVA_OP_NUKTA:    o = deva_nukta(&dv); break;
        case DEVA_OP_ANUSVARA: o = deva_toggle_anusvara(&dv); break;
        case DEVA_OP_VISARGA:  o = deva_visarga(&dv); break;
        case DEVA_OP_LONG:     o = deva_long_shift(&dv); break;
        case DEVA_OP_CANDRA:   o = deva_candra(&dv); break;
        case DEVA_OP_CURSOR: {
            const int d = (short)a[i].deva_ch;
            if (d == -1 && caret > 0) {
                caret_set(caret - 1);
                dirty = 1;
            } else if (d == 1 && caret < clen) {
                caret_set(caret + 1);
                dirty = 1;
            } else if (d == -2 || d == 2) {
                ev_scroll(d < 0 ? -1 : 1);   /* ★Web 版は行移動なし。本文を送る */
            }
            continue;
        }
        case DEVA_OP_COMMIT:
            if (send_line())
                dirty = 1;
            continue;
        default:
            continue;
        }
        if (o.ok) {
            comp_insert(o.text, o.len, o.replace);
            dirty = 1;
        }
    }
    gp_sync_prev_deva(&dvm, &f);
    return dirty;
}

/* ---- 行インジケータ（native の row_indicator）---- */

static int row_indicator(int row, uint16_t *ind, uint16_t *markp)
{
    int ilen;
    *markp = 0;
    if (seat == SEAT_KO) {
        ind[0] = ko_row_char(row);
        ilen = 1;
    } else if (seat == SEAT_HI) {
        ind[0] = deva_head_char(&dvm);
        ilen = 1;
    } else if (seat == SEAT_EN) {
        ilen = gp_row_chars_en(row, ind, 4);
        if (gm.engCapsLock)       *markp = 0x25B2;
        else if (gm.engSmartCaps) *markp = 0x25B3;
        else if (gm.engShiftNext) *markp = 0x21E7;
        if (*markp)
            for (int i = 0; i < ilen; i++)
                if (ind[i] >= 'a' && ind[i] <= 'z')
                    ind[i] -= 0x20;
    } else {
        ind[0] = gp_row_char(row);
        ilen = 1;
    }
    return ilen;
}

/* ---- 打鍵をひとまとめに適用する（native の apply_frame）---- */

static int apply_frame(int p, const uint8_t axes[4], int fields_, int *rowp)
{
    int dirty = 0;
    const int rs = rstick_flick(axes);
    int row;
    if (seat == SEAT_HI) {
        if (step_deva_frame(p, axes, rs, fields_))
            dirty = 1;
        row = dvm.lsLatch * 3 + dvm.nonVargaLayer;
    } else {
        GpFrameKana f = frame_of(p, fields_ * 17);
        row = f.row;
        GpAction a[4];
        const int an = gp_step_kana(&gm, seat_lang(), &f, a);
        for (int i = 0; i < an; i++) {
            if (conv_active())
                conv_accept();
            if (a[i].type == GPA_KANA) {
                comp_insert(a[i].text, a[i].tlen, a[i].replace);
            } else if (a[i].type == GPA_YOUON) {
                uint16_t o_[2];
                int rep;
                const int l = gp_resolve_youon(comp, caret, o_, &rep);
                if (l && rep > 0)
                    comp_insert(o_, l, rep);
            } else {
                uint16_t ch;
                int rep;
                if (ko_apply(&ko, &a[i], &ch, &rep))
                    comp_insert(&ch, 1, rep);
            }
            dirty = 1;
        }
        gp_sync_prev_kana(&gm, &f);
        if (rs)
            gp_break_rt_cycle(&gm);
    }

    if (rs == 2 && seat != SEAT_HI) {
        if (seat == SEAT_EN) {
            const uint16_t ch = 0x0027;
            comp_insert(&ch, 1, 0);
            dirty = 1;
        } else if (seat == SEAT_KO) {
            uint16_t ch;
            int rep;
            if (ko_cycle(&ko, &ch, &rep)) {
                comp_insert(&ch, 1, rep);
                dirty = 1;
            }
        } else {
            uint16_t o_[2];
            int rep;
            const int l = gp_resolve_dakuten(comp, caret, o_, &rep);
            if (l) {
                comp_insert(o_, l, rep);
                dirty = 1;
            }
        }
    }
    if (rs == 1) {
        if (conv_active()) {
            conv_cancel();
            dirty = 1;
        } else if (caret) {
            if (seat == SEAT_HI)
                deva_backspace(&dv);
            comp_bs();
            gm.eagerSet = 0;
            ko_commit(&ko);
            dirty = 1;
        }
    }
    if (rs == 3 && seat != SEAT_HI) {
        if (conv_active())
            conv_accept();
        if (seat == SEAT_KO) {
            uint16_t ch;
            int rep;
            if (ko_add_aeo(&ko, &ch, &rep)) {
                comp_insert(&ch, 1, rep);
                dirty = 1;
            }
        } else {
            const uint16_t ch = seat == SEAT_EN ? 0x002F : 0x30FC;
            comp_insert(&ch, 1, 0);
            if (seat == SEAT_EN)
                gm.engSmartCaps = 0;
            dirty = 1;
        }
    }
    if (rs == 4) {
        if (conv_active())
            conv_accept();
        const int within = punct_at && fields_ < punct_at;
        const int step_n = within ? punct_n + 1 : 1;
        static const uint16_t PUNCT_JA[] = { 0x3001, 0x3002, 0x3000 };
        static const uint16_t PUNCT_EN[] = { 0x0020, 0x002E, 0x002C };
        static const uint16_t PUNCT_KO[] = { 0x0020, 0x002E, 0x003F, 0x002C,
                                             0x300E, 0x300F };
        static const uint16_t PUNCT_HI[] = { 0x0020, 0x0964, 0x002C, 0x0965 };
        const uint16_t *tbl = seat == SEAT_EN ? PUNCT_EN
                            : seat == SEAT_KO ? PUNCT_KO
                            : seat == SEAT_HI ? PUNCT_HI : PUNCT_JA;
        const int steps = seat == SEAT_KO ? 6 : seat == SEAT_HI ? 4 : 3;
        int idx = step_n - 1;
        if (idx >= steps)
            idx = steps - 1;
        const uint16_t ch = tbl[idx];
        const int rep = step_n > 1 ? 1 : 0;
        if (seat == SEAT_EN)
            gm.engSmartCaps = 0;
        ko_commit(&ko);
        deva_commit(&dv);
        comp_insert(&ch, 1, rep);
        if (step_n < steps) {
            punct_n = step_n;
            punct_at = fields_ + PUNCT_WINDOW;
        }
        dirty = 1;
    }

    *rowp = row;
    return dirty;
}

/* ---- 席を替える（Select / 起動画面）---- */

static void reset_machines(void)
{
    gm.engShiftNext = 0;
    gm.engSmartCaps = 0;
    gm.engCapsLock = 0;
    gm.eagerSet = 0;
    gm.korAllReleased = 1;
    gm.korPatchimRollback = 0;
    ko_commit(&ko);
    deva_machine_init(&dvm);
    deva_init(&dv);
    punct_break();
}

static void set_seat(int s, int p, const uint8_t axes[4])
{
    seat = s;
    ui_lang = ui_lang_for_seat(seat);
    reset_machines();
    GpFrameDeva df = deva_frame_of(p, axes, 0, 0);
    gp_sync_prev_deva(&dvm, &df);
    say("hello", hg_eliza_greeting(seat_slot()));
}

/* ================= JS から呼ぶ口 ================= */

/* 起動。★ELIZA の 4 席を読む（変換は後から hg_conv_ready）。戻り値 = 読めた席の数。 */
EMSCRIPTEN_KEEPALIVE int hg_init(void)
{
    gp_init(&gm);
    deva_machine_init(&dvm);
    deva_init(&dv);
    int ok = 0;
    ok += hg_eliza_init(HG_JA, _binary_script_txt_start,
                        (int)(_binary_script_txt_end - _binary_script_txt_start), 0);
    ok += hg_eliza_init(HG_EN, _binary_script_en_txt_start,
                        (int)(_binary_script_en_txt_end - _binary_script_en_txt_start), 1);
    ok += hg_eliza_init(HG_KO, _binary_script_ko_txt_start,
                        (int)(_binary_script_ko_txt_end - _binary_script_ko_txt_start), 0);
    ok += hg_eliza_init(HG_HI, _binary_script_hi_txt_start,
                        (int)(_binary_script_hi_txt_end - _binary_script_hi_txt_start), 0);
    return ok;
}

/* 変換エンジンが揃った（JS が hechima を初期化し終えた）。1 = 使える。 */
EMSCRIPTEN_KEEPALIVE int hg_conv_ready(void)
{
    return conv_init("", "");
}

/* 起動画面で席を選んだ。挨拶を積む。 */
EMSCRIPTEN_KEEPALIVE void hg_start(int s, int p, int a0, int a1, int a2, int a3)
{
    const uint8_t axes[4] = { (uint8_t)a0, (uint8_t)a1, (uint8_t)a2, (uint8_t)a3 };
    clen = caret = fixed = 0;
    set_seat(s, p, axes);
    carry_over(p, axes);
}

/* メニューから戻った。押されたままのものを引き継ぐ。 */
EMSCRIPTEN_KEEPALIVE void hg_resync(int p, int a0, int a1, int a2, int a3)
{
    const uint8_t axes[4] = { (uint8_t)a0, (uint8_t)a1, (uint8_t)a2, (uint8_t)a3 };
    carry_over(p, axes);
}

/* 1 フレーム（1/60 秒）進める。戻り値のビット:
     1 = 入力欄が変わった / 2 = Start が押された（JS がメニューを開く） */
EMSCRIPTEN_KEEPALIVE int hg_step(int p, int a0, int a1, int a2, int a3)
{
    const uint8_t axes[4] = { (uint8_t)a0, (uint8_t)a1, (uint8_t)a2, (uint8_t)a3 };
    int ret = 0;
    fields++;
    const int edge = p & ~pad_prev;
    pad_prev = p;

    int row;
    if (apply_frame(p, axes, fields, &row))
        ret |= 1;
    if (row != cur_row)
        ret |= 1;
    cur_row = row;

    if (edge & BTN_R3) {
        if (conv_active()) {
            conv_cancel();
            ret |= 1;
        } else if (caret > fixed) {
            comp_drop_pending();
            gm.eagerSet = 0;
            ret |= 1;
        }
    }

    if ((edge & BTN_SELECT) && !clen) {
        set_seat((seat + 1) % SEAT_N, p, axes);
        ret |= 1;
    }

    if (edge & BTN_START)
        ret |= 2;

    if (seat != SEAT_HI) {
        if ((edge & BTN_L3) && conv_active()) {
            conv_accept();
            gm.eagerSet = 0;
            ret |= 1;
        } else if ((edge & BTN_L3) && caret > fixed) {
            fixed = caret;
            gm.eagerSet = 0;
            ret |= 1;
        } else if ((edge & BTN_L3) && send_line()) {
            ret |= 1;
        }
    }

    int ls[4];
    ls_dirs(axes, ls);
    if (seat == SEAT_HI) {
        for (int d = 0; d < 4; d++) {
            ls_prev[d] = ls[d];
            ls_at[d] = 0;
        }
    } else {
        const int rt = (p & BTN_R2) != 0;
        for (int d = 0; d < 4; d++) {
            int fire = 0;
            if (!ls[d]) {
                ls_at[d] = 0;
            } else if (!ls_prev[d]) {
                ls_at[d] = fields + LS_DELAY;
                fire = 1;
            } else if (ls_at[d] && fields >= ls_at[d]) {
                /* ★変換中はリピートしない（候補送りの暴走を防ぐ）。
                   スクロールはリピートさせる。 */
                if (!conv_active()) {
                    ls_at[d] = fields + LS_INTERVAL;
                    fire = 1;
                }
            }
            ls_prev[d] = ls[d];
            if (!fire)
                continue;
            switch (d) {
            case 0:
                if (conv_active()) {
                    if (rt) { conv_resize(-1); gp_consume_rt(&gm); }
                    else conv_move(-1);
                    ret |= 1;
                } else if (caret > 0) {
                    caret_set(caret - 1);
                    gm.eagerSet = 0;
                    ret |= 1;
                }
                break;
            case 1:
                if (conv_active()) {
                    if (rt) { conv_resize(+1); gp_consume_rt(&gm); }
                    else conv_move(+1);
                    ret |= 1;
                } else if (caret < clen) {
                    caret_set(caret + 1);
                    gm.eagerSet = 0;
                    ret |= 1;
                }
                break;
            case 2:
                if (conv_active()) {
                    conv_prev();
                    ret |= 1;
                } else {
                    ev_scroll(-1);
                }
                break;
            case 3:
                if (conv_active()) {
                    conv_next();
                    ret |= 1;
                } else if (caret > fixed) {
                    if (conv_start(comp + fixed, caret - fixed))
                        ret |= 1;
                } else {
                    ev_scroll(1);
                }
                break;
            }
        }
    }

    if (ev_n)
        ret |= 1;
    return ret;
}

/* 溜まった出来事を JSON の配列で返して空にする。 */
EMSCRIPTEN_KEEPALIVE const char *hg_take(void)
{
    static char *out;
    free(out);
    out = malloc(ev_n + 3);
    out[0] = '[';
    if (ev_n)
        memcpy(out + 1, ev, ev_n);
    out[ev_n + 1] = ']';
    out[ev_n + 2] = 0;
    ev_n = 0;
    return out;
}

/* ---- 図の札（native の help_left_char / help_cell_char）---- */

static uint16_t left_char(int row)
{
    if (seat == SEAT_KO)
        return ko_row_char(row);
    if (seat == SEAT_EN)
        return gp_cell(1, row, 0);
    return gp_row_char(row);
}

static uint16_t cell_char(int row, int vowel, int rt)
{
    if (seat == SEAT_KO)
        return ko_vowel_char(vowel, rt);
    return gp_cell(seat == SEAT_EN, row, vowel);
}

/* 英語の十字の 2 段目（その行の面ボタンの字。プッシュホンの「2 abc」）。 */
static void js_en_row(int row, int upper)
{
    uint16_t t[4];
    const int n = gp_row_chars_en(row, t, 4);
    for (int i = 0; i < n; i++)
        if (upper && t[i] >= 'a' && t[i] <= 'z')
            t[i] -= 0x20;
    js_u16(t, n);
}

static void js_pad(int p)
{
    const UiText *u = &UI[ui_lang];
    js_raw("\"pad\":{");
    if (seat == SEAT_HI) {
        const int lt = (p & BTN_L2) != 0;
        /* 十字: ↑ → ↓ ← が組の 4 つ。★中央は空（十字を押さないと子音は出ない） */
        js_raw("\"dpad\":{\"c\":\"\",\"u\":");
        js_ch(deva_stop_char(&dvm, 0));
        js_raw(",\"r\":");
        js_ch(deva_stop_char(&dvm, 1));
        js_raw(",\"d\":");
        js_ch(deva_stop_char(&dvm, 2));
        js_raw(",\"l\":");
        js_ch(deva_stop_char(&dvm, 3));
        js_raw("},\"face\":{\"r1\":");
        js_ch(lt ? deva_mark_char(DEVA_MARK_NUKTA) : deva_vowel_char(0, 0));
        js_raw(",\"sq\":");
        js_ch(deva_vowel_char(1, lt));
        js_raw(",\"tri\":");
        js_ch(deva_vowel_char(2, lt));
        js_raw(",\"cir\":");
        js_ch(deva_vowel_char(3, lt));
        js_raw(",\"x\":");
        js_ch(deva_vowel_char(4, lt));
        js_raw("},\"l1c\":");
        js_ch(deva_stop_char(&dvm, 4));
        js_raw(",\"r2c\":");
        js_ch(deva_mark_char(lt ? DEVA_MARK_VISARGA : DEVA_MARK_HALANT));
        js_raw(",\"center\":0");
    } else {
        const int base = (p & BTN_L1) ? 5 : 0;
        const int row = pad_row(p);
        const int rt = (p & BTN_R2) != 0;
        const int up = seat == SEAT_EN && (gm.engCapsLock || gm.engSmartCaps || gm.engShiftNext);
        static const int ORD[5] = { 0, 1, 2, 3, 4 };   /* 中・←・↑・→・↓ */
        static const char *const KEY[5] = { "c", "l", "u", "r", "d" };
        js_raw("\"dpad\":{");
        for (int i = 0; i < 5; i++) {
            if (i)
                js_raw(",");
            js_raw("\"");
            js_raw(KEY[i]);
            js_raw("\":");
            js_ch(left_char(base + ORD[i]));
        }
        js_raw("}");
        if (seat == SEAT_EN) {
            js_raw(",\"dsub\":{");
            for (int i = 0; i < 5; i++) {
                if (i)
                    js_raw(",");
                js_raw("\"");
                js_raw(KEY[i]);
                js_raw("\":");
                js_en_row(base + ORD[i], up);
            }
            js_raw("}");
        }
        js_raw(",\"face\":{");
        static const char *const FK[5] = { "r1", "sq", "tri", "cir", "x" };
        for (int v = 0; v < 5; v++) {
            uint16_t c = cell_char(row, v, rt);
            if (up && c >= 'a' && c <= 'z')
                c -= 0x20;
            if (v)
                js_raw(",");
            js_raw("\"");
            js_raw(FK[v]);
            js_raw("\":");
            js_ch(c);
        }
        js_raw("},\"l1c\":\"\",\"r2c\":\"\",\"center\":1");
    }
    js_raw(",\"l1\":");
    js_u8(u->l1[seat]);
    js_raw(",\"l2\":");
    js_u8(u->l2[seat]);
    js_raw(",\"r2\":");
    js_u8(u->r2[seat]);
    js_raw(",\"r1\":");
    js_u8(u->r1);
    js_raw("}");
}

/* いまの状態（入力欄・候補・インジケータ・図の札）。p = いまの押下（図の札が L1 / L2 / R2 で替わる）。 */
EMSCRIPTEN_KEEPALIVE const char *hg_state(int p)
{
    static uint16_t t[COMP_MAX * 3];
    js_n = 0;
    js_raw("{\"seat\":");
    js_int(seat);
    js_raw(",\"ui\":");
    js_int(ui_lang);
    js_raw(",\"conv\":");
    js_int(conv_active());
    js_raw(",\"convReady\":");
    js_int(conv_ready());

    /* 入力欄。★native の interactive と同じ組み方 —— 変換中は
       「確定済み ＋ 変換中の文節 ＋ キャレットより後ろの確定済み」。 */
    int tn = 0, ua, ub, fa = -1, fb = -1, car;
    if (conv_active()) {
        const int tmax = (int)(sizeof t / sizeof *t);
        for (int i = 0; i < fixed && tn < tmax; i++)
            t[tn++] = comp[i];
        ua = tn;
        int a = 0, b = 0;
        const int cn = conv_text(t + tn, tmax - tn, &a, &b);
        fa = a + tn;
        fb = b + tn;
        tn += cn;
        ub = tn;
        for (int i = caret; i < clen && tn < tmax; i++)
            t[tn++] = comp[i];
        car = -1;
    } else {
        for (int i = 0; i < clen; i++)
            t[tn++] = comp[i];
        ua = fixed;
        ub = caret;
        car = caret;
    }
    js_raw(",\"text\":");
    js_u16(t, tn);
    js_raw(",\"ua\":");
    js_int(ua);
    js_raw(",\"ub\":");
    js_int(ub);
    js_raw(",\"fa\":");
    js_int(fa);
    js_raw(",\"fb\":");
    js_int(fb);
    js_raw(",\"caret\":");
    js_int(car);

    /* 候補。★並べ方（5 件で 1 ページ・追加候補は上に別枠）は JS が native に揃える */
    js_raw(",\"cands\":[");
    const int n = conv_active() ? conv_cand_count() : 0;
    for (int i = 0; i < n; i++) {
        uint16_t c[CONV_STR_MAX];
        const int cn = conv_cand(i, c, CONV_STR_MAX);
        if (i)
            js_raw(",");
        js_u16(c, cn);
    }
    js_raw("],\"sel\":");
    js_int(n ? conv_cand_index() : -1);
    js_raw(",\"addl\":");
    js_int(n ? conv_addl_count() : 0);

    uint16_t ind[4], mark = 0;
    const int il = row_indicator(cur_row, ind, &mark);
    js_raw(",\"ind\":");
    js_u16(ind, il);
    js_raw(",\"mark\":");
    js_ch(mark);
    js_raw(",");
    js_pad(p);
    js_raw("}");
    return js;
}

/* 画面の文言（ui.h から）。★JS に文言を持たせない —— 写すと UI 言語を足したときにずれる。 */
EMSCRIPTEN_KEEPALIVE const char *hg_ui(int lang)
{
    if (lang < 0 || lang >= UI_LANG_N)
        lang = UI_EN;
    const UiText *u = &UI[lang];
    js_n = 0;
    js_raw("{\"title\":");
    js_u8(UI_BOOT_TITLE);
    js_raw(",\"sub\":");
    js_u8(UI_BOOT_SUB);
    js_raw(",\"work\":");
    js_u8(UI_BOOT_WORK);
    js_raw(",\"seats\":[");
    for (int s = 0; s < SEAT_N; s++) {
        if (s)
            js_raw(",");
        js_u8(UI_SEAT_NAME[s]);
    }
    js_raw("],\"seatUi\":[");
    for (int s = 0; s < SEAT_N; s++) {
        if (s)
            js_raw(",");
        js_int(ui_lang_for_seat(s));
    }
    js_raw("],\"menu\":{\"license\":");
    js_u8(u->menu[UI_MENU_LICENSE]);
    js_raw(",\"quit\":");
    js_u8(u->menu[UI_MENU_QUIT]);
    js_raw("},\"licTitle\":");
    js_u8(u->lic_title);
    js_raw(",\"licNote\":[");
    js_u8(u->lic_note[0]);
    js_raw(",");
    js_u8(u->lic_note[1]);
    js_raw("],\"lic\":[");
    for (int i = 0; i < UI_LIC_N; i++) {
        if (i)
            js_raw(",");
        js_raw("[");
        js_u8(UI_LIC[i].name);
        js_raw(",");
        js_u8(UI_LIC[i].license);
        js_raw(",");
        js_u8(UI_LIC[i].who);
        js_raw("]");
    }
    js_raw("],\"bootSel\":");
    js_u8(u->boot_sel);
    js_raw(",\"bootHint\":");
    js_u8(u->boot_hint);
    js_raw(",\"note\":[");
    for (int s = 0; s < SEAT_N; s++) {
        if (s)
            js_raw(",");
        js_raw("[");
        js_u8(u->note[s][0]);
        js_raw(",");
        js_u8(u->note[s][1]);
        js_raw("]");
    }
    js_raw("]}");
    return js;
}
