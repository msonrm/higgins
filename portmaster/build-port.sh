#!/bin/sh
# PortMaster へ出す zip を組む。
#
#   sh portmaster/build-port.sh              → portmaster/higgins.zip
#
# 中身は packaging.html の形:
#   higgins.zip
#   ├── Higgins.sh          起動スクリプト
#   ├── port.json           PortMaster の目録
#   ├── gameinfo.xml        ランチャの一覧に出る情報
#   ├── README.md           謝辞・操作表
#   ├── screenshot.png      遊んでいる画面（4:3・640×480 以上）
#   └── higgins/            GAMEDIR
#       ├── higgins.aarch64
#       ├── libhechima.so   変換エンジン（mozc）
#       ├── mozc.data       辞書 19MB
#       ├── higgins.otf     同梱フォント 13MB（日本語 + ハングル）
#       └── licenses/
#
# ★**glibc の版がそのまま動く範囲を決める**。ここで焼いたバイナリは焼いた機械の
#   glibc を要求する。dArkOS / dArkOSen 系（Debian trixie）は開発機と同じなので
#   そのまま動く。ArkOS / AmberELEC（Ubuntu 20.04 = glibc 2.31）は**動かない**
#   （出すなら古い glibc の入れ物で焼き直す。`--check` で今の要求版が出る）。
set -e
cd "$(dirname "$0")"
HERE=$(pwd)
ARCH="${ARCH:-$(uname -m)}"
OUT="${1:-$HERE/higgins.zip}"
GAME="$HERE/higgins"

if [ "$1" = "--check" ] || [ "$2" = "--check" ]; then
    BIN="$GAME/higgins.$ARCH"
    [ -f "$BIN" ] || { echo "先に組んでから: sh build-port.sh" >&2; exit 1; }
    echo "要求する glibc:"
    objdump -T "$BIN" | grep -o 'GLIBC_[0-9.]*' | sort -Vu | sed 's/^/  /'
    exit 0
fi

mkdir -p "$GAME/licenses"

# 1. 本体
( cd ../native && sh build.sh "$GAME/higgins.$ARCH" )

# 2. 変換エンジンと辞書。★実行ファイルの隣に置く（rpath と LD_LIBRARY_PATH の両方で
#    解決できる）。取り込みは tools/vendor_hechima_native.sh。
cp "../native/vendor/hechima/$ARCH/libhechima.so" "$GAME/libhechima.so"
cp "../native/vendor/hechima/mozc.data"           "$GAME/mozc.data"

# 3. 同梱フォント。★システムのフォントに頼らない（CJK を 1 本も持たない CFW がある）。
[ -f ../native/higgins.otf ] || {
    echo "同梱フォントが無い。先に作ってください: cd native && sh make_font.sh" >&2
    exit 1
}
cp ../native/higgins.otf "$GAME/higgins.otf"

# 4. ライセンス（★同梱物の出自を配布物の中に持たせる。README から辿れるだけでは足りない）
cp ../LICENSE                                   "$GAME/licenses/higgins-MIT.txt"
cp ../vendor/eliza/LICENSE                      "$GAME/licenses/eliza-CC0.txt"
cp "$HOME/development/zenmai/LICENSE"           "$GAME/licenses/zenmai-shell-MIT.txt"
cp "$HOME/development/fcitx5-mozc/mozc/LICENSE" "$GAME/licenses/mozc-BSD.txt"
cp "$HOME/development/fcitx5-mozc/LICENSE"      "$GAME/licenses/fcitx5-mozc-BSD.txt"
cp "$HOME/development/zenmai/native/vendor/noto-cjk/LICENSE" "$GAME/licenses/noto-cjk-OFL.txt"
cp ../vendor/VENDOR.md                          "$GAME/licenses/VENDOR.md"

# 5. スクリーンショット（gameinfo.xml が higgins/ の中を指しているので複製する）
#
# ★**実機で撮る必要はない**。SDL_VIDEODRIVER=offscreen で画面をそのまま書き出せる:
#     cd ../native && SDL_VIDEODRIVER=offscreen ZENMAI_SCRIPT=conv.script \
#       ZENMAI_STOP=750 ZENMAI_RAW=/tmp/s.raw ./higgins
#     python3 ../tools/raw2png.py /tmp/s.raw ../portmaster/screenshot.png
# ★先に消してから複製する。残しておくと、差し替え忘れたときに**前の版が黙って
#   zip に入り続ける**（Zenmai が 3 回踏んだ「古い成果物が残る」型）。
# ★★**画面を変える直しをしたら、スクショも撮り直す。**
# ★★**学習が空の状態で撮る。** 開発ツリーの native/conf には試打の履歴が溜まって
#   いて、同じ台本でも**出る候補が違う**（実測: 開発機では「私はさびしい」、
#   配布物の新品では「私は寂しい」）。遊ぶ人が最初に見る画面はこちら ——
#   つまり **portmaster/higgins/ で、conf を消してから撮る**のが正しい。
rm -f "$GAME/screenshot.png"
if [ -f screenshot.png ]; then cp screenshot.png "$GAME/screenshot.png"; fi

# 6. zip に詰める
rm -f "$OUT"
FILES="Higgins.sh port.json gameinfo.xml README.md higgins"
if [ -f screenshot.png ]; then FILES="$FILES screenshot.png"; fi
# shellcheck disable=SC2086
zip -r -q "$OUT" $FILES -x '*/log.txt' '*/conf/*'

echo "OK: $OUT ($(du -h "$OUT" | cut -f1))"
[ -f screenshot.png ] || echo "★ screenshot.png がまだ無い（PortMaster の PR には要る）"
