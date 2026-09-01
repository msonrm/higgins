/* Higgins の C↔C++ 境界。
 *
 * ★出す口はこの 3 本だけ。ELIZA の型（std::string / rulemap / shared_ptr）は
 *   境界の向こうに置いたままにする。
 *
 * ★**相手（スクリプト）を 2 つ持つ**（2026-09-01）。日本語 DOCTOR と、1966 年 CACM の
 *   原典 DOCTOR。★記憶（mem_rule）も規則も相手ごとに別なので、**会話が 2 本並走する**
 *   —— 言語を切り替えるのは「相手を替える」ことであって、同じ相手が別の言語を話す
 *   わけではない。
 */
#ifndef HG_ELIZA_BRIDGE_H
#define HG_ELIZA_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

enum { HG_JA = 0, HG_EN = 1, HG_SLOTS = 2 };

/* スクリプト（UTF-8・S 式）を読んで、その席の ELIZA を起こす。1 = 成功
 *
 * use_nomatch_msgs: 組み込みのエラーメッセージ（`PLEASE CONTINUE` / `HMMM` /
 *   `GO ON , PLEASE` / `I SEE`）を使うか。★**英語の文字列がエンジンに焼かれている**ので、
 *   日本語スクリプトでは 0 にしてスクリプト側の `NONE` に落とす。
 *   ★1966 年の原典には 1（既定）—— upstream いわく「Weizenbaum の ELIZA は
 *     組み込みメッセージを使っていた」ので、そこを変えると忠実さの主張が崩れる。
 *   ★どちらにせよ**発火する時点でスクリプトの誤り**（規則の飛び先が無い / 分解が
 *     合わない / newkey が尽きた）。0 にするのは日本語が出るようにするだけで、穴は塞がらない。 */
int hg_eliza_init(int slot, const char *script_utf8, int len, int use_nomatch_msgs);

/* 挨拶（hello_message）。UTF-8。 */
const char *hg_eliza_greeting(int slot);

/* 応答。日本語の席には**分かち書き済み**の UTF-8（wakachi を通したもの）を渡す。
   英語の席にはそのまま渡してよい（ELIZA は空白で切るので）。 */
const char *hg_eliza_response(int slot, const char *utf8);

#ifdef __cplusplus
}
#endif

#endif
