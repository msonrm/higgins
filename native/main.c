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
#include "josa.h"
#include "korean.h"
#include "devanagari.h"
#include "hindi.h"
#include "tsunagi.h"
#include "tsunagi_ko.h"
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
/* ★英語の相手 = 1966 年 CACM の原典（vendor の付録を無加工で焼いたもの）。 */
extern const char _binary_script_ko_txt_start[];
extern const char _binary_script_ko_txt_end[];
extern const char _binary_script_en_txt_start[];
extern const char _binary_script_en_txt_end[];
/* ★ヒンディー語の相手（doctor/hi.txt・2026-09-03）。 */
extern const char _binary_script_hi_txt_start[];
extern const char _binary_script_hi_txt_end[];

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

/* いま話している相手（席）。★もとは `english` の真偽値だったが、韓国語で 3 つ目が
   来たので enum にした。**日本語だけが変換を通る**ので、判定は seat != SEAT_JA。
   ★★**ヒンディー語（SEAT_HI）だけは入力の骨格が違う** —— かなの席（日/英/韓）は
     「子音行 × 母音」の同時押しだが、デーヴァナーガリーは「LS の latch × D-pad」で、
     両スティックとも他の役に取られる。だから物理入力の解釈から**別の道**になる
     （`GpFrameDeva` / `gp_step_deva`。input.h の注記）。 */
enum { SEAT_JA = 0, SEAT_EN = 1, SEAT_KO = 2, SEAT_HI = 3, SEAT_N = 4 };
static int seat;

/* 右スティック↓ の連打（句読点の巡回）。★**file scope にあるのは、
   「他の入力が来たら巡回を切る」を comp_insert / comp_bs / caret_set の中で
   やるため** —— 呼ぶ場所を数え上げる形にすると、いつか 1 つ忘れる。 */
static int punct_n, punct_at;

static void punct_break(void) { punct_n = 0; punct_at = 0; }

static int seat_slot(void)
{
    return seat == SEAT_EN ? HG_EN : seat == SEAT_KO ? HG_KO
         : seat == SEAT_HI ? HG_HI : HG_JA;
}

/* ★かなの席の言語。**SEAT_HI では呼ばない** —— デーヴァナーガリーは
   `gp_step_kana` を通らないので、GP_* の語彙に対応するものが無い。 */
static int seat_lang(void)
{
    return seat == SEAT_EN ? GP_EN : seat == SEAT_KO ? GP_KO : GP_JA;
}

/* UTF-8 の 1 行を履歴へ積む。★折返し・禁則は render.c が持っている。 */
static void say(const char *utf8, uint16_t color)
{
    uint16_t buf[1024];
    const int max = (int)(sizeof buf / sizeof *buf);
    /* ★出口は席で替わる。韓国語は**空白に意味がある**（띄어쓰기）ので、
       日本語の tsunagi（日本語に隣接する空白を全部消す）は使えない。
       ★★**ヒンディー語は出口が要らない** —— tsunagi が空白を消すのは
         「日本語に隣接する空白」だけで（tsunagi.c の is_ja）、デーヴァナーガリーは
         その 5 範囲のどれにも入らないので素通りする。英語と同じ扱いになる。
       ★日本語・韓国語に出口が要ったのは「空白を消す」「조사 を実現する」という
         仕事があったからで、ヒンディー語にはどちらも無い。 */
    const int n = seat == SEAT_KO ? tsunagi_ko(utf8, buf, max)
                                  : tsunagi(utf8, buf, max);
    hist_line(buf, n, color);
}

/* ---- 打っている字（合成バッファ）---- */

static GpMachine gm;
static KoComposer ko;    /* 韓国語の合成中の音節（席が韓国語のときだけ動く） */
/* デーヴァナーガリー（席がヒンディー語のときだけ動く）。★**2 つ要る** ——
   `dvm` は打鍵の状態機械（varga の latch・非 varga 層・RT の使い道）、
   `dv` は合成中の akshara。韓国語で `gm` と `ko` が分かれているのと同じ切り分け。 */
static DevaMachine dvm;
static Deva dv;
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
/* いま話している相手。0 = 日本語 DOCTOR / 1 = 1966 年の原典 DOCTOR。
 * ★**言語ではなく相手**（席）を指す —— 記憶も規則も別なので、切り替えは
 *   「英語で書く」ではなく「別の相手に向き直る」ことになる。
 * ★英語には変換が無いので、打った字は**その場で確定**する（comp_insert の末尾）。
 *   これだけで L3 が 1 回で送信になり、R3 は「捨てるものが無い」になり、
 *   左スティック↓ は行移動になる —— ★英語専用の分岐を主ループに撒かずに済む。
 * ★★**韓国語も同じ道に乗る** —— ハングルは確定形なので変換が要らない。
 *   合成（音節を組み立てる）のは korean.c で、そちらは「打った字」より内側の話。 */
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
    if (seat != SEAT_JA)
        fixed = caret;                 /* ★英語と韓国語は変換が無いので即確定（上の注記）*/
    /* ★★何かを入れたら句読点の巡回は切れる。**窓は押すたびに延びる**ので、
       切らないと「空白 → 字を打つ → もう一度 ↓」で 2 段目が発火し、
       **いま打った字を食う**（`rep = 1`）。実機の指摘（2026-09-02）——
       韓国語は 5 段なので、そのまま連打すると `'` まで走ってしまう。
       ★GIME も labo web も同じ作り（時間の窓だけ）なので、これは**上流へ返す**話。 */
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
    ko_commit(&ko);       /* ★動かしたら合成も切る（前の音節に받침が付くのを防ぐ） */
    deva_commit(&dv);     /* ★同じ理由 —— 動かした先の字に matra が乗ってしまう */
    punct_break();
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
       あちらは入力側でも幅を絞っていたので表に出なかった。
       ★英語では箱が**2 枠**（シフトの印 ＋ 行）になるので、そのぶん狭い（box_x1）。 */
    BOX_X1    = W - MARGIN - 30,
    /* 英語の箱の中身 = その行の面ボタンの字（最大 `@#-_` の 53px）。★行ごとに幅が
       変わると箱が動いて読みにくいので**固定**にして中で中央寄せする。 */
    EN_IND_W  = 62,                    /* 実測: 一番広い行 `@#-_` が 53px。左右に 4px 余る */
    /* シフトの印。★**枠の外**に置く（実機の指摘・2026-09-01）—— 枠の中に空き枠を
       用意しておくのは、印が無いときの見た目が落ち着かない。
       ★横幅は**英語では常に確保する**（出たり消えたりで折返しが動かないように）。 */
    EN_MARK_W = 26,
    BOX_LINES = 16,                    /* 折返しの上限（96 字なら 5 行で足りる） */
};
static uint16_t box[BOX_H][W];
static int box_rows = 1;               /* いま見せている行数（1..BOX_ROWS） */

/* ---- 枠の中で字を縦の中央へ ----
 *
 * ★**器（BASE 行）は一番きつい字に合わせてある** —— デーヴァナーガリーは上下に
 *   母音記号が付くので 30 行（`glyph.h` の頭書き）。そのぶん**余りは下に偏る**:
 *   実測（23px・器 30）で「あ」は 上 2 / 下 7、ハングルは 上 6 / 下 11、
 *   ラテンの小文字は 上 2 / 下 9。ベースラインが器の 21 行目に来るためで、
 *   広げた 6 行はほぼ全部がデーヴァナーガリーの descender のぶん。
 * ★本文では器の境目が見えないので誰も気づかないが、**枠を引く場所**
 *   （右下のインジケータ・メニューの帯）では「中身が上に寄って見える」。
 *   ★器が 24 だったころは器が本文の字にぴったりだったので出なかった —— つまり
 *     これは**器を広げたときに一緒に来た崩れ**（実機の指摘・2026-09-03）。
 *
 * ★★**推定しない。** `glyph.h`（PS1 と共有する 5 本）は ink の寸法を返さないので、
 *   **一度描いて測る**。字の出どころを知らないまま測れるので、フォントを替えても
 *   2 本目の面を足しても、席を足しても、ここは直さなくてよい。
 * ★測るのは**その箱に出しうる字ぜんぶ**（`ind_alphabet`）—— 見えている 1 字だけで
 *   決めると、行を替えるたびに中身が上下に跳ねる。★寸法の表を手で持たないので、
 *   席を足しても「一覧の出どころ」を 1 つ足すだけで済む。
 */
static uint16_t inkbuf[BASE][W];

/* s の n 字を**重ねて**描き、その ink が器の中で上下中央に来る下げ幅を返す。
 * ★重ねるのは、要るのが**縦の広がりだけ**だから（横に並べると W を越える）。
 * ★★**n 字まとめて渡すのが肝。** 箱が持っているのは「その席の一覧のうち、
 *   いま見えている 1 つ」なので、**器は一覧ぜんぶで決める** —— 1 つずつ中央へ
 *   寄せると、行を替えるたびに中身が上下に跳ねる（英語は ±3px 動く。
 *   ascender も descender も無い `abc` と、両方ある `ghi` の差）。
 */
static int ink_dy(const uint16_t *s, int n)
{
    enum { X0 = 8 };
    if (n <= 0)
        return 0;
    memset(inkbuf, 0, sizeof inkbuf);
    /* ★clip を戻す先は **(0, WIN_H)** —— render.c が最後に必ず置いていく値で、
       main.c の描画（欄・候補窓・メニュー）はどれもそれが置かれている前提で動く。 */
    glyph_clip(0, BASE);
    for (int i = 0; i < n; i++)
        draw24(inkbuf, BASE, X0, 0, s[i], 0x7FFF);   /* ★色は最大の白で固定 ——
            実際に使う色（INK / ACCENT / 地色）で測ると、**同じ字なのに置き場所で
            1px ずれる**（暗い色ではアンチエイリアスの端が RGB555 で 0 に丸まる）。
            ink は字の性質であって、どの色で塗るかとは無関係。 */
    glyph_clip(0, WIN_H);

    int top = -1, bot = -1;
    for (int r = 0; r < BASE; r++)
        for (int c = 0; c < W; c++)
            if (inkbuf[r][c]) {
                if (top < 0)
                    top = r;
                bot = r;
                break;
            }
    if (top < 0)
        return 0;                      /* 空白だけ ―― 動かさない */
    return (BASE - (bot - top + 1)) / 2 - top;
}

/* インジケータの箱に**出しうる字を全部**（＝器を決める一覧）。
   ★席ごとに一覧の出どころが違うだけで、考え方は同じ。 */
static int ind_alphabet(uint16_t *out, int max)
{
    int n = 0;
    if (seat == SEAT_HI) {
        /* varga 5 つの頭字 ＋ 非 varga 層の 2 つ（半母音 / LT で摩擦音） */
        DevaMachine m;
        deva_machine_init(&m);
        for (int d = 0; d <= 4 && n < max; d++) {
            m.lsLatch = d;
            out[n++] = deva_head_char(&m, 0);
        }
        m.nonVarga = 1;
        for (int lt = 0; lt <= 1 && n < max; lt++)
            out[n++] = deva_head_char(&m, lt);
        return n;
    }
    for (int r = 0; r < 10 && n < max; r++) {
        if (seat == SEAT_KO) {
            out[n++] = ko_row_char(r);
        } else if (seat == SEAT_EN) {
            uint16_t t[4];
            const int k = gp_row_chars_en(r, t, 4);
            for (int i = 0; i < k && n < max; i++) {
                out[n++] = t[i];
                /* ★シフト中は大文字が出る。同じ箱に入るので一緒に測る */
                if (t[i] >= 'a' && t[i] <= 'z' && n < max)
                    out[n++] = (uint16_t)(t[i] - 0x20);
            }
        } else {
            out[n++] = gp_row_char(r);
        }
    }
    return n;
}

/* 席が変わるまで動かない値なので覚えておく（毎打鍵で 80 字描き直さない）。 */
static int ind_dy(void)
{
    static int dy, cached = -1;
    if (cached != seat) {
        uint16_t a[96];
        dy = ink_dy(a, ind_alphabet(a, (int)(sizeof a / sizeof *a)));
        cached = seat;
    }
    return dy;
}

/* シフトの印（⇧ / △ / ▲）。★3 つは同じ場所で入れ替わるので、まとめて測る。 */
static int mark_dy(void)
{
    static const uint16_t MARKS[] = { 0x21E7, 0x25B3, 0x25B2 };
    static int dy, done;
    if (!done) {
        dy = ink_dy(MARKS, (int)(sizeof MARKS / sizeof *MARKS));
        done = 1;
    }
    return dy;
}

/* 字を描ける右端。★英語は印の枠のぶん狭い。**切り替えは欄が空のときだけ**なので、
   ここが変わっても折返しが目の前で動くことはない。 */
static int box_x1(void)
{
    if (seat != SEAT_EN)
        return BOX_X1;
    /* 枠の左端（W-MARGIN-EN_IND_W-3）の手前に、印の幅と隙間を空ける */
    return W - MARGIN - EN_IND_W - 3 - EN_MARK_W - 6;
}

/* ★注目文節の左上（**画面座標**）。候補窓をそこへ開くために draw_input が控える。
   **図を描く側と位置を決める側で同じ割り付けを使う**ため、別に計算し直さない。
   ★欄の高さが動くので、行番号ではなく y そのものを持つ。 */
static int focus_x = MARGIN, focus_y;

/* ---- 入力欄の整形 ----
 *
 * ★**打っている字も整形を通す。** 本文（render.c）と同じ理由 —— 1 対 1 でない
 *   言語（デーヴァナーガリー）は母音記号が前に回り、子音が縦に積んで合字になるので、
 *   `draw24(code)` をどう並べても正しい絵にならない（glyph.h の頭書き）。
 *
 * ★★**割り付け・キャレット・下線が同じ整形結果を見ること。** 別々に整形すると
 *   見えている行と動く先がずれる —— box_layout の頭書きにある「同じ割り付けを使う」が、
 *   「行」から「グリフ」へ一段下りただけの話。
 */
enum { ISHAPE_N = COMP_MAX * 3 };      /* draw_input が受ける最大（変換中の t[]） */
static Shaped ishape[ISHAPE_N];
static int ishape_n;

static int shape_input(const uint16_t *s, int n)
{
    ishape_n = shape_run(s, n, ishape, ISHAPE_N);
    return ishape_n;
}

/* 論理の位置（何字目か）→ 整形した並びでの位置（何グリフ目か）。
   ★合字と入れ替えがあると自明でないので、cluster を前から見て**最初に届いたもの**を
     採る。1 対 1 の実装では logical をそのまま返すのと同じ。 */
static int glyph_at(int logical)
{
    for (int i = 0; i < ishape_n; i++)
        if ((int)ishape[i].cluster >= logical)
            return i;
    return ishape_n;
}

/* 折返して各行の開始位置を出す。戻り値 = 行数（必ず 1 以上）。
   ★**start[] は整形した並びでの位置**（何グリフ目か）。論理の位置が要るところは
     `ishape[k].cluster` を引くこと。 */
static int box_layout(int m, int *start)
{
    int nl = 0;
    start[nl++] = 0;
    int x = BOX_X0;
    const int x1 = box_x1();
    for (int i = 0; i < m; i++) {
        const int w = ishape[i].adv;
        if (x + w > x1) {
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
    const int m = shape_input(comp, clen);
    int start[BOX_LINES];
    const int nl = box_layout(m, start);
    if (nl <= 1)
        return 0;
    const int cg = glyph_at(caret);    /* ★キャレットは論理・割り付けはグリフ */
    int cl = 0;
    for (int i = 0; i < nl; i++)
        if (start[i] <= cg)
            cl = i;
    const int nc = cl + d;
    if (nc < 0 || nc >= nl)
        return 0;
    /* いまの行内での横位置を測り、行った先で一番近いところへ落とす */
    int x = 0;
    for (int i = start[cl]; i < cg; i++)
        x += ishape[i].adv;
    const int i0 = start[nc];
    const int i1 = (nc + 1 < nl) ? start[nc + 1] : m;
    int cx = 0, pos = i0;
    while (pos < i1 && cx + ishape[pos].adv / 2 < x) {
        cx += ishape[pos].adv;
        pos++;
    }
    /* ★**論理へ戻して置く。** キャレットの持ち主（caret）は「何字目か」なので、
       グリフの番号をそのまま入れると、合字のある行で打つ場所がずれる。 */
    caret_set(pos < m ? (int)ishape[pos].cluster : clen);
    return 1;
}

/* 打っている字と行インジケータ（ind = 0 なら箱だけ）を描いて送る。
 *   car < 0 … キャレットを描かない（変換中）
 *   ua < 0  … 下線を引かない。そうでなければ **[ua, ub) が未確定**として薄い下線を
 *             引き、[fa, fb) の注目文節だけ太くする（★IME の作法）。
 *             ★確定済みには引かない —— もう変換の対象ではない。
 *             ★未確定は**キャレットの手前まで**（その先は確定済みが続きうる）
 */
static void draw_input(const uint16_t *s, int n, int car,
                       const uint16_t *ind, int ilen, uint16_t mark,
                       int ua, int ub, int fa, int fb)
{
    const int m = shape_input(s, n);
    int start[BOX_LINES];
    const int nl = box_layout(m, start);
    /* ★キャレットと注目文節は**論理の位置**で来るので、グリフの位置へ写す
       （1 対 1 の実装ではどちらも同じ数になる）。 */
    const int cg = (car >= 0) ? glyph_at(car) : -1;
    const int fg = (fa >= 0) ? glyph_at(fa) : -1;

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
        if (start[i] <= (car < 0 ? fg : cg))
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
        const int i1 = (top + r + 1 < nl) ? start[top + r + 1] : m;
        const int y = BOX_GAP + r * BASE;
        int x = BOX_X0, cx = BOX_X0;
        for (int i = i0; i < i1; i++) {
            const int lg = (int)ishape[i].cluster;   /* ★この画がどの字から来たか */
            const int aw = ishape[i].adv;
            if (i == cg)
                cx = x;
            if (i == fg) {
                focus_x = x;
                focus_y = boxy + r * BASE;
            }
            if (x + aw > box_x1())
                break;                 /* ★draw24_gid は横の範囲を見ない */
            draw24_gid(box, bufh, x + ishape[i].dx, y + ishape[i].dy,
                       ishape[i].gid, ishape[i].face, INK);
            /* ★未確定の下線は**論理の位置**で決める（文節は字で数えるので）。
               引く幅は**画の送り**。合字だと 2 字ぶんが 1 画になるが、
               そのときは下線も 1 画ぶん引くのが正しい。 */
            if (ua >= 0 && lg >= ua && lg < ub) {
                const int on = (lg >= fa && lg < fb);
                const uint16_t col = on ? ACCENT : DIM;
                for (int t = 0; t < (on ? 2 : 1); t++)
                    for (int c = 0; c < aw; c++)
                        box[y + BASE - 1 - t][x + c] = col;
            }
            x += aw;
        }
        if (car < 0 || top + r != cl)
            continue;
        if (cg >= i1) {                /* 行末: 字の右へ 3px */
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
         無かったので右端＝右下だったが、3 行になると 1 行目は「中途半端に上」。
       ★英語では**その行の面ボタンの字を全部**入れる（実機の指摘・2026-09-01）——
         1 字では「この行に何があるか」が見えず、インジケータとして働かない。
       ★シフトの印は**枠の外の左**（同）。枠の中に空き枠を用意すると落ち着かない。 */
    int iw = 0;
    for (int i = 0; i < ilen; i++)
        iw += glyph_w(ind[i]);
    const int inner = seat == SEAT_EN ? EN_IND_W : (iw < 24 ? 24 : iw);
    const int kx = W - MARGIN - inner;
    const int iy = bufh - BASE;
    for (int c = kx - 3; c <= kx + inner + 2; c++)
        box[iy][c] = box[iy + BASE - 1][c] = DIM;
    for (int r = 0; r < BASE; r++)
        box[iy + r][kx - 3] = box[iy + r][kx + inner + 2] = DIM;
    /* ★縦は **ink で**中央へ（ink_dy の頭書き）—— 器の余りは下に偏っているので、
       器の上端に置くと**中身が上に寄って見える**（実機の指摘・2026-09-03）。
       ★印は枠の外だが、同じ器に同じ規則で置けば箱の中身と目線が揃う。 */
    const int idy = ind_dy();
    for (int i = 0, ix = kx + (inner - iw) / 2; i < ilen; i++) {   /* 箱の中で中央寄せ */
        draw24(box, bufh, ix, iy + idy, ind[i], ACCENT);
        ix += glyph_w(ind[i]);
    }
    if (mark)                          /* ★枠の外（左）。隙間 4px */
        draw24(box, bufh, kx - 3 - 4 - glyph_w(mark), iy + mark_dy(), mark, ACCENT);

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

/* ---- パッド → デーヴァナーガリー ----
 *
 * ★★**かなの席とは物理入力の読み方が違う。** 子音は「LS で latch した varga ×
 *   D-pad の位置」の**2 次元**で決まり（LB がその varga の鼻音）、面ボタンが母音。
 *   ★だから両スティックとも埋まり、**キャレット移動は RT+LS へ退避**している。
 *   実測で決めた割り当てで、逆にすると子音の 69% で RT を噛む（TODO の H2 の節）。
 * ★**RS ← と ↓ はここでは読まない** —— BS と句読点は席に依存しない操作なので、
 *   かなの席と同じ場所（interactive の rs の分岐）で見る。
 */

/* varga 内の位置。★D-pad ↑→↓← が index 0..3（devanagari_tables.h の並び）。 */
static int pad_deva_dpad(int p)
{
    return (p & BTN_UP) ? 0 : (p & BTN_RIGHT) ? 1 : (p & BTN_DOWN) ? 2
         : (p & BTN_LEFT) ? 3 : -1;
}

/* 左スティックの向き（0=中立 1=↑ 2=→ 3=↓ 4=←）。
   ★★**ls_dirs とは並びが違う。** あちらは「← → ↑ ↓」の 4 本の旗、こちらは
     `GpFrameDeva.lsDir` の 1 つの番号で、中立が 0（＝ LS を倒さない状態が प varga）。
     ★varga 表の並び（音韻の順）とも別で、これは**方向の順** —— 変換は
     input.c の varga_of が持つ（そこを間違えて test_input が捕まえた）。 */
static int pad_deva_ls(const uint8_t axes[4])
{
    int d[4];
    ls_dirs(axes, d);
    return d[2] ? 1 : d[1] ? 2 : d[3] ? 3 : d[0] ? 4 : 0;
}

/* rs = pad_rstick_flick の戻り（2=↑ anusvara / 3=→ 長母音）。 */
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

/* ★対話ループに入るときの引き継ぎ。**押されたままのものを「もう見た」ことにする**。
   忘れると、押しっぱなしのボタンに応じた字が入った状態で始まる
   （Zenmai が 2026-08-29 に実機で踏んだ形。台本が素通りしていた）。 */
static void carry_over_pad(void)
{
    uint8_t axes[4] = { 0x80, 0x80, 0x80, 0x80 };
    pad_prev = pad_read_ex(axes);
    if (pad_prev < 0)
        pad_prev = 0;
    GpFrameKana f = frame_of(pad_prev, 0);
    gp_sync_prev_kana(&gm, &f);
    /* ★★**デーヴァナーガリーの席にも同じ引き継ぎが要る。** こちらは LS の latch と
       「RT を離したら halant」があるので、忘れると**入った瞬間に子音が 1 つ出る**か
       **halant が 1 つ乗る**。★席ごとに 2 回呼ぶ形にすると、いつか片方を忘れる
       （punct_break を comp_insert の中でやっているのと同じ判断）ので、ここで両方。 */
    GpFrameDeva df = deva_frame_of(pad_prev, axes, 0, 0);
    gp_sync_prev_deva(&dvm, &df);
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
   帯は各行の 4px 上から MENU_ROW なので、上の余白 = 12-4 = 8。
   下も 8 にするには MENU_H = N*MENU_ROW + 16（+24 にしていたら下だけ 16 になって、
   実機で「やめるの下だけ広い」と言われた・2026-08-31）。
   ★★**行の高さは器（BASE）から導く。** もとは 24 を前提にした 32 が直に書いてあり、
     器を 30 へ広げたとき（デーヴァナーガリー）に**帯が次の行へ 6px 食い込み、
     下の余白が 8 → 2 になった**（実機の指摘・2026-09-03）。
     ★同じ形を `render.h` の BODY_H / CMD_Y / WIN_H でも踏んでいる ——
       **器を前提にした数は、器から導いておかないと必ず 1 つ直し忘れる。** */
enum { MENU_N = 2, MENU_W = 320, MENU_ROW = BASE + 8, MENU_H = MENU_N * MENU_ROW + 16 };
static uint16_t menubox[MENU_H][W];

/* ★**2 行を同じ位置に座らせる**ため、器は全行まとめて測る（ind_dy と同じ考え）。 */
static int menu_dy(void)
{
    static int dy, done;
    if (!done) {
        uint16_t t[64];
        int n = 0;
        for (int i = 0; i < MENU_N; i++)
            n += u8_to_u16(MENU[i], -1, t + n, (int)(sizeof t / sizeof *t) - n);
        dy = ink_dy(t, n);
        done = 1;
    }
    return dy;
}

static void menu_draw(int sel)
{
    const int x = ((W - MENU_W) / 2) & ~1;
    fill_rect(menubox, x, 0, MENU_W, MENU_H, OPT_BG);
    for (int i = 0; i < MENU_W; i++)
        menubox[0][x + i] = menubox[MENU_H - 1][x + i] = OPT_EDGE;
    for (int j = 0; j < MENU_H; j++)
        menubox[j][x] = menubox[j][x + MENU_W - 1] = OPT_EDGE;

    const int dy = menu_dy();          /* ★縦は ink で中央へ（インジケータと同じ規則） */
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
            fill_rect(menubox, x + 8, y - 4, MENU_W - 16, MENU_ROW, ACCENT);
        int cx = x + (MENU_W - w) / 2;
        for (int k = 0; k < n; k++) {
            draw24(menubox, MENU_H, cx, y + dy, t[k], i == sel ? OPT_BG : INK);
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
    static char buf[4096];
    /* ★入口は席ごと。英語は ELIZA が空白で切れるのでそのまま、日本語は分かち書き、
       ★韓国語は**띄어쓰기があるので分かち書きは要らず**、語にくっついた
         조사・어미 を剥がすだけ（josa）。 */
    ko_commit(&ko);
    deva_commit(&dv);
    /* ★★**ヒンディー語の入口は hindi_in**（danda を `.` に写す / nukta を分解形へ）。
       分かち書きも助詞剥がしも要らない（空白があり、後置詞は分離語）ので、
       ELIZA から見ると「空白区切りの言語が 1 つ増えた」だけ ——
       ★ただし **danda を写さないと節が切れず、埋め戻しが文をまたぐ**（hindi.h の注記）。 */
    if (seat == SEAT_HI)
        hindi_in(typed, n, buf, (int)sizeof buf);
    else if (seat == SEAT_EN)
        u16_to_u8(typed, n, buf, (int)sizeof buf);
    else if (seat == SEAT_KO)
        josa(typed, n, buf, (int)sizeof buf);
    else
        wakachi(typed, n, buf, (int)sizeof buf);
    draw_echo(typed, n);                       /* 打った通りを反響（＞付き） */
    hist_blank();
    say(hg_eliza_response(seat_slot(), buf), INK);
    scroll_new();
}

/* 打っている行を ELIZA へ渡して欄を空にする。戻り値 = 送ったか。
 * ★**関数にしてある理由**: 呼ぶ場所が 2 つある —— かなの席の L3（3 段目）と、
 *   デーヴァナーガリーの席の RT+L3（`DEVA_OP_COMMIT`）。★同じ後始末
 *   （合成を切る・巡回を切る・eager を落とす）を 2 か所に書くと必ずずれる。
 */
static int send_line(void)
{
    if (!clen)
        return 0;
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
    ko_commit(&ko);
    deva_commit(&dv);
    punct_break();                     /* ★送ったら巡回も切る（欄が空になったので） */
    send(typed, typed_n);
    return 1;
}

/* ---- デーヴァナーガリーの席の 1 フレーム ----
 *
 * ★入力層（gp_step_deva）が返すのは**字ではなく指示**で、合成（schwa の置換 /
 *   長母音への昇格 / トグル）は devanagari.c が持つ。ここはその 2 つを繋ぐだけ。
 * ★★**キャレット移動と改行もここに来る**（DEVA_OP_CURSOR / DEVA_OP_COMMIT）——
 *   席に依存しない操作だが、「RT が何に使われたか」の判定（halant を抑止するか）が
 *   入力側にあるので、同じ条件をここへもう一度書くと**二重になって必ずずれる**。
 * 戻り値 = 描き直しが要るか。
 */
static int step_deva_frame(int p, const uint8_t axes[4], int rs, int fields)
{
    GpFrameDeva f = deva_frame_of(p, axes, rs, fields * 17);
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
            /* ★matra を先に試し、子音が無ければ独立母音 —— **どちらになるかは
               合成器が状態を見て決める**ので、ここは両方を渡すだけ。 */
            o = deva_matra(&dv, a[i].deva_ch);
            if (!o.ok)
                o = deva_vowel(&dv, a[i].deva_alt);
            break;
        case DEVA_OP_HALANT:   o = deva_halant(&dv); break;
        case DEVA_OP_NUKTA:    o = deva_nukta(&dv); break;
        case DEVA_OP_ANUSVARA: o = deva_toggle_anusvara(&dv); break;
        case DEVA_OP_VISARGA:  o = deva_visarga(&dv); break;
        case DEVA_OP_LONG:     o = deva_long_shift(&dv); break;
        case DEVA_OP_CURSOR: {
            /* ★向きは 横 -1/+1・縦 -2/+2。**符号を戻してから読む**
               （GpAction は unsigned short で運んでいる）。 */
            const int d = (short)a[i].deva_ch;
            if (d == -1 && caret > 0) {
                caret_set(caret - 1);
                dirty = 1;
            } else if (d == 1 && caret < clen) {
                caret_set(caret + 1);
                dirty = 1;
            } else if (d == -2) {
                if (caret_line_move(-1))
                    dirty = 1;
                else if (!clen)
                    view_scroll(-SCROLL_STEP);
            } else if (d == 2) {
                if (caret_line_move(1))
                    dirty = 1;
                else
                    view_scroll(SCROLL_STEP);
            }
            continue;
        }
        case DEVA_OP_COMMIT:
            if (send_line())
                dirty = 1;
            continue;
        default:
            /* ★★**default で「それらしい処理」に落とさない。** 新しい op を足した
               ときに黙って別のものになる —— test_input で実際に踏んだ形
               （CURSOR が long_shift になり `क` が `का` になっていた）。 */
            continue;
        }
        if (o.ok) {
            /* ★`o.len` は 0 のことがある（トグルを外す・backspace）——
               `comp_insert(text, 0, replace)` がそのまま「消す」になる。 */
            comp_insert(o.text, o.len, o.replace);
            dirty = 1;
        }
    }
    gp_sync_prev_deva(&dvm, &f);
    return dirty;
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
 *   右スティック ← BS / ↑ 濁点・半濁点 / → 長音「ー」 / ↓ 、。全角空白
 *   Select = 相手を替える（日本語 DOCTOR ⇔ 1966 年の原典）。★欄が空のときだけ
 *   ★英語のときは GIME の英語モードに揃える（docs は Sources/GIME/SPEC.md）:
 *     L2 = Shift（2 度押しで SmartCaps / 長押しで CapsLock）/ R2 = 数字 0
 *     右スティック ← BS / ↑ アポストロフィ / → スラッシュ / ↓ 空白→ピリオド→カンマ
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
    int prev_row = -1;
    uint16_t prev_mark = 0;

    gp_init(&gm);
    deva_machine_init(&dvm);
    deva_init(&dv);
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

        /* 右スティックのフリック（1=← 2=↑ 3=→ 4=↓）。
           ★★**席の分岐より先に読む。** デーヴァナーガリーでは ↑（anusvara）と
             →（長母音）が**フレームの一部**として gp_step_deva に渡るので、
             かなの席のように「あとから見る」ことができない。
           ★pad_rstick_flick は 1 フレームに 1 回だけ呼ぶこと（内部にエッジの記憶がある）。 */
        const int rs = pad_rstick_flick(axes);

        /* 行インジケータの中身を決める値。★**席で意味が違う** —— かなの席では
           子音行（0..9）、デーヴァナーガリーでは latch した varga と層。
           ★中身が (row, seat, mark) だけで決まる、という約束はどちらでも保つ。 */
        int row;
        if (seat == SEAT_HI) {
            /* ★デーヴァナーガリー（別の入口）。かなの状態機械（gm）は動かさない。 */
            if (step_deva_frame(p, axes, rs, fields))
                dirty = 1;
            row = dvm.lsLatch * 4 + dvm.nonVarga * 2 + ((p & BTN_L2) != 0);
        } else {
            /* かな（同時押し）*/
            GpFrameKana f = frame_of(p, fields * 17);
            row = f.row;
            GpAction a[4];
            const int an = gp_step_kana(&gm, seat_lang(), &f, a);
            for (int i = 0; i < an; i++) {
                /* ★変換中に打ち始めたら、その時点の選択で確定してから入れる
                   （実機の指摘・2026-08-31）。IME の作法。 */
                if (conv_active())
                    conv_accept();
                if (a[i].type == GPA_KANA) {
                    comp_insert(a[i].text, a[i].tlen, a[i].replace);
                } else if (a[i].type == GPA_YOUON) {   /* 後置の拗音シフト */
                    uint16_t o_[2];
                    int rep;
                    const int l = gp_resolve_youon(comp, caret, o_, &rep);
                    if (l && rep > 0)
                        comp_insert(o_, l, rep);
                } else {                   /* GPA_KO_*（音節・받침・複合母音）*/
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

        /* ★↑ と → は**デーヴァナーガリーでは読まない** —— どちらもフレームの一部として
           gp_step_deva へ渡してある（anusvara / 長母音）。ここで二重に見ない。 */
        if (rs == 2 && seat != SEAT_HI) {   /* ↑ 濁点 / 英語 = アポストロフィ / 韓国語 = 子音サイクル */
            if (seat == SEAT_EN) {
                const uint16_t ch = 0x0027;    /* ' */
                comp_insert(&ch, 1, 0);
                dirty = 1;
            } else if (seat == SEAT_KO) {
                /* ★받침があれば받침、無ければ초성を巡回（평음 → 격음 → 경음）。
                   ★日本語の濁点トグルと**同じ指**に来るのは偶然ではなく、
                     どちらも「直前の字を濁らせる」操作だから。 */
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
        if (rs == 1) {                 /* ← BS */
            if (conv_active()) {
                /* ★変換中は**未確定のかなに戻す**（普通の IME と同じ・実機の指摘）。
                   1 字消すのではなく、変換そのものをやめる。 */
                conv_cancel();
                dirty = 1;
            } else if (caret) {
                /* ★★デーヴァナーガリーでは**合成器にも消したことを伝える** ——
                   伝えないと、消したあとの字に matra が乗ったり anusvara が
                   トグルできたりする（韓国語で받침のキャンセルが落ちていたのと同じ形）。
                   ★合成中でなければ何も返さないので、そのまま素の BS になる。 */
                if (seat == SEAT_HI)
                    deva_backspace(&dv);
                comp_bs();
                gm.eagerSet = 0;
                ko_commit(&ko);        /* ★消した字に받침を足せてしまうのを防ぐ */
                dirty = 1;
            }
        }
        /* ★→ もデーヴァナーガリーでは読まない（長母音への昇格はフレームで渡してある）。 */
        if (rs == 3 && seat != SEAT_HI) {   /* → 長音「ー」/ 英語 = スラッシュ / 韓国語 = 複合母音 */
            if (conv_active())
                conv_accept();
            if (seat == SEAT_KO) {
                /* ★ㅏ/ㅓ を足して複合母音にする（ㅗ→ㅘ・ㅜ→ㅝ）。
                   日本語の長音と同じ指 —— どちらも「直前の母音を伸ばす」操作。 */
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
                    gm.engSmartCaps = 0;   /* ★GIME もここで解く（`/` は語の切れ目） */
                dirty = 1;
            }
        }
        if (rs == 4) {
            /* ★↓ = 句読点。1 回「、」/ 2 回「。」/ 3 回 全角空白。
               窓の中に次が来たら**前の字を差し替える**（GamepadEngine と同じ。
               あちらの PUNCTUATION_DOUBLE_TAP_MS は 600ms）。 */
            if (conv_active())
                conv_accept();
            const int within = punct_at && fields < punct_at;
            const int step_n = within ? punct_n + 1 : 1;
            /* ★英語は **空白 → . → ,**（GIME の英語モードと同じ順）。日本語は 、→。→ 全角空白。
               ★英語で最初に来るのが空白なのは、語の切れ目が一番よく要るから ——
                 ELIZA は空白で語を切るので、**空白が打てないと 1 語しか送れない**。 */
            /* ★★**韓国語だけ 5 段**（空白 → . → ? → , → '）。韓国語は
                 **記号を置く固定枠が無い**（↑ が子音サイクル・→ が複合母音で埋まり、
                 母音グリッドにも隙間が無い）ので、ここに置くしかない。
               ★`?` はエンジンが `.` に潰すが、**画面には打った通りが出る**ので入れる
                 （日本語の わ行お段 `？` と同じ理由。gen_tables.py の注記）。
               ★`'` は DOCTOR の応答が引用に使う —— **機械が印字する字はユーザーも
                 打てる**、という日本語・英語で保たれている性質を韓国語でも保つ。 */
            /* ★★**ヒンディー語は 4 段**（空白 → `।` → `,` → `॥`）。空白が最初なのは
                 英語と同じ理由 —— ELIZA は空白でしか語を切らない。
               ★★`,` を入れたのは**DOCTOR の応答が読点を打つから**
                 （「नमस्ते। बताइए, आपको क्या परेशानी है」）—— **機械が印字する字は
                 ユーザーも打てる**、という日本語・英語・韓国語で保たれている性質を
                 ここでも保つ。★デーヴァナーガリーの席は面ボタンが母音で埋まっていて
                 記号を置く固定枠が無いので、韓国語と同じくここに置くしかない。
               ★`॥`（double danda）は詩文の切れ目で日常には出ないので最後。 */
            static const uint16_t PUNCT_JA[] = { 0x3001, 0x3002, 0x3000 };
            static const uint16_t PUNCT_EN[] = { 0x0020, 0x002E, 0x002C };
            static const uint16_t PUNCT_KO[] = { 0x0020, 0x002E, 0x003F, 0x002C, 0x0027 };
            static const uint16_t PUNCT_HI[] = { 0x0020, 0x0964, 0x002C, 0x0965 };
            const uint16_t *tbl = seat == SEAT_EN ? PUNCT_EN
                                : seat == SEAT_KO ? PUNCT_KO
                                : seat == SEAT_HI ? PUNCT_HI : PUNCT_JA;
            const int steps = seat == SEAT_KO ? 5 : seat == SEAT_HI ? 4 : 3;
            int idx = step_n - 1;
            if (idx >= steps)
                idx = steps - 1;
            const uint16_t ch = tbl[idx];
            const int rep = step_n > 1 ? 1 : 0;
            /* ★空白や記号が来たら SmartCaps を解く（step_english が表の字に対して
               やっているのと同じこと。こちらは表を通らない字なので手で解く）。
               ★**CapsLock は落とさない** —— 明示して掛けた固定は明示した操作で解く。 */
            if (seat == SEAT_EN)
                gm.engSmartCaps = 0;
            ko_commit(&ko);            /* ★記号が来たら音節はそこで終わり */
            deva_commit(&dv);          /* ★同じ理由（記号に matra が乗らないように） */
            comp_insert(&ch, 1, rep);  /* ★ここで punct_break される */
            /* ★**入れたあとに覚え直す。** こうしておくと「巡回が続くのは、
               直前に入れたのがこの記号だったときだけ」が構造として保証される。 */
            if (step_n < steps) {
                punct_n = step_n;
                punct_at = fields + PUNCT_WINDOW;
            }
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

        if (edge & BTN_SELECT) {
            /* ★Select = 相手を替える。**欄が空のときだけ**効く ——
               切り替えは「相手を替える」ことなので、送っていない文が残っていると
               それが誰宛てなのか決まらない。★1 通に 2 言語が混ざる道がそもそも
               無くなるので、ELIZA 側に言語の判定を持ち込まずに済む。
               ★替わったことは**新しい相手の挨拶**で分かる（画面に出る唯一の印）。 */
            if (!clen) {
                /* ★席が 3 つになったので**巡回**にした（日 → 英 → 韓 → ヒンディー → 日）。
                   ★2 択のときは「戻る」が自明だったが、3 つ以上では一周しないと
                     戻れない —— そこはメニュー行きだが、まず巡回で実機に出す。
                   ★★**4 つ目でここが限界に来ている**（TODO の残件 1 = メニュー）。 */
                seat = (seat + 1) % SEAT_N;
                /* GIME と同じくシフト状態は持ち越さない */
                gm.engShiftNext = 0;
                gm.engSmartCaps = 0;
                gm.engCapsLock = 0;
                gm.eagerSet = 0;
                gm.korAllReleased = 1;
                gm.korPatchimRollback = 0;
                ko_commit(&ko);
                /* ★★デーヴァナーガリーも同じく持ち越さない。**そのうえで、いま
                   押されているものを「もう見た」ことにする** —— しないと、Select を
                   押した指が肩ボタンに掛かっていただけで、席に入った瞬間に子音や
                   halant が 1 つ出る（carry_over_pad と同じ配慮）。
                   ★ここで carry_over_pad を呼ばないのは、**あれがパッドを読み直す**から
                     —— 台本の再生はポーリング回数が時間軸なので、1 回余分に読むと
                     既存の台本が全部 1 コマずれる。 */
                deva_machine_init(&dvm);
                deva_init(&dv);
                {
                    GpFrameDeva df = deva_frame_of(p, axes, 0, 0);
                    gp_sync_prev_deva(&dvm, &df);
                }
                punct_break();
                hist_blank();
                say(hg_eliza_greeting(seat_slot()), INK);
                scroll_new();
                prev_row = -1;         /* ★行が同じでも箱の中身が変わる */
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

        /* ★★**デーヴァナーガリーでは L3 を通さない** —— あちらでは L3 が
           「非 varga 層に入る」で、送るのは **RT+L3**（gp_step_deva が
           DEVA_OP_COMMIT で返し、step_deva_frame が send_line を呼ぶ）。
           ★変換も未確定も無い席なので、かなの 3 段はそもそも意味を持たない。 */
        if (seat != SEAT_HI) {
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
            } else if ((edge & BTN_L3) && send_line()) {    /* L3 = 確定して ELIZA へ */
                dirty = 1;
            }
        }

        /* ---- 左スティック ----
         * ★作法は GamepadEngine（labo の web/src/gamepad/engine.ts）に揃える:
         *     ↓ 変換開始 / 次候補    ↑ 前候補    ←→ 文節移動
         *     ★**RT + ←→ = 文節伸縮**（RT は「使用済み」にして「ん」を抑止する）
         *   変換していないとき: ←→ キャレット移動 / ↑↓ 履歴を遡る（打っていないとき）
         * ★リピートは**変換中はしない**（候補送りの暴走を防ぐ）。engine.ts と同じ判断。
         *   間隔も同じ（400ms 待って 85ms ごと ＝ 1 field 17ms なので 24 / 5）。
         */
        if (seat == SEAT_HI) {
            /* ★★**デーヴァナーガリーでは LS は varga の latch**（gp_step_deva が読む）。
               ここでは**前フレームだけ覚え直す** —— 覚えないと、倒したまま席を戻した
               ときにエッジとして発火して、いきなりキャレットが動く。 */
            int ls[4];
            ls_dirs(axes, ls);
            for (int d = 0; d < 4; d++) {
                ls_prev[d] = ls[d];
                ls_at[d] = 0;
            }
        } else {
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

        /* 行インジケータ。★英語は**その行の面ボタンの字を全部**（`abc` / `pqrs`）。
           ★シフトが効いていれば**大文字で見せる** —— 打つ前に分かってほしい
             （実機の指摘・2026-09-01）。
           ★印は**色ではなく形**で分ける（小さい画面で色差は読めない。`⇪` は
             同梱フォントに無く、出すと黒い四角になるので使えない）:
               ⇧ 次の 1 字だけ / △ 空白・記号まで（SmartCaps）/ ▲ 解除するまで（CapsLock） */
        uint16_t ind[4];
        int ilen;
        uint16_t mark = 0;
        if (seat == SEAT_KO) {
            /* ★韓国語は**行の子音 1 字**（日本語の「あ」と同じ形）。
               母音は面ボタンで、RT で層が替わる —— 印は付けない（英語のシフトと違い、
               ★押している間だけの層なので、状態として残らない）。 */
            ind[0] = ko_row_char(row);
            ilen = 1;
        } else if (seat == SEAT_HI) {
            /* ★デーヴァナーガリーは**いま latch している varga の見出し字**
               （無気無声 ＝ varnamala の頭）。非 varga 層のときはその層の頭。
               ★★**latch なので「押している間」ではなく状態として残る** ——
                 だから、ここに出ていないと**次に何が出るか分からない**。
                 韓国語の行や日本語の「あ」より、この席では効きが大きい。
               ★印は付けない —— 層（L3）は 1 子音で自動 OFF、LT は押している間だけで、
                 どちらも「解除するまで残る」たぐいの状態ではない。 */
            ind[0] = deva_head_char(&dvm, (p & BTN_L2) != 0);
            ilen = 1;
        } else if (seat == SEAT_EN) {
            ilen = gp_row_chars_en(row, ind, 4);
            if (gm.engCapsLock)       mark = 0x25B2;   /* ▲ */
            else if (gm.engSmartCaps) mark = 0x25B3;   /* △ */
            else if (gm.engShiftNext) mark = 0x21E7;   /* ⇧ */
            if (mark)
                for (int i = 0; i < ilen; i++)
                    if (ind[i] >= 'a' && ind[i] <= 'z')
                        ind[i] -= 0x20;
        } else {
            ind[0] = gp_row_char(row);
            ilen = 1;
        }
        /* ★中身は (row, seat, mark) だけで決まるので、行と印を見れば足りる。 */
        if (dirty || row != prev_row || mark != prev_mark) {
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
                draw_input(t, tn, -1, ind, ilen, mark, ua, ub, fa, fb);
                draw_cands();          /* ★入力欄を描いたあと（focus_x を使う） */
            } else {
                /* ★変換をやめた回は本文を描き直す —— 候補窓が本文の上に
                   開いているので、消すには下を描き戻すしかない。 */
                if (prev_conv)
                    render_window();
                /* ★**未確定のかな（comp[fixed..clen)）にも下線を引く。**
                   引かないと「どこまでが確定済みか」が、変換して初めて分かる
                   （実機の指摘・2026-08-31）。注目文節はまだ無いので fa/fb は -1。 */
                draw_input(comp, clen, caret, ind, ilen, mark, fixed, caret, -1, -1);
            }
            prev_conv = conv_active();
            prev_row = row;
            prev_mark = mark;
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

    /* ★0 = 組み込みのエラーメッセージを使わない —— あれは**英語**なので、
       日本語スクリプトのまま出ると相手が急に英語で返してくる（実測: 「パパ」「さびしい」で
       `HMMM`）。スクリプト側の `NONE`（「よく分からないのですが」ほか 4 つ）に落とす。 */
    const int len = (int)(_binary_script_txt_end - _binary_script_txt_start);
    if (!hg_eliza_init(HG_JA, _binary_script_txt_start, len, 0))
        say("スクリプトが読めない。", INK);
    else
        say(hg_eliza_greeting(HG_JA), INK);
    /* ★英語の席は**組み込みメッセージを既定（ON）のまま**にする —— あれは英語の
       文字列なので、英語の相手には嘘にならない。upstream いわく「Weizenbaum の
       ELIZA は組み込みメッセージを使っていた」ので、原典の席では触らないのが筋。 */
    const int len_en = (int)(_binary_script_en_txt_end - _binary_script_en_txt_start);
    /* ★戻り値を捨てない —— 失敗すると応答が空文字になり、**無言で「返事をしない
       相手」に見える**（読めていないことが画面のどこにも出ない）。 */
    if (!hg_eliza_init(HG_EN, _binary_script_en_txt_start, len_en, 1))
        say("英語のスクリプトが読めない。", INK);
    /* ★韓国語は日本語と同じく **0**（組み込みメッセージを使わない）—— スクリプトが
       100% 韓国語なので、`HMMM` が出ると相手が急に英語で返すことになる。
       ★検査 tools/coverage_ko.py が「応答にラテン文字が出たら失敗」で見張っている。 */
    const int len_ko = (int)(_binary_script_ko_txt_end - _binary_script_ko_txt_start);
    if (!hg_eliza_init(HG_KO, _binary_script_ko_txt_start, len_ko, 0))
        say("韓国語のスクリプトが読めない。", INK);
    /* ★ヒンディー語も **0**（組み込みメッセージを使わない）—— 英語が出ると相手が
       急に英語で返す。★検査 tools/coverage_hi.py が「応答にラテン文字が出たら失敗」で
       見張っている（キーワード 125 語 × 4 形 = 500 件）。 */
    const int len_hi = (int)(_binary_script_hi_txt_end - _binary_script_hi_txt_start);
    if (!hg_eliza_init(HG_HI, _binary_script_hi_txt_start, len_hi, 0))
        say("ヒンディー語のスクリプトが読めない。", INK);

    view_bottom();
    render_window();
    const uint16_t ind0 = 0x3042;      /* 「あ」 */
    draw_input(0, 0, 0, &ind0, 1, 0, -1, -1, -1, -1);
    gpu_display_on();

    pad_try_analog();
    interactive();
    return 0;
}
