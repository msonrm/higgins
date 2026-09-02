#!/bin/sh
# 同梱するフォント（higgins.otf）を作る。
#
# ★**なぜシステムのフォントに頼らないか** —— R36H（dArkOSen）には Noto CJK が
#   入っていたが、★**PortMaster の全機種に入っているとは限らない**（CFW によっては
#   CJK フォントを 1 本も持たない）。同梱すれば機種に依存しない。
#
# ★**なぜ Zenmai のように小さく絞れないか** —— あちらは Zork の訳文に出る字だけ
#   （7,623 字 / 3.89MB）を焼けばよかった。Higgins は**かな漢字変換を積んでいるので、
#   mozc が出す字を先に知ることができない**。絞れば必ず穴が出る
#   （Zenmai が `—` U+2014 を落として「黙って空白で描かれた」のと同じ形）。
#
# ★そこで**日本語のブロックを丸ごと**取る。
#   ★線引きの根拠が説明できる形にしてある —— CJK 統合漢字の基本ブロック
#   （U+4E00-9FFF）全部・かな・記号・全角英数・互換漢字。
#   拡張 A / B は BMP の外か稀用字で、そもそも殻（uint16_t）が持てない。
#
# ★★**ハングルも同じ 1 本から出る**（2026-09-02・韓国語 DOCTOR の段階 2）——
#   Noto Sans CJK JP（font 0）が**音節 11,172 字を全部持っている**（実測。
#   ジャモ 256 / 互換ジャモ 94 も）。★**別のフォントは要らない = ライセンスも
#   OFL のまま増えない。** 韓国語で増えるのは**範囲の 3 行だけ**。
#
#   日本語だけ … 22,955 字 / 11.3MB
#   ＋ハングル … 34,477 字 / 13.2MB（**+1.86MB / +16%**）
#
#   ★辞書が 19MB なので、フォント 13.2MB は比率として釣り合う。
#   ★★**器は触らなくてよい** —— 22px でのハングルの ink は**高さ 22**（実測）で、
#     日本語の 24（`glyph_ft.c` の PROBE）より**低い**。24 行の器にそのまま入る。
#     （デーヴァナーガリーはここが崩れるので、器の設計から見直しになる）
#
# ★**GSUB/GPOS は落とさない**（Zenmai と同じ判断）。落とせるのは CJK だけで、
#   4 言語（デーヴァナーガリー）に進むと落とせなくなり、手順が言語ごとに分岐する。
#
# ★**`--name-IDs+=13,14` を必ず付ける** —— pyftsubset の既定は name の 13/14
#   （OFL の本文と URL）を捨てる。放っておくと**フォントから OFL の表記が消えたまま
#   配る**ことになる。Noto Sans CJK は OFL 1.1・予約フォント名の宣言なし。
#
#   sh make_font.sh [出力先]        （既定: higgins.otf）
#
# 前提: fonttools（pip install fonttools）、Noto Sans CJK。
# ★実体は追跡しない（.gitignore）。配布物を組むときに焼く。
set -e
cd "$(dirname "$0")"
OUT="${1:-higgins.otf}"
NOTO="${NOTO:-/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc}"
[ -f "$NOTO" ] || { echo "Noto Sans CJK が見つからない: $NOTO（NOTO= で指定できます）" >&2; exit 1; }

# ASCII / Latin-1 / ★ハングル字母 / 一般句読点 / 矢印 / 囲み数字 / 罫線 / 幾何学模様 /
# CJK 記号 / ひらがな / カタカナ / カタカナ拡張 / ★互換ジャモ / 囲み CJK /
# CJK 統合漢字 / 互換漢字 / CJK 互換形 / ★ハングル音節 / 半角全角形
#
# ★ハングルは 3 つ:
#   U+1100-11FF  ジャモ（組み合わせ用）—— 合成前の字母。★NFD で入ってきたとき用の保険
#   U+3130-318F  互換ジャモ（ㄱ ㅏ …）—— **入力中の未確定表示**に要る
#   U+AC00-D7A3  音節（가〜힣）—— 本体。11,172 字
UNI="U+0020-007E,U+00A0-00FF,U+1100-11FF,U+2000-206F,U+2190-21FF,U+2460-24FF,\
U+2500-257F,U+25A0-25FF,U+3000-303F,U+3040-309F,U+30A0-30FF,U+31F0-31FF,\
U+3130-318F,U+3200-33FF,U+4E00-9FFF,U+AC00-D7A3,U+F900-FAFF,U+FE30-FE4F,\
U+FF00-FFEF"

pyftsubset "$NOTO" --font-number=0 --unicodes="$UNI" --name-IDs+=13,14 --output-file="$OUT"

python3 - "$OUT" <<'PY'
import sys
from fontTools.ttLib import TTFont
f = TTFont(sys.argv[1])
n13 = f["name"].getDebugName(13)
print(f"OK: {sys.argv[1]}  {len(f.getBestCmap())} 字")
print(f"    ライセンス表記: {'あり' if n13 else '★無い（--name-IDs+= を確認）'}")
PY

# ---- 2 本目: デーヴァナーガリー（ヒンディー語）----
#
# ★★**1 本にまとめられない。** Noto Sans CJK は **CFF**、Noto Sans Devanagari は
#   **glyf** で、1 つのフォントに両方の outline 形式は入らない（実測）。
#   ★韓国語が「範囲を 3 行足すだけ」で済んだのと**決定的に違う**ところで、
#   `glyph_ft.c` に 2 本目の面を開く機構が要るのはこのため。
#
# ★**subset しない。** Devanagari ブロックだけに絞っても **244KB → 209KB** で
#   ほとんど減らない —— 中身の大半が**合字と配置の表**（GSUB/GPOS）で、そこは
#   落とせない（落とすと母音記号が動かず結合子音も出ず、★読めない字になる）。
#   減らないものに手順を増やす価値が無いので、取ってきたものをそのまま置く。
#
# ★**システムからは拾えない** —— 開発機に Devanagari フォントは 1 本も無い（実測）。
#   ★版を md5 で pin する。フォントが変われば**字形も送りも変わる**ので、
#   `shots.sh` の突き合わせが理由なく赤くなる。
DEVA_OUT="${2:-higgins-deva.ttf}"
DEVA_URL="https://raw.githubusercontent.com/notofonts/notofonts.github.io/main/fonts/NotoSansDevanagari/hinted/ttf/NotoSansDevanagari-Regular.ttf"
DEVA_MD5="f4ae6809bd8c31573370e8da72514012"   # Version 2.006 / 244,284 bytes

if [ -f "$DEVA_OUT" ] && [ "$(md5sum "$DEVA_OUT" | cut -d' ' -f1)" = "$DEVA_MD5" ]; then
    echo "OK: $DEVA_OUT（既にある・md5 一致）"
else
    echo "取ってくる: $DEVA_URL"
    curl -sfL -o "$DEVA_OUT.tmp" "$DEVA_URL" || { echo "★取得に失敗" >&2; exit 1; }
    got=$(md5sum "$DEVA_OUT.tmp" | cut -d' ' -f1)
    if [ "$got" != "$DEVA_MD5" ]; then
        rm -f "$DEVA_OUT.tmp"
        echo "★md5 が違う（期待 $DEVA_MD5 / 実際 $got）—— 上流で版が上がった可能性。" >&2
        echo "  絵が変わるので、確かめてから DEVA_MD5 を更新すること。" >&2
        exit 1
    fi
    mv "$DEVA_OUT.tmp" "$DEVA_OUT"
    echo "OK: $DEVA_OUT（$(stat -c%s "$DEVA_OUT") bytes）"
fi
