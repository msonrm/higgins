/* Higgins の C↔C++ 境界。
 *
 * ★**upstream を 1 行も変えない。** 「Weizenbaum の 1966 年の ELIZA がそのまま動く」が
 *   この企画の主張なので、エンジンに手を入れた時点で主張が崩れる。だから `main` だけ
 *   マクロで潰して丸ごと include する —— Zenmai が MojoZork に対してやったのと同じ手。
 *
 * ★ここに翻訳も分かち書きも置かない。日本語の面倒（wakachi / tsunagi）は入口と出口に
 *   分けてあり、**エンジンから見ると空白区切りの言語が 1 つ増えただけ**に見える。
 */
#include <memory>
#include <sstream>
#include <string>

#define main eliza_main_unused
#include "../vendor/eliza/src/eliza.cpp"
#undef main

#include "eliza_bridge.h"

namespace {
elizascript::script g_script;
std::unique_ptr<elizalogic::eliza> g_eliza;
std::string g_out;                     /* 戻り値の寿命はここが持つ */
}

extern "C" int hg_eliza_init(const char *script_utf8, int len)
{
    try {
        std::string text(script_utf8, static_cast<size_t>(len));
        std::istringstream in(text);
        elizascript::read(in, g_script);
        g_eliza = std::make_unique<elizalogic::eliza>(g_script.rules, g_script.mem_rule);
        return 1;
    } catch (...) {
        return 0;
    }
}

extern "C" const char *hg_eliza_greeting(void)
{
    g_out = join(g_script.hello_message);
    return g_out.c_str();
}

extern "C" const char *hg_eliza_response(const char *utf8)
{
    if (!g_eliza)
        return "";
    try {
        g_out = g_eliza->response(utf8);
    } catch (...) {
        g_out.clear();
    }
    return g_out.c_str();
}
