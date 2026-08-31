/* ELIZA（vendor/eliza/src/eliza.cpp・C++20）を C から呼ぶための口。
 *
 * ★出す口はこの 3 本だけ。ELIZA の型（std::string / rulemap / shared_ptr）は
 *   境界の向こうに置いたままにする。
 */
#ifndef HG_ELIZA_BRIDGE_H
#define HG_ELIZA_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

/* スクリプト（UTF-8・S 式）を読んで ELIZA を起こす。1 = 成功 */
int hg_eliza_init(const char *script_utf8, int len);

/* 挨拶（hello_message）。UTF-8。 */
const char *hg_eliza_greeting(void);

/* 応答。入力は**分かち書き済み**の UTF-8（wakachi を通したもの）。 */
const char *hg_eliza_response(const char *utf8);

#ifdef __cplusplus
}
#endif

#endif
