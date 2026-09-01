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
#include "conv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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
/* ★`comp[0..fixed)` は**確定済み**（もう変換しない）。
   IME でいう「確定した本文」にあたる —— Higgins には本文の欄が無く、打っている欄が
   そのまま入力なので、境界をここで持つ。
   ★これが無いと、確定したあとに続けて打って変換すると**確定済みの字まで
     もう一度変換してしまう**（実機の指摘・2026-08-31）。 */
static int fixed;

static void comp_insert(const uint16_t *t, int tn, int replace)
{
    /* ★確定済みの中を編集したら、そこから先は未確定に戻す */
    if (fixed > caret)
        fixed = caret;
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
    if (fixed > caret)
        fixed = caret;
}

/* 未確定（`comp[fixed..caret)`）だけを捨てる。
 * ★**確定済みには触らない。** キャンセルは「まだ決まっていないものを取り消す」操作で、
 *   決まったものを消すのは BS の仕事（実機の指摘・2026-09-01）。
 *   hechima のキャンセル（Escape）も、変換中なら**よみへ戻し**、よみだけなら
 *   **そのよみを捨て**、未確定が無ければ**何もしない** —— 本文は最後まで残る。 */
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

/* キャレットを動かす。★動かしたら**未確定はそこで終わる**（`fixed = caret`）——
 * ★置いていくと「右へ動かしただけ」で未確定が生まれ、確定済みの字が変換対象に
 *   なってしまう（実機の指摘・2026-09-01）。
 * ★言い換えると **キャレットは常に未確定の末尾にいる**、という約束にする。
 *   未確定の中を動きたい場面は無い —— 打ち直しは BS、変換位置の調整は文節移動。
 */
static void caret_set(int pos)
{
    if (pos < 0 || pos > clen)
        return;
    caret = pos;
    fixed = pos;
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
    /* ★行数は**打った量で伸びる**（1 行から始まり、3 行が上限。そこから先は流れる）。
       実機の指摘（2026-09-01）—— 1 行しか打っていないのに 3 行ぶん空いているのは
       落ち着かない。★本文窓はそのぶん伸び縮みする。 */
    BOX_ROWS  = 3,                     /* 上限 */
    /* 本文窓との隙間。★**欄の側が持つ**（本文窓ではなく）—— 欄の高さが動くと
       隙間も一緒に動くので、本文窓の側から塗ると**どちらも塗らない 8px** が残る。
       実際、送って欄が 2 行から 1 行へ縮んだとき、古い欄の字の下 8px が
       帯になって残っていた（実機の指摘・2026-09-01）。
       ★直し方は「毎回引き直す」—— 欄と隙間を**ひとまとめに送る**。
         変わり目だけ塗る形にすると、次に隙間を汚す道ができたときにまた残る。 */
    BOX_GAP   = 8,
    BOX_H     = BOX_GAP + BOX_ROWS * BASE,  /* 器の最大（隙間 ＋ box_rows 行） */
    BOX_X0    = MARGIN + 24,           /* ＞ のぶん右へ。★2 行目以降も揃える */
    /* 右端。★印の箱（左枠が W-MARGIN-27）に**3px の隙間を空けて**止める ——
       Zenmai の 1 行版は `W-MARGIN-24` まで描いていて箱と重なりうるが、
       あちらは入力側でも幅を絞っていたので表に出なかった。 */
    BOX_X1    = W - MARGIN - 30,
    BOX_LINES = 16,                    /* 折返しの上限（96 字なら 5 行で足りる） */
};
static uint16_t box[BOX_H][W];
static int box_rows = 1;               /* いま見せている行数（1..BOX_ROWS） */

/* ★注目文節の左上（**画面座標**）。候補窓をそこへ開くために draw_input が控える。
   **図を描く側と位置を決める側で同じ割り付けを使う**ため、別に計算し直さない。
   ★欄の高さが動くので、行番号ではなく y そのものを持つ。 */
static int focus_x = MARGIN, focus_y;

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

/* キャレットを 1 行ぶん上下へ。1 = 動いた / 0 = その方向に行がない。
   ★欄が複数行になったので要る（実機の指摘・2026-09-01）—— 1 字ずつ ←→ で
     戻るのは行が長いとつらい。
   ★**同じ割り付け（box_layout）を使う**。別に数えると、見えている行と
     動く先がずれる。 */
static int caret_line_move(int d)
{
    int start[BOX_LINES];
    const int nl = box_layout(comp, clen, start);
    if (nl <= 1)
        return 0;
    int cl = 0;
    for (int i = 0; i < nl; i++)
        if (start[i] <= caret)
            cl = i;
    const int nc = cl + d;
    if (nc < 0 || nc >= nl)
        return 0;
    /* いまの行内での横位置を測り、行った先で一番近いところへ落とす */
    int x = 0;
    for (int i = start[cl]; i < caret; i++)
        x += glyph_w(comp[i]);
    const int i0 = start[nc];
    const int i1 = (nc + 1 < nl) ? start[nc + 1] : clen;
    int cx = 0, pos = i0;
    while (pos < i1 && cx + glyph_w(comp[pos]) / 2 < x) {
        cx += glyph_w(comp[pos]);
        pos++;
    }
    caret_set(pos);
    return 1;
}

/* 打っている字と行インジケータ（ind = 0 なら箱だけ）を描いて送る。
 *   car < 0 … キャレットを描かない（変換中）
 *   ua < 0  … 下線を引かない。そうでなければ **[ua, ub) が未確定**として薄い下線を
 *             引き、[fa, fb) の注目文節だけ太くする（★IME の作法）。
 *             ★確定済みには引かない —— もう変換の対象ではない。
 *             ★未確定は**キャレットの手前まで**（その先は確定済みが続きうる）
 */
static void draw_input(const uint16_t *s, int n, int car, uint16_t ind,
                       int ua, int ub, int fa, int fb)
{
    int start[BOX_LINES];
    const int nl = box_layout(s, n, start);

    /* ★打った量で欄を伸ばす（1 行から。上限 BOX_ROWS）。高さが変わったら
       本文窓を作り直す —— 欄の下端は動かないので、上へ伸びる。 */
    const int rows = (nl < BOX_ROWS) ? (nl > 0 ? nl : 1) : BOX_ROWS;
    const int boxh = rows * BASE;
    const int boxy = H - 24 - boxh;    /* 下セーフ 24px は Zenmai と同じ */
    const int bufh = BOX_GAP + boxh;   /* 送る高さ（隙間ぶん上へ出す） */
    if (rows != box_rows) {
        box_rows = rows;
        body_h = boxy - BOX_GAP - body_top;
        view_bottom();
        render_window();
    }

    int cl = 0;                        /* キャレット（変換中は focus）のある行 */
    for (int i = 0; i < nl; i++)
        if (start[i] <= (car < 0 ? fa : car))
            cl = i;

    /* 見える窓をキャレットに追従させる（★キャレットは常に見えている） */
    int top = cl - (rows - 1);
    if (top > nl - rows)
        top = nl - rows;
    if (top < 0)
        top = 0;

    fill_rows(box, 0, bufh, BG);
    /* ★「＞」は窓の先頭行に置く（行が流れても目印が動かない）。
       ★上に隠れている行があるときは**薄く**する —— 遡れることが分かる最小の印。 */
    draw24(box, bufh, MARGIN, BOX_GAP, 0xFF1E, top ? DIM : ACCENT);

    for (int r = 0; r < rows && top + r < nl; r++) {
        const int i0 = start[top + r];
        const int i1 = (top + r + 1 < nl) ? start[top + r + 1] : n;
        const int y = BOX_GAP + r * BASE;
        int x = BOX_X0, cx = BOX_X0;
        for (int i = i0; i < i1; i++) {
            if (i == car)
                cx = x;
            if (fa >= 0 && i == fa) {
                focus_x = x;
                focus_y = boxy + r * BASE;
            }
            if (x + glyph_w(s[i]) > BOX_X1)
                break;                 /* ★draw24 は横の範囲を見ない */
            draw24(box, bufh, x, y, s[i], INK);
            if (ua >= 0 && i >= ua && i < ub) {   /* 未確定の下線 */
                const int on = (i >= fa && i < fb);
                const uint16_t col = on ? ACCENT : DIM;
                for (int t = 0; t < (on ? 2 : 1); t++)
                    for (int c = 0; c < glyph_w(s[i]); c++)
                        box[y + BASE - 1 - t][x + c] = col;
            }
            x += glyph_w(s[i]);
        }
        if (car < 0 || top + r != cl)
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

    /* 行インジケータの箱。
       ★**右下の角**に置く（実機の指摘・2026-08-31）—— Zenmai は欄が 1 行しか
         無かったので右端＝右下だったが、3 行になると 1 行目は「中途半端に上」。 */
    int iw = ind ? glyph_w(ind) : 0;
    if (iw < 24)
        iw = 24;
    const int kx = W - MARGIN - iw;
    const int iy = bufh - BASE;
    for (int c = kx - 3; c <= kx + iw + 2; c++)
        box[iy][c] = box[iy + BASE - 1][c] = DIM;
    for (int r = 0; r < BASE; r++)
        box[iy + r][kx - 3] = box[iy + r][kx + iw + 2] = DIM;
    if (ind)
        draw24(box, bufh, kx, iy, ind, ACCENT);

    /* ★隙間ごと送る（BOX_GAP の注記）。欄が伸びても縮んでも、境目に古い画素が残らない。 */
    gp0_upload(0, boxy - BOX_GAP, W, bufh, box[0]);
}

/* ---- 候補窓 ----
 *
 * ★変換中だけ、注目文節の左上に開く（IME の作法 —— 変換している場所の近くに出す）。
 * ★**番号は振らない。** ゲームパッドには数字キーが無いので番号では選べず、
 *   選んでいる 1 つを反転させれば「何番目か」も同時に伝わる。
 * ★5 行の窓で、選んでいる候補を追って流れる（入力欄と同じ作法）。
 * ★地色は本文より 3 段明るい藍。1 段では実機でまず見分けが付かない
 *   （RGB555 の 1 段は 8/255 しかない。Zenmai が状態行で実測した値をそのまま使う）。
 */
#define PANEL 0x10A5
enum { CAND_ROWS = CONV_PAGE, CAND_PAD = 4,
       /* ★追加候補と本来の候補のあいだの隙間。線を字にくっつけると苦しく、
          選択の帯（ACCENT）と同じ色なので**帯に飲まれて線が消える**
          （実機の指摘・2026-09-01）。上下に 5px ずつ空けて真ん中に引く。 */
       CAND_SEP = 11,
       CAND_H = (CAND_ROWS + 2) * BASE + CAND_SEP + CAND_PAD * 2 + 14 };
static uint16_t candbox[CAND_H][W];

/* stride W のバッファの一部を転送する。
   ★`gp0_upload` は **w×h の連続バッファ**を前提にしている（PS1 では VRAM への
     転送コマンドそのもの）ので、幅の一部だけ送りたいときは**行ごとに呼ぶ**。
   ★w は偶数にすること（PS1 側は 32bit 単位で流す）。 */
static void upload_rect(int x, int y, int w, int h, uint16_t (*buf)[W])
{
    for (int j = 0; j < h; j++)
        gp0_upload(x, y + j, w, 1, &buf[j][x]);
}

static void fill_rect(uint16_t (*buf)[W], int x, int y, int w, int h, uint16_t col)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            buf[y + j][x + i] = col;
}

static void draw_cands(void)
{
    const int n = conv_cand_count();
    if (n <= 0)
        return;
    const int sel = conv_cand_index();
    uint16_t c[CONV_STR_MAX];

    int w = 0;                          /* 一番長い候補に合わせる */
    for (int i = 0; i < n; i++) {
        const int cn = conv_cand(i, c, CONV_STR_MAX);
        int cw = 0;
        for (int t = 0; t < cn; t++)
            cw += glyph_w(c[t]);
        if (cw > w)
            w = cw;
    }
    int boxw = w + CAND_PAD * 6;
    if (boxw > W - MARGIN * 2)
        boxw = W - MARGIN * 2;
    boxw = (boxw + 1) & ~1;             /* ★偶数（plat.h の契約） */

    /* ★**ページ送り**にする（実機の指摘・2026-08-31）。1 件ずつ流れると、
       どれを見ているのか分からなくなる —— 候補は「並び」なので、位置が
       動かないほうが読める。**5 件で 1 ページ**。
       ★★追加候補（ひらがな・カタカナ）は**ページングの外**（実機の指摘・
         2026-09-01）—— あれは「↑ で呼び出した別枠」なので常に上に積み、
         窓がそのぶん伸びる（5 → 6 → 7 行）。混ぜるとページが動いて位置を見失う。 */
    const int na = conv_addl_count();
    const int nc = n - na;                  /* 本来の候補 */
    const int in_addl = (sel < na);
    const int ci = sel - na;                /* 本来の候補での位置（負 = 追加候補） */
    const int pages = (nc + CAND_ROWS - 1) / CAND_ROWS;
    const int page = in_addl ? 0 : ci / CAND_ROWS;
    const int top = na + page * CAND_ROWS;  /* 表示リスト上の開始位置 */
    const int crows = (nc - page * CAND_ROWS < CAND_ROWS)
                    ? nc - page * CAND_ROWS : CAND_ROWS;
    const int rows = na + crows;            /* 窓の行数 = 追加候補 + 1 ページ */
    const int sep = na ? CAND_SEP : 0;      /* 追加候補があるときだけ隙間 */
    /* ★ページ表示のぶんだけ下に足を出す（重ねると最後の候補と被る）。
       1 ページしか無いときは足も出さない。 */
    const int foot = (pages > 1) ? 14 : 0;
    const int boxh = rows * BASE + sep + CAND_PAD * 2 + foot;

    int x = focus_x - CAND_PAD;         /* 注目文節の左端に合わせる */
    if (x + boxw > W - MARGIN)
        x = W - MARGIN - boxw;
    if (x < MARGIN)
        x = MARGIN;
    x &= ~1;
    int y = focus_y - boxh - 4;
    /* ★**本文窓の中に収める。** はみ出した数 px は render_window() が描き戻せず、
       文節を移ったときに前の窓の枠がそこだけ残る（実測で 4px 残った）。 */
    if (y + boxh > body_top + body_h)
        y = body_top + body_h - boxh;
    if (y < body_top)
        y = body_top;

    fill_rect(candbox, x, 0, boxw, boxh, PANEL);
    for (int i = 0; i < boxw; i++)      /* 枠 */
        candbox[0][x + i] = candbox[boxh - 1][x + i] = ACCENT;
    for (int j = 0; j < boxh; j++)
        candbox[j][x] = candbox[j][x + boxw - 1] = ACCENT;

    for (int r = 0; r < rows; r++) {
        /* 上 na 行は追加候補、その下が本来の候補の 1 ページ */
        const int i = (r < na) ? r : top + (r - na);
        const int cn = conv_cand(i, c, CONV_STR_MAX);
        const int ry = CAND_PAD + r * BASE + (r < na ? 0 : sep);
        if (i == sel)                   /* 選んでいる 1 つを反転する */
            fill_rect(candbox, x + 1, ry, boxw - 2, BASE, ACCENT);
        int cx = x + CAND_PAD * 2;
        for (int t = 0; t < cn; t++) {
            if (cx + glyph_w(c[t]) > x + boxw - CAND_PAD)
                break;
            draw24(candbox, boxh, cx, ry, c[t], i == sel ? BG : INK);
            cx += glyph_w(c[t]);
        }
    }
    /* ★追加候補と本来の候補の**あいだに線**を引く（実機の指摘・2026-09-01）——
       別枠であることが目で分かるように。 */
    if (na > 0 && na < rows) {
        const int ly = CAND_PAD + na * BASE + sep / 2;
        for (int i = 4; i < boxw - 4; i++)
            candbox[ly][x + i] = ACCENT;
    }

    /* ★ページの位置は**点列**で示す（hechima の `pageDots` と同じ語彙）——
       ★**数えずに掴める**のが要点。12px の数字は実機で「小さすぎて読めない」と
       言われた（2026-08-31）。
       ★上流は 8 ページを超えたら数字へ倒すが、こちらは候補が 9 件までなので
       ページは最大 2 —— 点列だけで足りる。
       ★1 ページしか無いときは出さない（意味の無い印は雑音）。 */
    if (pages > 1) {
        enum { DOT = 5, GAP = 4 };
        const int dw = pages * DOT + (pages - 1) * GAP;
        int dx = x + (boxw - dw) / 2;
        const int dy = boxh - DOT - 5;
        for (int i = 0; i < pages; i++) {
            const uint16_t col = (i == page) ? ACCENT : DIM;
            for (int j = 0; j < DOT; j++)
                for (int k = 0; k < DOT; k++) {
                    /* 角を落として丸に見せる */
                    if ((j == 0 || j == DOT - 1) && (k == 0 || k == DOT - 1))
                        continue;
                    candbox[dy + j][dx + k] = col;
                }
            dx += DOT + GAP;
        }
    }

    upload_rect(x, y, boxw, boxh, candbox);
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

/* 変換を確定して、打っている字（comp[]）へ移す。
 * ★**確定と送信を分ける**（実機の指摘・2026-08-31）—— IME の作法では 1 回目の
 *   確定で未確定が消え、送るのはもう一度。変換したまま送ると打ち直す隙が無い。
 * ★変換中に打ち始めたときも、まずこれを通す（その時点の選択で確定して続きを打つ）。
 * ★gm（かなの状態機械）はここでは触らない —— 打鍵の途中に呼ばれるので、
 *   eager の差し替え位置を壊してしまう。
 */
static void conv_accept(void)
{
    uint16_t out[COMP_MAX * 2];
    const int n = conv_commit(out, (int)(sizeof out / sizeof *out));
    /* ★変換していたのは **comp[fixed..caret)**。そこだけを確定文字列に差し替える。
       ★以前は「fixed から末尾まで」を捨てていたので、**カーソルより後ろの字が
         消えていた**（文字列の途中に打って変換したとき・実機の指摘 2026-09-01）。 */
    comp_insert(out, n, caret - fixed);
    fixed = caret;                     /* 挿入後の caret が新しい確定境界 */
}

/* ---- 左スティックの向き ----
 * ★**大きい方の軸だけを見る**（engine.ts の dominant と同じ）。斜めに倒したときに
 *   2 方向が同時に発火すると、変換中は「候補送り + 文節移動」が一度に起きる。
 * 閾値 0x40 は右スティックのフリック（render.c の pad_rstick_flick）に合わせた。 */
enum { LS_DELAY = 24, LS_INTERVAL = 5,     /* 400ms / 85ms（1 field ≒ 17ms） */
       PUNCT_WINDOW = 35 };                /* 600ms（GamepadEngine と同じ） */

static void ls_dirs(const uint8_t axes[4], int out[4])
{
    const int dx = (int)axes[2] - 0x80, dy = (int)axes[3] - 0x80;
    const int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    out[0] = out[1] = out[2] = out[3] = 0;
    if (ax < 0x40 && ay < 0x40)
        return;
    if (ax >= ay)
        out[dx < 0 ? 0 : 1] = 1;           /* ← → */
    else
        out[dy < 0 ? 2 : 3] = 1;           /* ↑ ↓ */
}

/* ---- メニュー ----
 *
 * ★Start で開く（実機の指摘・2026-08-31）。はじめは「長押しで終わる」形にしたが、
 *   **実機では反応しなかった**（台本では 60 field で抜ける。原因は追えていない）。
 *   ★どのみち**目に見える反応があるほうが確実**で、Zenmai も同じ形にしてある。
 * ★いまは「つづける / やめる」だけ。**打ち方の絵とライセンスはここに足す。**
 * ★地色は濃い藍（Zenmai の OPT_BG）—— 本文の外側だと地色だけで分かるので枠線は要らない、
 *   という実機の判断があったが、こちらは板が小さいので枠も引く。
 */
#define OPT_BG   0x1C62
#define OPT_EDGE 0x36B9

static const char *const MENU[] = { "つづける", "やめる" };
/* ★高さは「上下の余白が同じ」になるように決める。
   帯は各行の 4px 上から 32px なので、上の余白 = 12-4 = 8。
   下も 8 にするには MENU_H = N*32 + 16（+24 にしていたら下だけ 16 になって、
   実機で「やめるの下だけ広い」と言われた・2026-08-31）。 */
enum { MENU_N = 2, MENU_W = 320, MENU_ROW = 32, MENU_H = MENU_N * MENU_ROW + 16 };
static uint16_t menubox[MENU_H][W];

static void menu_draw(int sel)
{
    const int x = ((W - MENU_W) / 2) & ~1;
    fill_rect(menubox, x, 0, MENU_W, MENU_H, OPT_BG);
    for (int i = 0; i < MENU_W; i++)
        menubox[0][x + i] = menubox[MENU_H - 1][x + i] = OPT_EDGE;
    for (int j = 0; j < MENU_H; j++)
        menubox[j][x] = menubox[j][x + MENU_W - 1] = OPT_EDGE;

    for (int i = 0; i < MENU_N; i++) {
        uint16_t t[32];
        const int n = u8_to_u16(MENU[i], -1, t, 32);
        int w = 0;
        for (int k = 0; k < n; k++)
            w += glyph_w(t[k]);
        const int y = 12 + i * MENU_ROW;
        /* ★選んでいる行は**候補窓と同じ反転**にする（帯 = ACCENT・字 = 地色）。
           PANEL（本文より 3 段明るい灰）を敷いたら、藍の板の上では**暗く見えて
           沈んだ** —— 同じ色でも、下に敷く色で意味が変わる。 */
        if (i == sel)
            fill_rect(menubox, x + 8, y - 4, MENU_W - 16, BASE + 8, ACCENT);
        int cx = x + (MENU_W - w) / 2;
        for (int k = 0; k < n; k++) {
            draw24(menubox, MENU_H, cx, y, t[k], i == sel ? OPT_BG : INK);
            cx += glyph_w(t[k]);
        }
    }
    /* ★本文窓の中に収める（外へ出た画素は render_window() で消せない） */
    upload_rect(x, body_top + (body_h - MENU_H) / 2, MENU_W, MENU_H, menubox);
}

/* 1 = やめる / 0 = つづける */
static int menu_open(void)
{
    int sel = 0;
    int prev = pad_read();
    if (prev < 0)
        prev = 0;
    int lsp[4] = { 0, 0, 0, 0 };
    render_window();                   /* 候補窓が開いていたら消す */
    menu_draw(sel);

    for (;;) {
        wait_fields(1);
        uint8_t axes[4];
        const int p = pad_read_ex(axes);
        if (p < 0)
            continue;
        const int edge = p & ~prev;
        prev = p;

        int moved = 0;
        int ls[4];
        ls_dirs(axes, ls);
        if ((edge & BTN_UP) || (ls[2] && !lsp[2])) {
            sel = (sel + MENU_N - 1) % MENU_N;
            moved = 1;
        }
        if ((edge & BTN_DOWN) || (ls[3] && !lsp[3])) {
            sel = (sel + 1) % MENU_N;
            moved = 1;
        }
        for (int i = 0; i < 4; i++)
            lsp[i] = ls[i];
        if (moved)
            menu_draw(sel);

        /* ★どのフェイスボタンでも決まる（Zenmai と同じ作法。画面には書かない） */
        if (edge & (BTN_CIR | BTN_X | BTN_TRI | BTN_SQ | BTN_L3))
            return sel;
        if (edge & BTN_START)          /* Start で開いて Start で閉じる */
            return 0;
    }
}

/* ---- 変換エンジンの置き場 ----
 * ★探し方は glyph_ft.c（フォント）と同じ考え方: 環境変数 → 実行ファイルの隣。 */
static const char *near_exe(const char *rel, char *buf, int max)
{
    const ssize_t n = readlink("/proc/self/exe", buf, (size_t)max - 64);
    if (n <= 0)
        return 0;
    buf[n] = 0;
    char *slash = strrchr(buf, '/');
    if (!slash)
        return 0;
    strcpy(slash + 1, rel);
    return buf;
}

static const char *data_path(void)
{
    static char buf[1024];
    const char *env = getenv("HIGGINS_DATA");
    if (env && *env)
        return env;
    const char *p = near_exe("mozc.data", buf, (int)sizeof buf);       /* 配布 */
    if (p && access(p, R_OK) == 0)
        return p;
    p = near_exe("vendor/hechima/mozc.data", buf, (int)sizeof buf);    /* 開発機 */
    if (p && access(p, R_OK) == 0)
        return p;
    return "mozc.data";
}

/* 学習（segment.db / boundary.db / cform.db）とユーザー辞書の置き場。
   ★PortMaster では起動スクリプトが HIGGINS_PROFILE を指す —— **置き場所を決めるのは
     起動スクリプトの仕事**で、こちらは環境変数を見るだけ（Zenmai のセーブと同じ形）。 */
static const char *profile_dir(void)
{
    static char buf[1024];
    const char *env = getenv("HIGGINS_PROFILE");
    const char *p = (env && *env) ? env : near_exe("conf", buf, (int)sizeof buf);
    if (p)
        mkdir(p, 0777);                    /* 無ければ作る */
    return p ? p : "";
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
 *   L3 押し込み = 確定して ELIZA へ / R3 押し込み = 未確定を捨てる（確定済みは残る）
 *   Start = やめる（★段階 E でオプション画面に置き換える）
 *
 *   ★段階 D（mozc）で足りる余地を空けてある: 打っている最中の**左スティック縦**
 *     （↓ 変換開始 / ↑ 前候補）と、**右スティック↓**。
 */
static void interactive(void)
{
    int fields = 0, dirty = 1;
    int ls_prev[4] = { 0, 0, 0, 0 };   /* ← → ↑ ↓ の前フレーム */
    int ls_at[4] = { 0, 0, 0, 0 };     /* 次にリピートする時刻（fields） */
    int prev_conv = 0;                 /* 前の回に変換中だったか（候補窓を消すため） */
    int punct_n = 0, punct_at = 0;     /* 右スティック↓ の連打（句読点トグル） */
    uint16_t prev_rowchar = 0;

    gp_init(&gm);
    clen = 0;
    caret = 0;
    fixed = 0;
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
            /* ★変換中に打ち始めたら、その時点の選択で確定してから入れる
               （実機の指摘・2026-08-31）。IME の作法。 */
            if (conv_active())
                conv_accept();
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
        if (rs == 1) {                 /* ← BS */
            if (conv_active()) {
                /* ★変換中は**未確定のかなに戻す**（普通の IME と同じ・実機の指摘）。
                   1 字消すのではなく、変換そのものをやめる。 */
                conv_cancel();
                dirty = 1;
            } else if (caret) {
                comp_bs();
                gm.eagerSet = 0;
                dirty = 1;
            }
        }
        if (rs == 3) {                 /* → 長音「ー」 */
            if (conv_active())
                conv_accept();
            const uint16_t ch = 0x30FC;
            comp_insert(&ch, 1, 0);
            dirty = 1;
        }
        if (rs == 4) {
            /* ★↓ = 句読点。1 回「、」/ 2 回「。」/ 3 回 全角空白。
               窓の中に次が来たら**前の字を差し替える**（GamepadEngine と同じ。
               あちらの PUNCTUATION_DOUBLE_TAP_MS は 600ms）。 */
            if (conv_active())
                conv_accept();
            const int within = punct_at && fields < punct_at;
            punct_n = within ? punct_n + 1 : 1;
            punct_at = fields + PUNCT_WINDOW;
            uint16_t ch = 0x3001;      /* 、 */
            int rep = 0;
            if (punct_n == 2) {
                ch = 0x3002;           /* 。 */
                rep = 1;
            } else if (punct_n >= 3) {
                ch = 0x3000;           /* 全角空白 */
                rep = 1;
                punct_n = 0;
                punct_at = 0;
            }
            comp_insert(&ch, 1, rep);
            dirty = 1;
        }

        if (edge & BTN_R3) {
            /* ★2 段にする（IME の作法）: 変換中はまず変換だけ取り消し、
               もう一度で**未確定のかな**を捨てる。
               ★**確定済みは消さない**（実機の指摘・2026-09-01）—— 前は行ごと
                 捨てていたが、キャンセルは未確定に対する操作で、確定した字まで
                 消えるのは hechima（Escape）とも違う。消したいときは BS。 */
            if (conv_active()) {
                conv_cancel();
                dirty = 1;
            } else if (caret > fixed) {
                comp_drop_pending();
                gm.eagerSet = 0;
                dirty = 1;
            }
        }

        if (edge & BTN_START) {        /* ★Start = メニュー */
            if (menu_open())
                break;                 /* やめる */
            /* 戻ったら本文を描き直し、押されたままのものを「もう見た」ことにする
               （★忘れると、選んだボタンに応じた字が入った状態で再開する）。 */
            render_window();
            carry_over_pad();
            prev_conv = 0;
            dirty = 1;
            continue;
        }

        if ((edge & BTN_L3) && conv_active()) {
            /* ★1 回目の押し込みは**変換の確定**だけ（実機の指摘・2026-08-31）。
               ELIZA へ渡すのはもう一度押したとき。 */
            conv_accept();
            gm.eagerSet = 0;
            dirty = 1;
        } else if ((edge & BTN_L3) && caret > fixed) {
            /* ★変換していない**未確定のかな**も、まず確定するだけ（下線が外れる）。
               送るのはもう一度押したとき —— 変換したときと同じ手数にする
               （実機の指摘・2026-09-01。打ってすぐ送られるのは直感的でない）。 */
            fixed = caret;
            gm.eagerSet = 0;
            dirty = 1;
        } else if ((edge & BTN_L3) && clen) {               /* L3 = 確定して ELIZA へ */
            /* 遡っている最中の確定は、まず下端へ跳んでひと呼吸置く */
            if (view_px < hist_total() - body_h) {
                view_bottom();
                render_window();
                wait_fields(18);
            }
            static uint16_t typed[COMP_MAX * 2];
            const int typed_n = clen;
            for (int i = 0; i < clen; i++)
                typed[i] = comp[i];
            clen = 0;
            caret = 0;
            fixed = 0;
            gm.eagerSet = 0;
            dirty = 1;
            send(typed, typed_n);
        }

        /* ---- 左スティック ----
         * ★作法は GamepadEngine（labo の web/src/gamepad/engine.ts）に揃える:
         *     ↓ 変換開始 / 次候補    ↑ 前候補    ←→ 文節移動
         *     ★**RT + ←→ = 文節伸縮**（RT は「使用済み」にして「ん」を抑止する）
         *   変換していないとき: ←→ キャレット移動 / ↑↓ 履歴を遡る（打っていないとき）
         * ★リピートは**変換中はしない**（候補送りの暴走を防ぐ）。engine.ts と同じ判断。
         *   間隔も同じ（400ms 待って 85ms ごと ＝ 1 field 17ms なので 24 / 5）。
         */
        {
            int ls[4];
            ls_dirs(axes, ls);
            const int rt = (p & BTN_R2) != 0;
            for (int d = 0; d < 4; d++) {
                int fire = 0;
                if (!ls[d]) {
                    ls_at[d] = 0;
                } else if (!ls_prev[d]) {
                    ls_at[d] = fields + LS_DELAY;
                    fire = 1;                          /* エッジ */
                } else if (!conv_active() && ls_at[d] && fields >= ls_at[d]) {
                    ls_at[d] = fields + LS_INTERVAL;
                    fire = 1;                          /* リピート */
                }
                ls_prev[d] = ls[d];
                if (!fire)
                    continue;
                switch (d) {
                case 0:                                /* ← */
                    if (conv_active()) {
                        if (rt) { conv_resize(-1); gp_consume_rt(&gm); }
                        else conv_move(-1);
                        dirty = 1;
                    } else if (caret > 0) {
                        caret_set(caret - 1);
                        gm.eagerSet = 0;
                        dirty = 1;
                    }
                    break;
                case 1:                                /* → */
                    if (conv_active()) {
                        if (rt) { conv_resize(+1); gp_consume_rt(&gm); }
                        else conv_move(+1);
                        dirty = 1;
                    } else if (caret < clen) {
                        caret_set(caret + 1);
                        gm.eagerSet = 0;
                        dirty = 1;
                    }
                    break;
                case 2:                                /* ↑ */
                    if (conv_active()) {
                        conv_prev();
                        dirty = 1;
                    } else if (caret_line_move(-1)) {
                        gm.eagerSet = 0;   /* ★行を移ったら eager の差し替えは切る */
                        dirty = 1;
                    } else if (!clen) {
                        view_scroll(-SCROLL_STEP);
                    }
                    break;
                case 3:                                /* ↓ */
                    if (conv_active()) {
                        conv_next();
                        dirty = 1;
                    } else if (caret > fixed) {
                        /* ★**未確定の部分だけ**を変換する ＝ `comp[fixed..caret)`。
                           ★末尾までではない（実機の指摘・2026-09-01）—— 文字列の
                             途中に打ったとき、それ以降の確定済みまで巻き込む。
                           ★条件も「未確定があるか」であって「最終行にいるか」では
                             ない —— 行で決めると 1 行目で打ち始めたときに変換できない。 */
                        if (conv_start(comp + fixed, caret - fixed))
                            dirty = 1;
                    } else if (caret_line_move(1)) {
                        /* 全部確定済みなら ↓ は行移動（↑ の対） */
                        gm.eagerSet = 0;
                        dirty = 1;
                    } else {
                        view_scroll(SCROLL_STEP);
                    }
                    break;
                }
            }
        }

        const uint16_t rowchar = gp_row_char(row);
        if (dirty || rowchar != prev_rowchar) {
            if (conv_active()) {
                /* ★表示は「確定済み ＋ 変換中の文節 ＋ **キャレットより後ろの
                   確定済み**」。3 つ目を忘れると、文字列の途中で変換したときに
                   後ろが消えて見える（実機の指摘・2026-09-01）。
                   下線と候補窓の位置もそのぶんずらす。 */
                uint16_t t[COMP_MAX * 3];
                const int tmax = (int)(sizeof t / sizeof *t);
                int tn = 0;
                for (int i = 0; i < fixed && tn < tmax; i++)
                    t[tn++] = comp[i];
                int fa = 0, fb = 0;
                const int ua = tn;
                const int cn = conv_text(t + tn, tmax - tn, &fa, &fb);
                fa += tn;
                fb += tn;
                tn += cn;
                const int ub = tn;
                for (int i = caret; i < clen && tn < tmax; i++)
                    t[tn++] = comp[i];
                /* ★毎回まず本文を描き戻す —— 候補窓は本文の上に開くので、
                   文節を移ると**前の窓が残る**（実測）。位置も大きさも変わるので、
                   前の矩形を覚えて消すより、下を描き直す方が確実で安い。 */
                render_window();
                draw_input(t, tn, -1, rowchar, ua, ub, fa, fb);
                draw_cands();          /* ★入力欄を描いたあと（focus_x を使う） */
            } else {
                /* ★変換をやめた回は本文を描き直す —— 候補窓が本文の上に
                   開いているので、消すには下を描き戻すしかない。 */
                if (prev_conv)
                    render_window();
                /* ★**未確定のかな（comp[fixed..clen)）にも下線を引く。**
                   引かないと「どこまでが確定済みか」が、変換して初めて分かる
                   （実機の指摘・2026-08-31）。注目文節はまだ無いので fa/fb は -1。 */
                draw_input(comp, clen, caret, rowchar, fixed, caret, -1, -1);
            }
            prev_conv = conv_active();
            prev_rowchar = rowchar;
            dirty = 0;
        }
    }
}

int main(void)
{
    gpu_init();
    render_init();
    /* ★入力欄のぶん本文窓を縮める（render_init の既定を上書き）。
       ★欄の高さは打った量で動くので、ここは**1 行ぶんの初期値**でしかない ——
         以後は draw_input が行数の変わり目で引き直す。 */
    body_top = TOP;
    body_h = (H - 24 - BASE) - BOX_GAP - body_top;   /* 欄 1 行ぶんから始める */
    paint_screen(BG);

    /* ★変換エンジン。無くても Higgins は動く（かなのまま ELIZA へ渡る）——
       辞書 19MB を積めない機種でも起動はする、という形にしておく。 */
    conv_init(data_path(), profile_dir());

    const int len = (int)(_binary_script_txt_end - _binary_script_txt_start);
    /* ★0 = 組み込みのエラーメッセージを使わない —— あれは**英語**なので、
       日本語スクリプトのまま出ると相手が急に英語で返してくる（実測: 「パパ」「さびしい」で
       `HMMM`）。スクリプト側の `NONE`（「よく分からないのですが」ほか 4 つ）に落とす。 */
    if (!hg_eliza_init(_binary_script_txt_start, len, 0))
        say("スクリプトが読めない。", INK);
    else
        say(hg_eliza_greeting(), INK);

    view_bottom();
    render_window();
    draw_input(0, 0, 0, 0, -1, -1, -1, -1);
    gpu_display_on();

    pad_try_analog();
    interactive();
    return 0;
}
