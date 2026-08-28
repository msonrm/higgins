#!/usr/bin/env bash
# hechima の配布物を web/vendor/ へ取り込む。
#
# ★二重管理にしないための約束:
#   - 版の正典は **hechima repo の site/public/vendor/VENDOR.md**。ここはその pin。
#   - 手でコピーしない。差し替えるときも必ずこのスクリプトを通す。
#   - 取り込んだ実体の md5 を vendor/VENDOR.md に焼くので、
#     「どの版を持っているか」が上流と照合できる（デプロイ伝播ラグの実績あり）。
#
#   使い方: tools/vendor_hechima.sh [hechima の site/public/vendor のパス]

set -euo pipefail
SRC="${1:-$HOME/development/hechima/site/public/vendor}"
DST="$(cd "$(dirname "$0")/.." && pwd)/web/vendor"

[ -d "$SRC" ] || { echo "hechima の vendor が見つからない: $SRC" >&2; exit 1; }

# 取り込むもの（最小構成 + 配列エンジン。EMBEDDING.md の「3. 最小構成」に対応）
FILES=(
  "hechima/hechima.js"
  "hechima/hechima-worker.js"
  "hechima/hechima.d.ts"
  "hechima-wasm/hechima-wasm.js"
  "hechima-wasm/hechima-wasm.wasm"
  "hechima-wasm/mozc.data"
  "keymap-engine/keymap-engine.js"
  "keymaps/romaji.json"
)

for f in "${FILES[@]}"; do
  mkdir -p "$DST/$(dirname "$f")"
  cp "$SRC/$f" "$DST/$f"
done

# ★取り込んだ実体の md5 を出す。vendor/VENDOR.md の hechima 節に貼る。
echo "| ファイル | サイズ | md5 |"
echo "|---|---|---|"
for f in "${FILES[@]}"; do
  printf '| `%s` | %s | `%s` |\n' \
    "$f" \
    "$(du -h "$DST/$f" | cut -f1)" \
    "$(md5sum "$DST/$f" | cut -d' ' -f1)"
done
