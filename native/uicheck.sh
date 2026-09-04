#!/bin/sh
# UI の文言が画面に収まるかを、**全 UI 言語 × 全席**で確かめる。
#
# ★★**はみ出しは画面では端が切れるだけ**なので、絵を撮っていても気づけない ——
#   本体が stderr に 1 行出す作りにしてあるので（`help:` / `license:`）、それを拾う。
# ★★**要るのは「人」ではなく「別の入力源」**（`tools/coverage_*.py` と同じ形）——
#   人が実機で見て回ると、3 言語 × 4 席 × 数画面のうち踏むのは一部だけになる。
# ★フォントに依存する検査（幅はフォント次第）だが、**同梱フォントは md5 で pin**
#   してあるので、開発機と実機で同じ結果になる。
#
# ★★**shots.sh とは別物。** あちらは画素の md5 を人が突き合わせる道具（機械依存）。
#   こちらは**機械に依存しない**ので `make test` に配線してある。
# ★実機ではファイル名が違う（`higgins.aarch64`）ので、指せるようにしてある ——
#   SD カードは vfat でリンクが張れないため、名前で受けるのが唯一の道。
set -e
cd "$(dirname "$0")"
BIN="${HG_BIN:-./higgins}"
[ -x "$BIN" ] || { echo "$BIN が無い（sh build.sh を先に）" >&2; exit 1; }
[ -f higgins.otf ] || { echo "higgins.otf が無い（sh make_font.sh）" >&2; exit 1; }
OUT="${TMPDIR:-/tmp}/hg-uicheck"
rm -rf "$OUT"
mkdir -p "$OUT/conf"
ng=0
n=0
for lang in ja en ko; do
    # ★席ごとに 1 本ずつ（台本が席を選ぶ）＋ ライセンスの頁。
    for f in help.script help_en.script help_ko.script help_hi.script license.script; do
        stop=$(grep -o 'ZENMAI_STOP=[0-9]*' "$f" | head -1 | cut -d= -f2)
        rm -rf "$OUT/conf"
        mkdir -p "$OUT/conf"
        msg=$(SDL_VIDEODRIVER=offscreen ZENMAI_SCRIPT="$f" ZENMAI_STOP="$stop" \
              ZENMAI_FONT="$PWD/higgins.otf" ZENMAI_FONT_DEVA="$PWD/higgins-deva.ttf" \
              HIGGINS_PROFILE="$OUT/conf" HIGGINS_LANG="$lang" \
              "$BIN" 2>&1 >/dev/null | grep -E '^(help|license):' || true)
        n=$((n + 1))
        if [ -n "$msg" ]; then
            echo "$msg" | sed "s|^|  [$lang / $f] |"
            ng=1
        fi
    done
done
if [ "$ng" = 0 ]; then
    echo "OK: UI の文言は $n 通り（言語 × 画面）とも画面に収まっている"
else
    echo "FAIL: はみ出している文言がある（上の行）" >&2
fi
exit $ng
