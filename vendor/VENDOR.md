# 同梱しているもの / Vendored code

| ディレクトリ | 何 | ライセンス |
|---|---|---|
| `eliza/` | Anthony Hay による ELIZA の再実装（C++ 単一ファイル + JS 版 + スクリプト集） | **CC0 1.0** |

## `eliza/`

- upstream: https://github.com/anthay/ELIZA
- 取り込み: **2026-08-28**（`--depth 1` clone → `.git` を落として vendoring）
- ライセンス: **CC0 1.0**（`eliza/LICENSE` に全文）

### ★中の DOCTOR スクリプトの出自

ここが**この企画の主張が立つかどうかの根拠**なので、辿れる形で残しておく。

| もの | 出どころ | ライセンス |
|---|---|---|
| ELIZA 原典（MAD-SLIP ソース + DOCTOR） | 2021 年に Jeff Shrager と MIT アーキビスト Myles Crowley が Weizenbaum papers から発見 | ★**CC0**（Weizenbaum 遺族が許諾） |
| 欠けていた SLIP ライブラリ 約 4% の再実装 | Shrager ら | CC0 |
| 1966 CACM 論文 付録のスクリプト | Anthony Hay が転記（2020-12） | CC0 として頒布 |
| フランス語 DOCTOR | 2025-04 パリ Musée du Jeu de Paume 展のため。Shrager + ChatGPT の初訳を CNRS の Barnabé Sauvage が校正 | CC0 として頒布 |

### ★upstream から落としたもの（2026-08-28）

`eliza/doc/` の画像 **8 枚（約 2MB）は取り込んでいない**。

| 落としたもの | 理由 |
|---|---|
| `Audrey_Hepburn_as_Eliza_Doolittle_1963.jpg` | 映画スチル。撮影者／映画会社に権利がある |
| `1979AlfredNussbaum.jpg` / `1972-04-24_Cosell_oh_zero.jpg` | 第三者の写真・書簡 |
| `JW_MIT_archive_Slip_HASH_function.jpg` | MIT アーカイブ資料の複製 |
| `Hollerith_Char_Set_IBM709.jpg` / `Hollerith_Char_Set_Michigan.jpg` | IBM の資料 |
| `action-shot.jpg` / `action-shot.png` | ★これは Anthony Hay 自身のもので CC0。ただし実装に不要で 1.1MB あるので同じく落とした |

★**upstream の CC0 宣言は著者自身の著作物には及ぶが、これらには及ばない。**
実装は `src/` だけで完結していて doc の画像は 1 枚も使わないので、
**再配布しないものは持たない**方を採った。`doc/*.md` の画像リンクは切れるが、
原本は upstream で見られる。テキスト（`Eliza_part_*.md`・`ELIZA_transcription*`）は
著者自身の文章と原典の転記なので残してある。
`.gitignore` にも門を作ってあるので、再 clone しても入ってこない。


## `web/vendor/hechima/` ほか —— hechima（日本語入力）

Web 版で日本語を打つための一式。**自作部分 MIT / 変換エンジンは powered by Mozc**。

- ★**版の正典は hechima repo の `site/public/vendor/VENDOR.md`**。ここはその pin であって、
  版の説明を二重に書かない（同じ制約を 2 か所に書くと片方が必ず腐る）
- ★**手でコピーしない**。差し替えるときも `tools/vendor_hechima.sh` を通す
- 取り込んだ実体の md5 を下に焼いてあるので、上流と照合できる
  （hechima ではデプロイ伝播ラグを `?cb=` で見抜けず md5 照合が要った実績がある）

取得元: `~/development/hechima/site/public/vendor/`（`msonrm/hechima` = public）
取り込み: 2026-08-28

| ファイル | サイズ | md5 |
|---|---|---|
| `hechima/hechima.js` | 36K | `6edca13c85c8d0ee1809a5646c521afd` |
| `hechima/hechima-worker.js` | 16K | `a5daac0e70ea5f18349c6e7848980c4b` |
| `hechima/hechima.d.ts` | 24K | `97a32687842588c785af07eaa0bcb119` |
| `hechima-wasm/hechima-wasm.js` | 80K | `193697d14172ac60945550c40c214548` |
| `hechima-wasm/hechima-wasm.wasm` | 2.6M | `7b56ad80cc5d605f44e684d7b80a0f52` |
| `hechima-wasm/mozc.data` | 19M | `c40baaba995e6f4bb3fcf5df420eec04` |
| `keymap-engine/keymap-engine.js` | 148K | `747cc09b1f7faa6034b869c0da96e07d` |
| `keymaps/romaji.json` | 4.0K | `3830e768f85c841dc1b7f8ac540a7d33` |

`mozc.data` は Mozc の辞書。**名前と帰属を保つため改名しない**（上流の指示）。

### ★使わないもの

- **Harold Garfinkel Archive の実会話記録** —— ★**CC0 ではない**（Anne Warfield Rawls が
  intellectual executor として権利を保有）。elizagen.org 側に置かれているが**出自が違う**ので、
  このリポジトリには一切取り込まない。

> Zenmai で `ps1/glyphs.h` が **MIT ではなく OFL** だったのと同じ形の事故 ——
> 「抽出した資産は元のライセンスを引きずる」—— を避けるため、
> **取り込む前にここへ 1 行足す**ことを手順にする。


## `native/` —— Zenmai の殻（PortMaster 版の土台）

キーボードの無い aarch64 Linux ハンドヘルドで動かすための土台。
★**機械に触る 9 関数（`plat.h`）と、字の 5 関数（`glyph.h`）の後ろだけが機種依存**で、
組版・折返し・禁則・履歴・表示窓（`render.c`）と、かな入力の状態機械（`input.c`）は
その上に**無改造で**載る。

- ★**殻の本家は Zenmai**（`msonrm/zenmai` の `native/`）。ここはその pin
- ★**手でコピーしない**。差し替えるときも `tools/vendor_shell.sh` を通す
- ★なぜ Zenmai が本家か —— 殻の契約を決めているのは **PS1 の制約**（freestanding /
  libc 無し / FPU 無し / 2MB RAM）で、それを検査できるのは Zenmai だけだから
  （`sim.py` の画素一致・`gen_mock.py`・台本ゴールデン）。
  ★**きつい方が共有コードを持たないと、ゆるい方（Linux）で malloc を気軽に使う・
  float を使う・文字コードを u32 にする、が黙って通ってしまう。**

取り込み: **2026-08-31** / 上流の版 = `dac2483`（Merge pull request #37 from msonrm/docs/release-procedure）

| ファイル | 行 | md5 |
|---|---|---|
| `plat.h` | 54 | `2cd7b40f18bbbd9a0a9677c51a179da3` |
| `plat_sdl.c` | 416 | `bb3a88cea38bea54512a2bf051fc8ff1` |
| `glyph.h` | 44 | `68a51b1474546a5b2fb59a212eac104b` |
| `glyph_ft.c` | 325 | `9b6d37cdd872943c909e4ff995b03bd2` |
| `render.c` | 587 | `3fee80fdc143b482c0cd113f10670544` |
| `render.h` | 68 | `e2d8f2a44ab99396b5e185556bbe356f` |
| `input.c` | 265 | `89d274c52b74fd6b3d675910cfad74f0` |
| `input.h` | 58 | `50a52faa74060f9d16c7e3be4909cdce` |
| `tables.h` | 104 | `27832972b1202422b273f02eb3aa5013` |
| `card.h` | 14 | `7a05b47530c3eec9a5c86d185c33b051` |
| `save_file.c` | 72 | `6f99dbca6385eb3c09ace1ecbf3a8473` |
| `test_glyph.c` | 201 | `a039ec0e701ab7897ee6f49ccae6b7f1` |

★`tables.h` は**取り込みリストに無かったのを足した**（`input.c:3` が include している
かな表 10×5 と T9 表）。生成物の側を pin する —— labo から生成し直す道を持つと、
Higgins と Zenmai で表がずれても誰も気づけない。

★`save_file.c` / `card.h` / `test_glyph.c` は**まだリンクしていない**
（セーブは段階 E で決める。フォントの被覆検査は同梱フォントを作るときに使う）。

### Higgins が自分で書いたもの（殻ではない）

| ファイル | 何 |
|---|---|
| `main.c` | 起動と対話ループ（★下の「写した範囲」） |
| `text.c` | UTF-8 ⇄ `uint16_t`。★Zenmai は変換がビルド時に済んでいたので新規 |
| `wakachi.c` | 最長一致だけ（語彙は `tools/gen_vocab.py` が焼く。規則の正典は `tools/wakachi.py`） |
| `tsunagi.c` | 日本語に隣接する空白を消す |
| `eliza_bridge.cpp` | ELIZA（C++20）を C から呼ぶ 3 本。★upstream は 1 行も変えない |
| `content.h` / `content_stub.c` | ★**殻の穴埋め**（`render.c` の `draw_logical()` が Zenmai の本文データを参照するため）。本筋は上流で切り出すこと |
| `test_reply.c` | 検査用のホストビルド（SDL を開かない） |
| `build.sh` | 焼く。語彙とスクリプトも毎回焼き直す |

### ★Zenmai の `main.c` から写した範囲（2026-08-31）

**vendoring ではなく写し**（`main.c` は Zork の話なので取り込みリストに入れていない）。
上流が直っても伝わらないので、ここに記録する。
★**二度目に同じ形が出たら、Zenmai 側で切り出して殻に加えること。**

| 写したもの | 元（zenmai/native/main.c） | 変えたところ |
|---|---|---|
| `paint_screen` | 57 | 状態行が無いので帯を汎用に |
| `comp_insert` / `comp_bs` | 383-418 | `CMD_MAX` 36 → `COMP_MAX` 96（Zork のコマンドではなく**自由文**を打つため） |
| `pad_row` / `pad_vowel` | 490-508 | そのまま |
| `frame_of` / `carry_over_pad` | 510-554 | 英語面が無いので `lt` 引数を落とした |
| `interactive_ja` の骨 | 1235-1462 | ★下の「マッピングの補正」。Zork 固有（`cmd_run` / 別案 / 聞き返し / 状態行 / ルビ）は落とした |

### ★マッピングの補正 —— GIME 準拠（スティック前提）

★**Zenmai は PS1 を想定していたので、アナログスティック無しでもひらがなが打てる
代替路を持っている。** Higgins は**かな漢字変換をやる以上スティックが要る**
（変換操作を置く場所が他に無い。スティックの無い機種は同時に RAM 128〜256MB で、
mozc の辞書 18.9MB がそもそも載らない）ので、**道を 1 本にした**。
★**二重の道を残したまま段階 D で片方を剥がすと、「どちらでも打てる」状態が
検査を素通りする。**

| 操作 | Zenmai（代替路つき） | Higgins（GIME 準拠） |
|---|---|---|
| 濁点・半濁点 | Select **または** 右スティック↑ | 右スティック↑ **だけ** |
| BS | R2 長押し 24 フレーム **または** 右スティック← | 右スティック← **だけ** |
| 長音「ー」 | 右スティック→ | 同じ |
| 確定 | Start / L3 / 右スティック↓ | ★**L3 だけ**（右スティック↓は段階 D の候補選択へ空けた） |
| 打った行を捨てる | R3 | 同じ |
| 履歴を遡る | 左スティック↑↓（常時）+ L2+↑↓ | ★左スティック↑↓ を**打っていないときだけ**（打っている間の縦は段階 D の変換 ―― ↓ 変換開始 / ↑ 前候補 ―― に譲る） |
| Start | 打っていないとき = オプション画面 | やめる（★段階 E でオプションに置き換える） |
| 英語 T9 の記号面 | `zork_sym()` で `.` `,` `-` 空白 に差し替え | ★**写さない**。`tables.h` の素の表が `? ( ) !` を持っており、自由文にはそちらが要る |

### ★入力欄は殻を使わず自前で描く（2026-08-31）

Zenmai の `draw_strip` / `build_strip` は **1 行**（`CMD_H = 24`）。
★**Zork のコマンドは 1 行に収まるが、相談文は収まらない** —— DOCTOR に打つのは
命令ではなく文なので、前提そのものが違う。

- `main.c` の `draw_input` が **3 行**の欄を自前で描く。使うのは `glyph.h` の
  `draw24` / `glyph_w` と `plat.h` の `gp0_upload` だけ ＝ **境界はそのまま**
- ★**殻には手を入れない。** Zenmai に要らない機能を `render.c` へ入れると、
  PS1 側が払えない重さが共有コードに紛れ込む
- 折返しは幅で切るだけ（禁則なし）。★本文の折返し（`push_char` のぶら下げ・道連れ）は
  **履歴に積む経路**なので入力欄には使えない
- 溢れたらキャレットを追って窓が流れる。★**上に隠れている行があるときは ＞ を薄くする**
  —— 遡れることが分かる最小の印（3 行しかないので、字の流れだけでは気づけない）
- ★字の右端は印の箱に **3px の隙間**を空けて止める。Zenmai の 1 行版は `W-MARGIN-24`
  まで描いていて箱と重なりうるが、あちらは**入力側でも幅を絞っていた**ので表に出なかった
- `comp_insert` の上限は**字数だけ**（96 字）にした。幅で止める理由が無くなったため
- 台本 `native/long.script` が回帰を守る（80 字打って 4 行にし、左スティック← で
  先頭まで戻す。★`ZENMAI_STOP` の値で場面が決まる = 5000 が末尾・5700 が先頭）
