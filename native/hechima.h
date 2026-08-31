/* libhechima（mozc）の C 口。
 *
 * ★**正典は labo の `hechima-wasm/hechima_wasm.cc` の extern "C" ブロック。**
 *   .so にヘッダは付いてこないので、使うぶんだけここに書き写している。
 *   ★C はリンク時に署名を検査しないので、上流で引数が変わると**黙って壊れる**。
 *   build.sh が `nm -D` で**名前の存在**だけは検査する（署名は動作で見るしかない）。
 *   ★いずれ labo 側にヘッダを置いて vendoring するのが筋。
 *
 * ★変換エンジンは wasm・Android・Linux で**同じラッパー 1 本**（hechima_wasm.cc）。
 *   違うのはツールチェインとリンク方法だけ。
 */
#ifndef HG_HECHIMA_H
#define HG_HECHIMA_H

#ifdef __cplusplus
extern "C" {
#endif

/* 学習ファイル（segment.db / boundary.db / cform.db）とユーザー辞書の置き場。
   ★hechima_init より**前**に呼ぶこと。0 = 成功 / -1 = 引数不正 */
int hechima_set_profile(const char *dir);

/* mozc.data を読んでエンジンを起こす。0 = 成功 / -1 = 辞書が読めない / -2 = 起動失敗 */
int hechima_init(const char *data_path);

/* かな → {"segments":[{"key":"..","candidates":[".."],"costs":[..]}]}
   ★エンジン側に状態が残る（続く resize はこの結果の上で動く）。"" = 失敗 */
const char *hechima_convert(const char *kana_utf8, int max_cands);

/* 文節を伸縮する（offset_length = ±1 など）。
   "" = 伸縮不能（先頭を縮め切った / 末尾を伸ばし切った）＝ 呼び元は現状維持 */
const char *hechima_resize(int segment_index, int offset_length, int max_cands);

/* 確定を学習させる。sizes_csv = 各文節のよみ字数、values_tsv = 選んだ値（タブ区切り）。
   0 = 成功。★既定の分節と同じなら境界は学習されない（上流が判定する） */
int hechima_learn(const char *kana_utf8, const char *sizes_csv, const char *values_tsv);

/* 直前の学習を取り消す */
int hechima_revert(void);

#ifdef __cplusplus
}
#endif

#endif
