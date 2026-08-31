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
# ★そこで**日本語のブロックを丸ごと**取る = 22,955 字 / 11.3MB。
#   ★線引きの根拠が説明できる形にしてある —— CJK 統合漢字の基本ブロック
#   （U+4E00-9FFF）全部・かな・記号・全角英数・互換漢字。
#   拡張 A / B は BMP の外か稀用字で、そもそも殻（uint16_t）が持てない。
#   ★辞書が 19MB なので、フォント 11.3MB は比率として釣り合う。
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

# ASCII / Latin-1 / 一般句読点 / 矢印 / 囲み数字 / 罫線 / 幾何学模様 /
# CJK 記号 / ひらがな / カタカナ / カタカナ拡張 / 囲み CJK / CJK 統合漢字 /
# 互換漢字 / CJK 互換形 / 半角全角形
UNI="U+0020-007E,U+00A0-00FF,U+2000-206F,U+2190-21FF,U+2460-24FF,U+2500-257F,\
U+25A0-25FF,U+3000-303F,U+3040-309F,U+30A0-30FF,U+31F0-31FF,U+3200-33FF,\
U+4E00-9FFF,U+F900-FAFF,U+FE30-FE4F,U+FF00-FFEF"

pyftsubset "$NOTO" --font-number=0 --unicodes="$UNI" --name-IDs+=13,14 --output-file="$OUT"

python3 - "$OUT" <<'PY'
import sys
from fontTools.ttLib import TTFont
f = TTFont(sys.argv[1])
n13 = f["name"].getDebugName(13)
print(f"OK: {sys.argv[1]}  {len(f.getBestCmap())} 字")
print(f"    ライセンス表記: {'あり' if n13 else '★無い（--name-IDs+= を確認）'}")
PY
