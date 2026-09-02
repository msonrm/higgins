#!/usr/bin/env bash
# Zenmai の「殻」を native/ へ取り込む。
#
# ★殻の本家は **zenmai/native/**（https://github.com/msonrm/zenmai）。ここはその pin。
#
# ★なぜ Zenmai 側が本家か —— **殻の契約を決めているのは PS1 の制約**（freestanding /
#   libc 無し / FPU 無し / 2MB RAM）で、それを検査できるのは Zenmai だけだから:
#     sim.py         PS1 のシミュレータ。SDL 版との**画素一致**を確かめる台
#     gen_mock.py    割り付け規則の正典（render.c はその移植）
#     test-*.sh      台本ゴールデン（日本語 / 英語 / オプション / 引き継ぎ / セーブ / quit）
#   ★きつい方の制約が共有コードを持たないと、ゆるい方（Linux）で malloc を気軽に
#   使う・float を使う・文字コードを u32 にする、が**黙って通ってしまう**。
#
# ★**いずれ割れる**ことは織り込んである:
#     plat.* / glyph.* / input.* / card.h  … 割れない（境界そのもの）
#     render.c / render.h                  … ★割れる可能性が高い
#       - 禁則・プロポーショナル … PS1 でもやれるので**共有できる**
#       - 文字コードの u32 化    … Higgins が 4 言語をやるなら要るが、
#                                   ★PS1 は 2MB RAM なので払えないかもしれない
#   割るときは「PS1 が払えない」が合図。**黙って割れさせず、理由を書くこと。**
#
#   使い方: tools/vendor_shell.sh [zenmai の native のパス]

set -euo pipefail
SRC="${1:-$HOME/development/zenmai/native}"
DST="$(cd "$(dirname "$0")/.." && pwd)/native"

[ -d "$SRC" ] || { echo "zenmai の native が見つからない: $SRC" >&2; exit 1; }

# 取り込むもの。★main.c / cmd.c / translate.c / content*.c は **Zenmai 固有**なので
#   取らない（あれは Zork の話）。Higgins は自分の main を書いて殻に載せる。
#
# ★★**tables.h（かな表）も取らない。** input.c が include しているので一度は
#   取り込んだが、あれは**Zenmai が Zork 用に歪めた表**だった —— や行の「」を空枠に
#   潰し、わ行を わ/を/ん/ー に組み替え、ゔ を落としてある（gen_tables.py の
#   「PS1 適応」）。Higgins は自由文を打つのでどれも要らない。
#   ★**同じ正典から自分で焼く**（native/gen_tables.py）。
#   「上流の生成物」と「上流が加工した生成物」は違う ―― 実機で発覚（2026-08-31）。
#
# ★★**`input.c` / `input.h` は 2026-09-02 に割れた**（取り込みリストから外した）。
#   ★割れた理由は「PS1 が払えない」ではなく、**Higgins が別の言語の入力を足した**から ——
#   韓国語の合成（`step_korean` / `korAllReleased`）・英語の行インジケータ
#   （`gp_row_chars_en`）・シフトの巡回で、実測 **174 行ぶん**離れている
#   （`input.h` も 49 行）。★**Zork に韓国語入力は要らない**ので、上流へ返す筋のものでもない。
#
#   ★**放っておくと危なかった** —— vendoring は上書きなので、このスクリプトを
#   流すだけで**Higgins の韓国語入力が黙って消える**。「手でコピーしない」という
#   VENDOR.md の作法が、そのまま事故の引き金になっていた。
#   Zenmai 側の `input.c` に直したいことが出たら、**差分を見て手で運ぶ**こと。
FILES=(
  plat.h
  plat_sdl.c
  glyph.h
  glyph_ft.c
  render.c
  render.h
  card.h
  save_file.c
  test_glyph.c
)

mkdir -p "$DST"
for f in "${FILES[@]}"; do
  [ -f "$SRC/$f" ] || { echo "上流に無い: $f" >&2; exit 1; }
  cp "$SRC/$f" "$DST/$f"
done

# ★取り込んだ実体の md5 を出す。vendor/VENDOR.md の zenmai 節に貼る
#  （どの版を持っているかが上流と照合できる。hechima の vendoring と同じ作法）。
echo "| ファイル | 行 | md5 |"
echo "|---|---|---|"
for f in "${FILES[@]}"; do
  printf '| `%s` | %s | `%s` |\n' \
    "$f" \
    "$(grep -c '' "$DST/$f")" \
    "$(md5sum "$DST/$f" | cut -d' ' -f1)"
done

echo
echo "★上流の版:"
git -C "$(dirname "$SRC")" log --oneline -1 2>/dev/null | sed 's/^/    /' || echo "    (git 情報なし)"
