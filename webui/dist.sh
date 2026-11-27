#!/bin/sh
# 公開用の webui/dist/ を組む（静的ホストへそのまま置ける形。ノート PC で配るなら
#   `python3 -m http.server -d webui/dist` → http://localhost:8000/ でよい）。
#
#   sh webui/build.sh && sh webui/dist.sh      → webui/dist/
#
# ★開発用の参照（`../native/` `../web/vendor/hechima-wasm/` `out/`）を 1 階層に畳む
#   （wasm/dist.sh と同じやり方）。
# ★辞書（19MB）とハングルの逃げ場のフォント（13MB）は `.gz` も並べる（main.js が先に試す）。
#   ★フォントは CSS の @font-face から読むので `.gz` は使われない —— 素のほうだけ置く。
set -e
cd "$(dirname "$0")"
D=dist
[ -f out/core.wasm ] || { echo "先に焼く: sh webui/build.sh" >&2; exit 1; }

rm -rf "$D"
mkdir -p "$D/licenses"

fold() { sed -e 's#\.\./native/##g' -e 's#\.\./web/vendor/hechima-wasm/##g' -e 's#"out/#"#g'; }
fold < index.html > "$D/index.html"
fold < main.js > "$D/main.js"
cp out/core.js out/core.wasm "$D/"
cp ../native/higgins.otf ../native/higgins-deva.ttf "$D/"
cp ../web/vendor/hechima-wasm/hechima-wasm.js ../web/vendor/hechima-wasm/hechima-wasm.wasm \
   ../web/vendor/hechima-wasm/mozc.data "$D/"
gzip -9 -k "$D/mozc.data"

if grep -n '\.\./' "$D/index.html" "$D/main.js"; then
    echo "★畳み残しがある（上の行）" >&2; exit 1
fi

# ---- ライセンス ----
# ★FreeType / HarfBuzz / zlib は**入っていない**（字はブラウザが描く）。
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
cp /usr/share/doc/emscripten/copyright          "$L/emscripten-runtime-copyright.txt"

{
    echo '<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">'
    echo '<title>Higgins licenses</title>'
    echo '<style>body{font-family:sans-serif;max-width:40em;margin:2em auto;padding:0 16px;line-height:1.8}</style>'
    echo '<h1>Higgins — ライセンス</h1>'
    echo '<p>かな漢字変換は powered by Mozc。</p><ul>'
    for f in "$L"/*; do
        n=$(basename "$f")
        [ "$n" = index.html ] || echo "<li><a href=\"$n\">$n</a></li>"
    done
    echo '</ul><p><a href="../">← もどる</a></p>'
} > "$L/index.html"

echo "dist/ を組んだ:"
du -sh "$D"
