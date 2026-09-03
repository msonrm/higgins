/* Higgins の C↔C++ 境界。
 *
 * ★出す口はこの 3 本だけ。ELIZA の型（std::string / rulemap / shared_ptr）は
 *   境界の向こうに置いたままにする。
 *
 * ★**相手（スクリプト）を席の数だけ持つ**（2026-09-01）。日本語 DOCTOR / 1966 年 CACM の
 *   原典 DOCTOR / 韓国語 DOCTOR / ヒンディー語。★記憶（mem_rule）も規則も相手ごとに
 *   別なので、**会話が席の数だけ並走する** —— 言語を切り替えるのは「相手を替える」
 *   ことであって、同じ相手が別の言語を話すわけではない。
 */
#ifndef HG_ELIZA_BRIDGE_H
#define HG_ELIZA_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

/* ★HG_HI（ヒンディー語）は `doctor/hi.txt`（2026-09-03 に訳した）。
   ★入口は他の 3 席と違う —— danda `।` を `.` に写さないと ELIZA の節の切り出しが
   働かない（native/hindi.h の注記）。**出口は無い**（tsunagi が素通しする）。 */
enum { HG_JA = 0, HG_EN = 1, HG_KO = 2, HG_HI = 3, HG_SLOTS = 4 };

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
