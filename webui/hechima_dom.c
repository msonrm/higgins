/* hechima.h の口を、別の wasm モジュール（web/vendor/hechima-wasm）へ JS 越しに渡す。
 *
 * ★wasm/hechima_web.c の対。違いは **init も同期**なこと —— Web 版（DOM）は Asyncify を
 *   使わないので、辞書の読み込みは JS が先に済ませ、済んだら hg_conv_ready() を呼ぶ。
 *   ここの hechima_init は「JS が用意した Module.hechima があるか」を答えるだけ。
 * ★戻りの文字列は**こちらのヒープへ写す**（上流は「次の呼び出しまで有効」の約束）。
 */
#include <emscripten.h>
#include <stdlib.h>
#include "hechima.h"

int hechima_set_profile(const char *dir)
{
    (void)dir;
    return 0;
}

EM_JS(int, web_hechima_ok, (void), {
    return Module.hechima ? 0 : -1;
});

int hechima_init(const char *data_path)
{
    (void)data_path;
    return web_hechima_ok();
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

static char *last;

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
