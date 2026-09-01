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

/* スクリプト（UTF-8・S 式）を読んで ELIZA を起こす。1 = 成功
 *
 * use_nomatch_msgs: 組み込みのエラーメッセージ（`PLEASE CONTINUE` / `HMMM` /
 *   `GO ON , PLEASE` / `I SEE`）を使うか。★**英語の文字列がエンジンに焼かれている**ので、
 *   日本語スクリプトでは 0 にしてスクリプト側の `NONE` に落とす。
 *   ★1966 年の原典を載せるときは 1（既定）—— upstream いわく「Weizenbaum の ELIZA は
 *     組み込みメッセージを使っていた」ので、そこを変えると忠実さの主張が崩れる。
 *   ★どちらにせよ**発火する時点でスクリプトの誤り**（規則の飛び先が無い / 分解が
 *     合わない / newkey が尽きた）。0 にするのは日本語が出るようにするだけで、穴は塞がらない。 */
int hg_eliza_init(const char *script_utf8, int len, int use_nomatch_msgs);

/* 挨拶（hello_message）。UTF-8。 */
const char *hg_eliza_greeting(void);

/* 応答。入力は**分かち書き済み**の UTF-8（wakachi を通したもの）。 */
const char *hg_eliza_response(const char *utf8);

#ifdef __cplusplus
}
#endif

#endif
