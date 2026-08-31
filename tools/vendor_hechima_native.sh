#!/usr/bin/env bash
# labo の hechima-wasm から **Linux ネイティブの**変換エンジンを取り込む。
#
# ★web 版（tools/vendor_hechima.sh）の対。あちらは wasm + JS、こちらは .so + 辞書。
#   ラッパーのソースは同じ 1 本（hechima_wasm.cc）で、違うのはツールチェインだけ。
#
#   tools/vendor_hechima_native.sh [dist-linux のパス]
#
# ★**実体は追跡しない**（.gitignore）。libhechima.so 5.7MB + mozc.data 19MB は
#   リポジトリに入れるには大きく、再現手順（labo の link_linux.sh）がある。
#   Zenmai が同梱フォントの実体を追跡しないのと同じ判断。md5 は下に出るので、
#   vendor/VENDOR.md に貼れば上流と照合できる。
#
# ★**segment.db / boundary.db / cform.db は取らない** —— あれは mozc が
#   user profile に**実行時に作る学習ファイル**で、配布物ではない
#   （labo の dist-linux に落ちているのは hechima_cli を動かした副産物）。
set -euo pipefail
SRC="${1:-$HOME/development/logical-layout-labo/hechima-wasm/dist-linux}"
ARCH=$(uname -m)
DST="$(cd "$(dirname "$0")/.." && pwd)/native/vendor/hechima"

[ -f "$SRC/$ARCH/libhechima.so" ] || {
    echo "libhechima.so が見つからない: $SRC/$ARCH" >&2
    echo "  labo で焼く: cd hechima-wasm && MOZC_SRC=... MOZC_BUILD=... bash link_linux.sh" >&2
    exit 1
}
[ -f "$SRC/$ARCH/mozc.data" ] || { echo "mozc.data が無い: $SRC/$ARCH" >&2; exit 1; }

mkdir -p "$DST/$ARCH"
cp "$SRC/$ARCH/libhechima.so" "$DST/$ARCH/libhechima.so"
# ★mozc.data はアーキ非依存なので 1 つだけ持つ（辞書はバイトオーダ非依存の形式）
cp "$SRC/$ARCH/mozc.data" "$DST/mozc.data"

echo "| ファイル | サイズ | md5 |"
echo "|---|---|---|"
for f in "$ARCH/libhechima.so" "mozc.data"; do
    printf '| `%s` | %s | `%s` |\n' \
        "$f" "$(du -h "$DST/$f" | cut -f1)" "$(md5sum "$DST/$f" | cut -d' ' -f1)"
done
echo
echo "★公開シンボル: $(nm -D --defined-only "$DST/$ARCH/libhechima.so" | grep -c ' T hechima_') 個"
