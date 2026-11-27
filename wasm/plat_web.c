/* Zenmai プラットフォーム実装 — ブラウザ（Emscripten）。
 *
 * plat_sdl.c の対。境界は plat.h の 9 本だけで、main.c・render.c・input.c・conv.c は
 * 1 行も触らずにそのまま動く。★`plat_sdl.c` は Zenmai と共有の殻なので**分岐を足さない**
 * —— ブラウザは SDL を通さずに canvas と Gamepad API を直に触る。
 *
 * ★**入力はゲームコントローラだけ**（キーボード・タッチは読まない）。
 *   Gamepad API の "standard" 配置は**位置で決まっている**（buttons[0] = 下 / [1] = 右 /
 *   [2] = 左 / [3] = 上）ので、plat_sdl.c が悩んだ「SDL の A/B/X/Y から位置が
 *   復元できない」はここでは起きない。盤の図（✕ = 下 / ○ = 右 / □ = 左 / △ = 上）と
 *   そのまま合う。
 *
 * ★**待つ = ブラウザへ制御を返す**（Asyncify）。上の層は `for (;;) { …; wait_fields(1); }`
 *   の形で回っているので、wait_fields の中で emscripten_sleep するだけで済む ——
 *   主ループを「1 フレームずつ呼ばれる関数」に書き直さなくてよい。
 *
 * ★台本の再生（ZENMAI_SCRIPT / ZENMAI_STOP / ZENMAI_RAW）は plat_sdl.c と同じ形を持つ。
 *   node で回して native の shots.sh と**同じ絵が出るか**を md5 で突き合わせるため
 *   （wasm/shots.mjs）。★書式を変えるときは**両方**直すこと。
 */
#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plat.h"

static uint16_t fb[H][W];              /* 画面そのもの（PS1 の VRAM 表示領域に対応） */
static uint32_t rgba[H * W];           /* canvas へ渡す形（RGBA8888） */
static int dirty = 1;                  /* ★変わったときだけ canvas へ流す */
static double next_ms;                 /* 次のフィールドの目標時刻（積算で刻む） */

static int script_step(int *p, uint8_t axes[4]);

/* ---- 終わり方 ----
 *
 * ★**main() が返ったら exit() する**（main.c は build.sh が `-Dmain=higgins_main` で焼く）。
 *   native では main が返る＝プロセスが終わる（メニューの「やめる」）。ところが wasm では、
 *   Asyncify で一度でも眠ったあと（変換エンジンを待った conv_init より後）に main が返ると、
 *   **emscripten はそれを終わりと扱わず、黙って止まる** —— ブラウザでは画面が固まるだけで
 *   onExit も来なかった（台本で確かめた）。exit() なら眠ったあとでも onExit まで届く。
 */
int higgins_main(void);

int main(void)
{
    exit(higgins_main());
}

/* ---- 画面 ---- */

EM_JS(void, web_canvas_init, (int w, int h), {
    if (typeof document === 'undefined') return;           /* node（台本の再生）*/
    const c = Module.canvas || document.getElementById('screen');
    if (!c) return;
    c.width = w; c.height = h;
    Module.hgCtx = c.getContext('2d');
    Module.hgImg = Module.hgCtx.createImageData(w, h);
});

EM_JS(void, web_canvas_put, (const void *px, int bytes), {
    if (!Module.hgCtx) return;
    Module.hgImg.data.set(HEAPU8.subarray(px, px + bytes));
    Module.hgCtx.putImageData(Module.hgImg, 0, 0);
});

void gpu_init(void)
{
    static int done;
    if (done)                          /* PS1 版は _start の周回で毎回呼ぶので冪等にする */
        return;
    done = 1;
    web_canvas_init(W, H);
    memset(fb, 0, sizeof fb);
    next_ms = emscripten_get_now();
}

void gpu_display_on(void)
{
    /* fb は最初から黒なので何もしなくてよい（plat_sdl.c と同じ）。 */
}

/* PS1 の GPU フィルは色を 0xBBGGRR で受け取る（GP0(02h) の並び）。 */
void gp0_fill(int x, int y, int w, int h, uint32_t rgb24)
{
    uint16_t c = (uint16_t)(((rgb24 & 0xF8) >> 3)              /* R */
                          | ((rgb24 & 0xF800) >> 6)            /* G */
                          | ((rgb24 & 0xF80000) >> 9));        /* B */
    for (int j = 0; j < h; j++) {
        int dy = y + j;
        if (dy < 0 || dy >= H) continue;
        for (int i = 0; i < w; i++) {
            int dx = x + i;
            if (dx >= 0 && dx < W) fb[dy][dx] = c;
        }
    }
    dirty = 1;
}

void gp0_upload(int x, int y, int w, int h, const uint16_t *src)
{
    for (int j = 0; j < h; j++) {
        int dy = y + j;
        if (dy < 0 || dy >= H) continue;
        int x0 = x < 0 ? 0 : x;
        int x1 = x + w > W ? W : x + w;
        if (x1 > x0)
            memcpy(&fb[dy][x0], src + (size_t)j * w + (x0 - x),
                   (size_t)(x1 - x0) * sizeof(uint16_t));
    }
    dirty = 1;
}

void gp0_copy(int sx, int sy, int dx, int dy, int w, int h)
{
    if (w > W) w = W;
    if (dy > sy) {
        for (int j = h - 1; j >= 0; j--)
            if (sy + j >= 0 && sy + j < H && dy + j >= 0 && dy + j < H)
                memmove(&fb[dy + j][dx], &fb[sy + j][sx], (size_t)w * sizeof(uint16_t));
    } else {
        for (int j = 0; j < h; j++)
            if (sy + j >= 0 && sy + j < H && dy + j >= 0 && dy + j < H)
                memmove(&fb[dy + j][dx], &fb[sy + j][sx], (size_t)w * sizeof(uint16_t));
    }
    dirty = 1;
}

/* BGR555（`x BBBBB GGGGG RRRRR`）→ RGBA8888。★5 bit を 8 bit へは上位を下に複写して
   伸ばす（SDL の BGR555 テクスチャと同じ丸め）。 */
static void present(void)
{
    if (!dirty)
        return;
    dirty = 0;
    const uint16_t *s = &fb[0][0];
    for (int i = 0; i < W * H; i++) {
        unsigned v = s[i];
        unsigned r = v & 31, g = (v >> 5) & 31, b = (v >> 10) & 31;
        r = (r << 3) | (r >> 2);
        g = (g << 3) | (g >> 2);
        b = (b << 3) | (b >> 2);
        rgba[i] = 0xFF000000u | (b << 16) | (g << 8) | r;
    }
    web_canvas_put(rgba, (int)sizeof rgba);
}

static int headless(void)
{
    static int h = -1;
    if (h < 0) {
        const char *s = getenv("ZENMAI_SCRIPT");
        h = s && *s ? 1 : 0;
    }
    return h;
}

/* ★n フィールド（1/60 秒）待つ。提示もここでやる（plat_sdl.c と同じ寄せ方）。
   目標時刻を積算するので、1 回遅れても後で取り返して時計がずれない。 */
void wait_fields(int n)
{
    if (headless())                    /* ★台本の再生中は待たない（検査を待たせない） */
        return;
    for (; n > 0; n--) {
        present();
        next_ms += 1000.0 / 60;
        double now = emscripten_get_now();
        if (next_ms - now > 0)
            emscripten_sleep((unsigned)(next_ms - now));
        else {
            if (now - next_ms > 250)
                next_ms = now;         /* 大きく遅れたら取り返しを諦める（早送りを防ぐ） */
            emscripten_sleep(0);       /* ★遅れていても一度は返す（返さないと画面が固まる） */
        }
    }
}

/* ---- パッド ---- */

void pad_try_analog(void)
{
    /* Gamepad API は最初からアナログ。PS1 の「アナログ化コマンド」に相当なし。 */
}

/* ★繋がっている最初のパッドを読む。-1 = 1 台も無い。
   ★ボタンの値を PS1 のビットへ写すのはここ（JS）で済ませる —— 配列を C へ渡すより短い。
   ★L2 / R2 はアナログトリガ。閾値は plat_sdl.c と同じ 1/4（8192 / 32768）。
   ★軸は SDL の -32768..32767 を経由して 0..255 へ落とす（plat_sdl.c の axis8 と同じ丸め）。 */
EM_JS(int, web_pad_read, (uint8_t *axes), {
    if (typeof navigator === 'undefined' || !navigator.getGamepads) return -1;
    const pads = navigator.getGamepads();
    let g = null;
    for (const p of pads) if (p && p.connected) { g = p; break; }
    Module.hgPad = g ? { id: g.id, mapping: g.mapping } : null;
    if (!g) return -1;
    const b = (i) => { const x = g.buttons[i]; return !!x && (x.pressed || x.value > 0.5); };
    const t = (i) => { const x = g.buttons[i]; return !!x && (x.value > 0.25 || x.pressed); };
    let p = 0;
    if (b(0))  p |= 1 << 14;           /* 下 = ✕ */
    if (b(1))  p |= 1 << 13;           /* 右 = ○ */
    if (b(2))  p |= 1 << 15;           /* 左 = □ */
    if (b(3))  p |= 1 << 12;           /* 上 = △ */
    if (b(4))  p |= 1 << 10;           /* L1 */
    if (b(5))  p |= 1 << 11;           /* R1 */
    if (t(6))  p |= 1 << 8;            /* L2 */
    if (t(7))  p |= 1 << 9;            /* R2 */
    if (b(8))  p |= 1 << 0;            /* SELECT */
    if (b(9))  p |= 1 << 3;            /* START */
    if (b(10)) p |= 1 << 1;            /* L3 */
    if (b(11)) p |= 1 << 2;            /* R3 */
    if (b(12)) p |= 1 << 4;            /* 十字 ↑ */
    if (b(13)) p |= 1 << 6;            /* ↓ */
    if (b(14)) p |= 1 << 7;            /* ← */
    if (b(15)) p |= 1 << 5;            /* → */
    const ax = (i) => {
        let v = Math.round((g.axes[i] || 0) * 32767);
        v = v < -32768 ? -32768 : v > 32767 ? 32767 : v;
        const a = (v + 32768) >> 8;
        return a < 0 ? 0 : a > 255 ? 255 : a;
    };
    HEAPU8[axes + 0] = ax(2);          /* 右スティック X */
    HEAPU8[axes + 1] = ax(3);          /* 右スティック Y */
    HEAPU8[axes + 2] = ax(0);          /* 左スティック X */
    HEAPU8[axes + 3] = ax(1);          /* 左スティック Y */
    return p;
});

int pad_read_ex(uint8_t axes[4])
{
    axes[0] = axes[1] = axes[2] = axes[3] = 0x80;
    {
        int scripted;
        if (script_step(&scripted, axes))
            return scripted;
    }
    int p = web_pad_read(axes);
    if (p < 0) {
        axes[0] = axes[1] = axes[2] = axes[3] = 0x80;
        return 0;                      /* ★繋がっていない = 何も押していない（plat_sdl.c と同じ） */
    }
    return p;
}

int pad_read(void)
{
    uint8_t axes[4];
    return pad_read_ex(axes);
}

/* ---- 台本の再生（検査用）----
 *
 * ★plat_sdl.c と同じ台本を食べる（行は "f0-f1:UP+TRI"、時間軸はパッドのポーリング回数）。
 *   wasm/shots.mjs が node でこれを回し、native/shots.sh と md5 を突き合わせる。
 *
 *   ZENMAI_SCRIPT=<台本>  再生する（パッドは読まない）
 *   ZENMAI_STOP=<回>      この回数で止めて書き出す（既定 40000）
 *   ZENMAI_RAW=<出力>     画面を 640×480 の RGB555 リトルエンディアンで書き出す
 */
#define SCRIPT_MAX 256
static struct { int f0, f1, mask; } sc[SCRIPT_MAX];
static int sc_n = -1;                  /* -1 = 未読み込み / 0 = 台本なし */
static int poll_n;

static int bit_of(const char *nm)
{
    static const struct { const char *nm; int bit; } B[] = {
        { "SEL", BTN_SELECT }, { "L3", BTN_L3 }, { "R3", BTN_R3 }, { "START", BTN_START },
        { "UP", BTN_UP }, { "RIGHT", BTN_RIGHT }, { "DOWN", BTN_DOWN }, { "LEFT", BTN_LEFT },
        { "L2", BTN_L2 }, { "R2", BTN_R2 }, { "L1", BTN_L1 }, { "R1", BTN_R1 },
        { "TRI", BTN_TRI }, { "CIR", BTN_CIR }, { "X", BTN_X }, { "SQ", BTN_SQ },
        /* スティックは疑似ビット（PS1 の 16 ビットの上に積む。sim.py と同じ並び） */
        { "LSL", 1 << 16 }, { "LSR", 1 << 17 }, { "LSU", 1 << 18 }, { "LSD", 1 << 19 },
        { "RSL", 1 << 20 }, { "RSR", 1 << 21 }, { "RSU", 1 << 22 }, { "RSD", 1 << 23 },
    };
    for (unsigned i = 0; i < sizeof B / sizeof *B; i++)
        if (strcmp(nm, B[i].nm) == 0)
            return B[i].bit;
    fprintf(stderr, "台本: 知らないボタン名 '%s'\n", nm);
    exit(2);
}

static void script_load(void)
{
    sc_n = 0;
    const char *path = getenv("ZENMAI_SCRIPT");
    if (!path || !*path)
        return;
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "台本が開けない: %s\n", path); exit(2); }
    char line[512];
    while (fgets(line, sizeof line, f)) {
        char *h = strchr(line, '#');
        if (h) *h = 0;
        char *colon = strchr(line, ':');
        if (!colon) continue;
        *colon = 0;
        int f0, f1;
        if (sscanf(line, " %d - %d", &f0, &f1) != 2) continue;
        int mask = 0;
        char *p = colon + 1, *tok;
        while ((tok = strtok(p, "+ \t\r\n")) != NULL) {
            p = NULL;
            mask |= bit_of(tok);
        }
        if (sc_n < SCRIPT_MAX)
            sc[sc_n++] = (typeof(sc[0])){ f0, f1, mask };
    }
    fclose(f);
    if (!sc_n) {
        fprintf(stderr, "台本にボタン行が 1 つも無い: %s\n", path);
        exit(2);
    }
}

/* 台本を再生中なら 1 を返し、p / axes をその回の内容で埋める。 */
static int script_step(int *p, uint8_t axes[4])
{
    if (sc_n < 0)
        script_load();
    if (!sc_n)
        return 0;

    const char *stop = getenv("ZENMAI_STOP");
    int limit = stop && *stop ? atoi(stop) : 40000;
    if (++poll_n >= limit) {
        const char *raw = getenv("ZENMAI_RAW");
        if (raw && *raw) {
            FILE *f = fopen(raw, "wb");
            if (f) { fwrite(fb, sizeof fb, 1, f); fclose(f); }
        }
        exit(0);
    }

    int mask = 0;
    for (int i = 0; i < sc_n; i++)
        if (poll_n >= sc[i].f0 && poll_n <= sc[i].f1)
            mask |= sc[i].mask;
    *p = mask & 0xFFFF;
    axes[0] = axes[1] = axes[2] = axes[3] = 0x80;
    if (mask & (1 << 16)) axes[2] = 0x00;      /* 左スティック ← */
    if (mask & (1 << 17)) axes[2] = 0xFF;      /* → */
    if (mask & (1 << 18)) axes[3] = 0x00;      /* ↑ */
    if (mask & (1 << 19)) axes[3] = 0xFF;      /* ↓ */
    if (mask & (1 << 20)) axes[0] = 0x00;      /* 右スティック ← */
    if (mask & (1 << 21)) axes[0] = 0xFF;      /* → */
    if (mask & (1 << 22)) axes[1] = 0x00;      /* ↑ */
    if (mask & (1 << 23)) axes[1] = 0xFF;      /* ↓ */
    return 1;
}
