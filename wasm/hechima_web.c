/* hechima.h の口を、ブラウザでは**別の wasm モジュール**（web/vendor/hechima-wasm）へ渡す。
 *
 * ★native は libhechima.so をリンクするが、wasm では 2 つのモジュールを 1 本に繋げない
 *   （mozc を焼き直すことになる）。だから**同じ C の口のまま**、中身だけ JS 越しに呼ぶ。
 *   ラッパーは wasm・Android・Linux で同じ 1 本（labo の hechima_wasm.cc）なので、
 *   呼び方も戻りの JSON も native と変わらない —— conv.c は 1 行も触らない。
 *
 * ★**同期で呼べる**のは hechima-wasm がスレッドを使わない作りだから（Worker を挟まない）。
 *   init だけは非同期（辞書 19MB を取ってくる）—— Asyncify で待つので、呼ぶ側からは
 *   native と同じ「返ってきたら使える」に見える。★そのあいだも起動画面は出ている
 *   （main.c が lang_menu を conv_init より先に置いているので）。
 *
 * ★戻りの文字列は**こちらのヒープへ写す**。上流は「次の呼び出しまで有効」の静的バッファを
 *   返す約束なので、直前のぶんを解放してから写せば同じ寿命になる。
 *
 * 呼び先は `Module.hechima`（ccall を持つモジュール）と `Module.hechimaInit()`
 * （初期化の Promise を返す）—— どちらも wasm/main.js（ブラウザ）と wasm/shots.mjs（node）が渡す。
 */
#include <emscripten.h>
#include <stdlib.h>
#include "hechima.h"

/* ★学習の置き場は hechima-wasm 側の仮想 FS（/tmp）に決まっていて、指す口が無い
   （wasm 版は hechima_set_profile を書き出していない）。受けるだけにする。 */
int hechima_set_profile(const char *dir)
{
    (void)dir;
    return 0;
}

EM_ASYNC_JS(int, web_hechima_init, (void), {
    if (typeof Module.hechimaInit !== 'function') return -2;
    try {
        return await Module.hechimaInit();
    } catch (e) {
        console.error('hechima の起動に失敗:', e);
        return -2;
    }
});

/* ★辞書の置き場はモジュールの側が決める（native のように道を渡しても読む FS が違う）。 */
int hechima_init(const char *data_path)
{
    (void)data_path;
    return web_hechima_init();
}

EM_JS(char *, web_hechima_resize, (int seg, int off, int max), {
    const H = Module.hechima;
    if (!H) return 0;
    const r = H.ccall('hechima_resize', 'string', ['number', 'number', 'number'], [seg, off, max]);
    return stringToNewUTF8(r == null ? "" : String(r));
});

EM_JS(char *, web_hechima_convert, (const char *kana, int max), {
    const H = Module.hechima;
    if (!H) return 0;
    const r = H.ccall('hechima_convert', 'string', ['string', 'number'], [UTF8ToString(kana), max]);
    return stringToNewUTF8(r == null ? "" : String(r));
});

EM_JS(int, web_hechima_learn, (const char *kana, const char *sizes, const char *values), {
    const H = Module.hechima;
    if (!H) return -1;
    return H.ccall('hechima_learn', 'number', ['string', 'string', 'string'],
                   [UTF8ToString(kana), UTF8ToString(sizes), UTF8ToString(values)]);
});

EM_JS(int, web_hechima_revert, (void), {
    const H = Module.hechima;
    if (!H) return -1;
    return H.ccall('hechima_revert', 'number', [], []);
});

static char *last;                     /* 直前に返した文字列（次の呼び出しまで有効） */

static const char *keep(char *s)
{
    free(last);
    last = s;
    return s ? s : "";
}

const char *hechima_convert(const char *kana_utf8, int max_cands)
{
    return keep(web_hechima_convert(kana_utf8, max_cands));
}

const char *hechima_resize(int segment_index, int offset_length, int max_cands)
{
    return keep(web_hechima_resize(segment_index, offset_length, max_cands));
}

int hechima_learn(const char *kana_utf8, const char *sizes_csv, const char *values_tsv)
{
    return web_hechima_learn(kana_utf8, sizes_csv, values_tsv);
}

int hechima_revert(void)
{
    return web_hechima_revert();
}
