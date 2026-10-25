#!/bin/sh
# 公開用の wasm/dist/ を組む（Cloudflare Pages 等の静的ホストへそのまま置ける形）。
#
#   sh wasm/build.sh && sh wasm/dist.sh      → wasm/dist/
#
# ★開発用の参照（`../native/` `../web/vendor/hechima-wasm/` `out/`）を **1 階層に畳んで**
#   書き換える（Zenmai の build.mjs と同じ。dist/ の中だけで閉じる）。
# ★辞書（19MB）とフォント（13MB）は **`.gz` を並べて置く** —— 拡張子から content-type が
#   決まらず CDN の自動圧縮が効かない（hechima で実測）。main.js は `.gz` を先に試し、
#   無ければ素のほうを取る。★素のほうも置く（DecompressionStream の無いブラウザのため）。
set -e
cd "$(dirname "$0")"
D=dist
[ -f out/higgins.wasm ] || { echo "先に焼く: sh wasm/build.sh" >&2; exit 1; }
[ -f ../native/higgins.otf ] && [ -f ../native/higgins-deva.ttf ] || {
    echo "同梱フォントが無い: cd native && sh make_font.sh" >&2; exit 1; }

rm -rf "$D"
mkdir -p "$D/licenses"

fold() { sed -e 's#\.\./native/##g' -e 's#\.\./web/vendor/hechima-wasm/##g' -e 's#"out/#"#g'; }
fold < index.html > "$D/index.html"
fold < main.js > "$D/main.js"
cp out/higgins.js out/higgins.wasm "$D/"
cp ../native/higgins.otf ../native/higgins-deva.ttf "$D/"
cp ../web/vendor/hechima-wasm/hechima-wasm.js ../web/vendor/hechima-wasm/hechima-wasm.wasm \
   ../web/vendor/hechima-wasm/mozc.data "$D/"
gzip -9 -k "$D/mozc.data" "$D/higgins.otf"

# ★畳み残しがあると、公開先でだけ 404 になる（開発機では ../ が解決してしまう）
if grep -n '\.\./' "$D/index.html" "$D/main.js"; then
    echo "★畳み残しがある（上の行）" >&2; exit 1
fi

# ---- ライセンス ----
# ★PortMaster 版（portmaster/build-port.sh）と同じ一式 ＋ **wasm に焼き込んだもの**。
#   native は FreeType / HarfBuzz を実機から借りるが、wasm は静的に抱えるので配る義務がこちらに来る。
L="$D/licenses"
cp ../LICENSE                                   "$L/higgins-MIT.txt"
cp ../vendor/eliza/LICENSE                      "$L/eliza-CC0.txt"
cp "$HOME/development/zenmai/LICENSE"           "$L/zenmai-shell-MIT.txt"
cp "$HOME/development/fcitx5-mozc/mozc/LICENSE" "$L/mozc-BSD.txt"
cp "$HOME/development/fcitx5-mozc/LICENSE"      "$L/fcitx5-mozc-BSD.txt"
cp "$HOME/development/zenmai/native/vendor/noto-cjk/LICENSE" "$L/noto-cjk-OFL.txt"
cp ../vendor/noto-devanagari/LICENSE            "$L/noto-devanagari-OFL.txt"
cp ../vendor/VENDOR.md                          "$L/VENDOR.md"
cp ../web/vendor/THIRD_PARTY_NOTICES.md         "$L/hechima-THIRD_PARTY_NOTICES.md"
P="${EM_CACHE:-$HOME/.cache/emscripten}/ports"
cp "$P"/freetype/freetype-VER-*/docs/FTL.TXT    "$L/freetype-FTL.txt"
cp "$P"/harfbuzz/harfbuzz-*/COPYING             "$L/harfbuzz-MIT.txt"
cp "$P"/zlib/zlib-*/LICENSE                     "$L/zlib.txt"
# ★libc（musl）・libc++ など、Emscripten がリンクしたシステムライブラリの出自は
#   Debian の copyright が 1 本にまとめている
cp /usr/share/doc/emscripten/copyright          "$L/emscripten-runtime-copyright.txt"

# ★静的ホストはディレクトリの一覧を出さないので、目次を置く
{
    echo '<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">'
    echo '<title>Higgins licenses</title>'
    echo '<style>body{font-family:sans-serif;max-width:40em;margin:2em auto;padding:0 16px;line-height:1.8}</style>'
    echo '<h1>Higgins — ライセンス</h1>'
    echo '<p>Portions of this software are copyright © The FreeType Project (www.freetype.org). All rights reserved.</p>'
    echo '<p>かな漢字変換は powered by Mozc。</p><ul>'
    for f in "$L"/*; do
        n=$(basename "$f")
        [ "$n" = index.html ] || echo "<li><a href=\"$n\">$n</a></li>"
    done
    echo '</ul><p><a href="../">← もどる</a></p>'
} > "$L/index.html"

echo "dist/ を組んだ:"
du -sh "$D"
find "$D" -type f -size +10M -exec ls -lh {} \; | awk '{print "  大きいファイル: " $5, $9}'
