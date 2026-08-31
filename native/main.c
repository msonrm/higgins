/* Higgins — ELIZA をゲームパッドだけで打つ（PortMaster / デスクトップ）。
 *
 * ★機械に触るのは `plat.h` の 9 本、字は `glyph.h` の 5 本だけ。どちらも Zenmai の
 *   殻をそのまま vendoring している（版は vendor/VENDOR.md に pin）。
 *   組版・折返し・禁則・履歴・表示窓（render.c）と、かな入力の状態機械（input.c）も
 *   **1 行も変えずに**載っている。
 *
 * 打った字の流れ:
 *   ゲームパッド → input.c（同時押しで 1 字）→ comp[] → wakachi（分かち書き）
 *     → ELIZA（vendor・無改造）→ tsunagi（空白を繋ぐ）→ 履歴 → 画面
 *
 * ★環境変数が `ZENMAI_` のままなのは意図的 —— `plat_sdl.c` を 1 バイトも変えない
 *   ことで、上流と md5 で照合できる状態を保つため（改名は「殻を fork する」に等しい）。
 *     ZENMAI_WINDOW=1  全画面ではなく窓で出す
 *     ZENMAI_FONT=<..> 使うフォント（既定は実行ファイルの隣 → システム）
 *     ZENMAI_SCRIPT / ZENMAI_STOP / ZENMAI_RAW  台本再生と画面の書き出し（検査用）
 */
#include "render.h"
#include "input.h"
#include "text.h"
#include "wakachi.h"
#include "tsunagi.h"
#include "eliza_bridge.h"

/* build.sh が objcopy で焼き込む DOCTOR スクリプト（UTF-8 の S 式）。
   ★ファイルを外に置かないのは、「同梱してある」という主張を配布の形に
     依存させないため（Zenmai の story.bin と同じ判断）。 */
extern const char _binary_script_txt_start[];
extern const char _binary_script_txt_end[];

/* ---- 画面 ---- */

/* 画面いっぱいを 1 色で塗る。
   ★**GPU のフィル（gp0_fill）は地色に使わない**（Zenmai から写した判断）——
     字のある行はバッファ転送で描くので、塗りと転送が混ざると地色がほんのわずかに
     食い違う。転送は 15bit をそのまま書き、フィルは 24bit を落として書く別経路。
     同じ道で塗れば食い違いようがない。 */
enum { BAND_H = 24 };
static uint16_t band[BAND_H][W];

static void paint_screen(uint16_t color)
{
    fill_rows(band, 0, BAND_H, color);
    for (int y = 0; y < H; y += BAND_H)
        gp0_upload(0, y, W, BAND_H, band[0]);
}

/* UTF-8 の 1 行を履歴へ積む。★折返し・禁則は render.c が持っている。 */
static void say(const char *utf8, uint16_t color)
{
    uint16_t buf[1024];
    const int n = tsunagi(utf8, buf, (int)(sizeof buf / sizeof *buf));
    hist_line(buf, n, color);
}

/* ---- 打っている字（合成バッファ）---- */

static GpMachine gm;
/* ★Zenmai は 36 字（Zork のコマンドは短い）。Higgins は**自由文**を打つので広げる。
   ★**打つのは命令ではなく「相談」**なので、1 行に収まる前提そのものが違う
     （欄は下の draw_input が複数行で描き、溢れたぶんは流れる）。 */
enum { COMP_MAX = 96 };
static uint16_t comp[COMP_MAX];
static int clen, caret;

static void comp_insert(const uint16_t *t, int tn, int replace)
{
    int del = replace > caret ? caret : replace;
    for (int i = caret - del; i + del < clen; i++)
        comp[i] = comp[i + del];
    clen -= del;
    caret -= del;
    /* ★上限は**字数だけ**。Zenmai は「1 行に収まる幅」でも止めていたが、
       こちらの欄は溢れたら流れるので、幅で止める理由が無い。 */
    if (clen + tn > COMP_MAX)
        tn = COMP_MAX - clen;
    for (int i = clen - 1; i >= caret; i--)
        comp[i + tn] = comp[i];
    for (int i = 0; i < tn; i++)
        comp[caret + i] = t[i];
    clen += tn;
    caret += tn;
}

static void comp_bs(void)
{
    if (!caret)
        return;
    for (int i = caret - 1; i + 1 < clen; i++)
        comp[i] = comp[i + 1];
    clen--;
    caret--;
}

/* ---- 入力欄（複数行）----
 *
 * ★殻の `draw_strip`（1 行）は使わない。**Zork のコマンドは 1 行に収まるが、
 *   相談文は収まらない** —— DOCTOR に打つのは命令ではなく文なので、前提が違う。
 * ★**殻（render.c）には手を入れない。** `glyph.h` の `draw24` と `plat.h` の
 *   `gp0_upload` を直に使って自前で描く —— Zenmai に要らない機能を殻へ入れると、
 *   PS1 側が払えない重さが共有コードに紛れ込む（VENDOR.md「殻の本家は Zenmai」）。
 * ★見た目（＞ / キャレット / 右端の印の箱）は `build_strip` から写した。
 */
enum {
    BOX_ROWS  = 3,                     /* 見えている行数。★固定 = 本文窓は動かない */
    BOX_H     = BOX_ROWS * BASE,
    BOX_Y     = H - 24 - BOX_H,        /* 下セーフ 24px は Zenmai と同じ */
    BOX_X0    = MARGIN + 24,           /* ＞ のぶん右へ。★2 行目以降も揃える */
    /* 右端。★印の箱（左枠が W-MARGIN-27）に**3px の隙間を空けて**止める ——
       Zenmai の 1 行版は `W-MARGIN-24` まで描いていて箱と重なりうるが、
       あちらは入力側でも幅を絞っていたので表に出なかった。 */
    BOX_X1    = W - MARGIN - 30,
    BOX_LINES = 16,                    /* 折返しの上限（96 字なら 5 行で足りる） */
};
static uint16_t box[BOX_H][W];

/* 折返して各行の開始位置を出す。戻り値 = 行数（必ず 1 以上）。 */
static int box_layout(const uint16_t *s, int n, int *start)
{
    int nl = 0;
    start[nl++] = 0;
    int x = BOX_X0;
    for (int i = 0; i < n; i++) {
        const int w = glyph_w(s[i]);
        if (x + w > BOX_X1) {
            if (nl >= BOX_LINES)
                break;
            start[nl++] = i;
            x = BOX_X0;
        }
        x += w;
    }
    return nl;
}

/* 打っている字と行インジケータ（ind = 0 なら箱だけ）を描いて送る。 */
static void draw_input(const uint16_t *s, int n, int car, uint16_t ind)
{
    int start[BOX_LINES];
    const int nl = box_layout(s, n, start);

    int cl = 0;                        /* キャレットのある行 */
    for (int i = 0; i < nl; i++)
        if (start[i] <= car)
            cl = i;

    /* 見える窓をキャレットに追従させる（★キャレットは常に見えている） */
    int top = cl - (BOX_ROWS - 1);
    if (top > nl - BOX_ROWS)
        top = nl - BOX_ROWS;
    if (top < 0)
        top = 0;

    fill_rows(box, 0, BOX_H, BG);
    /* ★「＞」は窓の先頭行に置く（行が流れても目印が動かない）。
       ★上に隠れている行があるときは**薄く**する —— 遡れることが分かる最小の印。 */
    draw24(box, BOX_H, MARGIN, 0, 0xFF1E, top ? DIM : ACCENT);

    for (int r = 0; r < BOX_ROWS && top + r < nl; r++) {
        const int i0 = start[top + r];
        const int i1 = (top + r + 1 < nl) ? start[top + r + 1] : n;
        const int y = r * BASE;
        int x = BOX_X0, cx = BOX_X0;
        for (int i = i0; i < i1; i++) {
            if (i == car)
                cx = x;
            if (x + glyph_w(s[i]) > BOX_X1)
                break;                 /* ★draw24 は横の範囲を見ない */
            draw24(box, BOX_H, x, y, s[i], INK);
            x += glyph_w(s[i]);
        }
        if (top + r != cl)
            continue;
        if (car >= i1) {               /* 行末: 字の右へ 3px */
            cx = x;
            for (int rr = 0; rr < BASE; rr++)
                for (int c = 0; c < 3; c++)
                    box[y + rr][cx + 2 + c] = ACCENT;
        } else {                       /* 途中: 字の左へ 2px */
            for (int rr = 0; rr < BASE; rr++)
                for (int c = 0; c < 2; c++)
                    box[y + rr][cx - 1 + c] = ACCENT;
        }
    }

    /* 行インジケータの箱（★1 行目の右端。build_strip と同じ形） */
    int iw = ind ? glyph_w(ind) : 0;
    if (iw < 24)
        iw = 24;
    const int kx = W - MARGIN - iw;
    for (int c = kx - 3; c <= kx + iw + 2; c++)
        box[0][c] = box[BASE - 1][c] = DIM;
    for (int r = 0; r < BASE; r++)
        box[r][kx - 3] = box[r][kx + iw + 2] = DIM;
    if (ind)
        draw24(box, BOX_H, kx, 0, ind, ACCENT);

    gp0_upload(0, BOX_Y, W, BOX_H, box[0]);
}

/* ---- パッド → 行 / 段 ----
 * ★**図を描く側と入力する側で同じ式を使う**（Zenmai の注記のまま）。写すと、
 *   図に出ている字と実際に入る字がずれても誰も気づけない。
 * 行: 何も押さない=0 / ←=1 / ↑=2 / →=3 / ↓=4。L1 を押すと +5（はまやらわ）。
 * 段: R1=あ / □=い / △=う / ○=え / ×=お。-1 = まだ選んでいない。
 */
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

static GpFrame frame_of(int p, int ms)
{
    const int cc = !!(p & BTN_LEFT) + !!(p & BTN_UP) + !!(p & BTN_RIGHT)
                 + !!(p & BTN_DOWN) + !!(p & BTN_L1);
    const int vowel = pad_vowel(p);
    GpFrame f = { ms, pad_row(p), vowel, vowel >= 0,
                  !!(p & BTN_L2), !!(p & BTN_R2), cc };
    return f;
}

/* ★対話ループに入るときの引き継ぎ。**押されたままのものを「もう見た」ことにする**。
   忘れると、押しっぱなしのボタンに応じた字が入った状態で始まる
   （Zenmai が 2026-08-29 に実機で踏んだ形。台本が素通りしていた）。 */
static void carry_over_pad(void)
{
    pad_prev = pad_read();
    if (pad_prev < 0)
        pad_prev = 0;
    GpFrame f = frame_of(pad_prev, 0);
    gp_sync_prev(&gm, &f);
}

/* ---- 対話 ---- */

/* 打ち終わった 1 文を ELIZA へ渡し、返事を積む。 */
static void send(const uint16_t *typed, int n)
{
    static char wa[4096];
    wakachi(typed, n, wa, (int)sizeof wa);
    draw_echo(typed, n);                       /* 打った通りを反響（＞付き） */
    hist_blank();
    say(hg_eliza_response(wa), INK);
    scroll_new();
}

/* ★マッピングは GIME 準拠（docs/gamepad-mapping.md）。
 *
 *   ★**Zenmai の「アナログスティック無しでも打てる」代替路は写していない。**
 *     あちらは PS1（初代のパッドにスティックが無い）を想定していたので、
 *     濁点を Select にも、BS を R2 長押しにも、確定を Start にも置いていた。
 *     Higgins は**かな漢字変換をやる以上スティックが要る**（変換操作を置く場所が
 *     他に無い。スティックの無い機種は同時に RAM 128〜256MB で、mozc の辞書
 *     18.9MB がそもそも載らない）ので、道を 1 本にする ——
 *     ★**二重の道を残したまま段階 D で片方を剥がすと、「どちらでも打てる」状態が
 *       検査を素通りする。**
 *
 *   左手 D-pad(+L1) = 子音行 / 右手 面ボタン(+R1) = 母音 …… 同時押しで 1 字
 *   L2 = 拗音シフト / R2 = ん（連打で を・んを）
 *   右スティック ← BS / ↑ 濁点・半濁点 / → 長音「ー」
 *   左スティック ←→ キャレット移動 / ↑↓ 履歴を遡る（★打っていないときだけ）
 *   L3 押し込み = 確定して ELIZA へ / R3 押し込み = 打った行を捨てる
 *   Start = やめる（★段階 E でオプション画面に置き換える）
 *
 *   ★段階 D（mozc）で足りる余地を空けてある: 打っている最中の**左スティック縦**
 *     （↓ 変換開始 / ↑ 前候補）と、**右スティック↓**。
 */
static void interactive(void)
{
    int fields = 0, dirty = 1;
    int ls_prev = 0, ls_rep = 0;
    uint16_t prev_rowchar = 0;

    gp_init(&gm);
    clen = 0;
    caret = 0;
    carry_over_pad();

    for (;;) {
        wait_fields(1);
        fields++;
        uint8_t axes[4];
        const int p = pad_read_ex(axes);
        if (p < 0) {                   /* 繋がっていない */
            pad_prev = 0;
            continue;
        }
        const int edge = p & ~pad_prev;
        pad_prev = p;

        /* かな（同時押し）*/
        GpFrame f = frame_of(p, fields * 17);
        const int row = f.row;
        GpAction a[4];
        const int an = gp_step(&gm, 0 /* japanese */, &f, a);
        for (int i = 0; i < an; i++) {
            if (a[i].type == GPA_KANA) {
                comp_insert(a[i].text, a[i].tlen, a[i].replace);
            } else {                   /* GPA_YOUON（後置の拗音シフト） */
                uint16_t o_[2];
                int rep;
                const int l = gp_resolve_youon(comp, caret, o_, &rep);
                if (l && rep > 0)
                    comp_insert(o_, l, rep);
            }
            dirty = 1;
        }
        gp_sync_prev(&gm, &f);

        /* 右スティックのフリック（1=← 2=↑ 3=→ 4=↓） */
        const int rs = pad_rstick_flick(axes);
        if (rs)
            gp_break_rt_cycle(&gm);
        if (rs == 2) {                 /* ↑ 濁点・半濁点をトグル */
            uint16_t o_[2];
            int rep;
            const int l = gp_resolve_dakuten(comp, caret, o_, &rep);
            if (l) {
                comp_insert(o_, l, rep);
                dirty = 1;
            }
        }
        if (rs == 1 && caret) {        /* ← BS */
            comp_bs();
            gm.eagerSet = 0;
            dirty = 1;
        }
        if (rs == 3) {                 /* → 長音「ー」 */
            const uint16_t ch = 0x30FC;
            comp_insert(&ch, 1, 0);
            dirty = 1;
        }

        if ((edge & BTN_R3) && clen) { /* R3 = 打った行を捨てる */
            clen = 0;
            caret = 0;
            gm.eagerSet = 0;
            dirty = 1;
        }

        if ((edge & BTN_START) && !clen)   /* ★段階 E でオプション画面に置き換える */
            break;

        if ((edge & BTN_L3) && clen) { /* L3 = 確定して ELIZA へ */
            /* 遡っている最中の確定は、まず下端へ跳んでひと呼吸置く */
            if (view_px < hist_total() - body_h) {
                view_bottom();
                render_window();
                wait_fields(18);
            }
            static uint16_t typed[COMP_MAX];
            const int typed_n = clen;
            for (int i = 0; i < clen; i++)
                typed[i] = comp[i];
            clen = 0;
            caret = 0;
            gm.eagerSet = 0;
            dirty = 1;
            send(typed, typed_n);
        }

        /* 左スティック縦 = 履歴を遡る。
           ★**打っていないときだけ**（Zenmai は常に効く）。打っている最中の縦は
             段階 D で変換（↓ 変換開始 / ↑ 前候補）に使うので、いま空けておく。 */
        if (fields % 3 == 0 && clen == 0) {
            if (axes[3] < 0x40)
                view_scroll(-SCROLL_STEP);
            if (axes[3] > 0xC0)
                view_scroll(SCROLL_STEP);
        }

        /* 左スティック横 = キャレット移動（押し続けで連続） */
        {
            const int d = axes[2] < 0x40 ? -1 : axes[2] > 0xC0 ? 1 : 0;
            int fire = 0;
            if (d != ls_prev) {
                ls_prev = d;
                ls_rep = 0;
                fire = d != 0;
            } else if (d && ++ls_rep >= 6) {
                ls_rep = 0;
                fire = 1;
            }
            if (fire) {
                const int nc = caret + d;
                if (nc >= 0 && nc <= clen && nc != caret) {
                    caret = nc;
                    gm.eagerSet = 0;
                    dirty = 1;
                }
            }
        }

        const uint16_t rowchar = gp_row_char(row);
        if (dirty || rowchar != prev_rowchar) {
            draw_input(comp, clen, caret, rowchar);
            prev_rowchar = rowchar;
            dirty = 0;
        }
    }
}

int main(void)
{
    gpu_init();
    render_init();
    /* ★入力欄を 3 行に広げたぶん、本文窓を縮める（render_init の既定を上書き）。
       ★欄の高さは**固定**なので、打っている途中に本文が動くことはない。 */
    body_top = TOP;
    body_h = BOX_Y - 8 - body_top;
    paint_screen(BG);

    const int len = (int)(_binary_script_txt_end - _binary_script_txt_start);
    if (!hg_eliza_init(_binary_script_txt_start, len))
        say("スクリプトが読めない。", INK);
    else
        say(hg_eliza_greeting(), INK);

    view_bottom();
    render_window();
    draw_input(0, 0, 0, 0);
    gpu_display_on();

    pad_try_analog();
    interactive();
    return 0;
}
