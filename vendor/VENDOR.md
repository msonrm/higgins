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

### ★使わないもの

- **Harold Garfinkel Archive の実会話記録** —— ★**CC0 ではない**（Anne Warfield Rawls が
  intellectual executor として権利を保有）。elizagen.org 側に置かれているが**出自が違う**ので、
  このリポジトリには一切取り込まない。

> Zenmai で `ps1/glyphs.h` が **MIT ではなく OFL** だったのと同じ形の事故 ——
> 「抽出した資産は元のライセンスを引きずる」—— を避けるため、
> **取り込む前にここへ 1 行足す**ことを手順にする。
