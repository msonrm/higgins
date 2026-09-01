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
/* ★席ごとに**スクリプトと記憶の両方**を持つ。eliza は rules への参照を握るので、
   script を席の外（共有）に置くと、もう一方を読み直した瞬間に足元が消える。 */
struct Seat {
    elizascript::script script;
    std::unique_ptr<elizalogic::eliza> eliza;
};
Seat g_seat[HG_SLOTS];
std::string g_out;                     /* 戻り値の寿命はここが持つ */

bool bad(int slot) { return slot < 0 || slot >= HG_SLOTS; }
}

extern "C" int hg_eliza_init(int slot, const char *script_utf8, int len, int use_nomatch_msgs)
{
    if (bad(slot))
        return 0;
    try {
        Seat &s = g_seat[slot];
        std::string text(script_utf8, static_cast<size_t>(len));
        std::istringstream in(text);
        elizascript::read(in, s.script);
        s.eliza = std::make_unique<elizalogic::eliza>(s.script.rules, s.script.mem_rule);
        s.eliza->set_use_nomatch_msgs(use_nomatch_msgs != 0);
        return 1;
    } catch (...) {
        return 0;
    }
}

extern "C" const char *hg_eliza_greeting(int slot)
{
    if (bad(slot))
        return "";
    g_out = join(g_seat[slot].script.hello_message);
    return g_out.c_str();
}

extern "C" const char *hg_eliza_response(int slot, const char *utf8)
{
    if (bad(slot) || !g_seat[slot].eliza)
        return "";
    try {
        g_out = g_seat[slot].eliza->response(utf8);
    } catch (...) {
        g_out.clear();
    }
    return g_out.c_str();
}
