#!/bin/sh
# 画面の回帰検査 —— 台本を offscreen で回し、フレームバッファの md5 を並べる。
#
# ★**Higgins には画素の台が無かった。** Zenmai の test-sdl.sh は「PS1 と SDL が
#   同じ絵を出すか」を突き合わせる台で、★Higgins に PS1 版は無いので流用できない
#   （字が FreeType 固定 ―― かな漢字変換を積むと出る漢字が無限になるため）。
#   描画に手を入れるときは、これで**前後の md5 を突き合わせる**。
#
# ★**ZENMAI_FONT を同梱フォントに向けている**のが要点 —— 指さないと開発機の
#   システムフォント（Noto CJK）に落ちる。あれはハングルも持っているので、
#   ★**同梱フォントに足し忘れても手元では正しく出てしまう**（実機だけ豆腐）。
#
#   sh shots.sh                      md5 の一覧を出す
#   sh shots.sh > base.txt           基準を保存する
#   sh shots.sh | diff base.txt -    変わった画面を見る
#
# ★★**基準（base.txt）はリポジトリに入れない。** md5 は FreeType の版と同梱フォントの
#   版で丸ごと変わるので、commit した基準は**別の機械で必ず赤くなる**（そして
#   「壊れた」と読み違える）。これは「触る前に自分で撮り、触ったあとで比べる」道具。
#   ★だから `make test` にも配線していない —— 検査ではなく**前後の突き合わせ**。
#
# ★停止フレームは**台本の頭のコメント**（`ZENMAI_STOP=700`）を読む。書いていない
#   台本は**最後のフレーム + 200** で止める ―― 書き忘れた台本を黙って落とすと、
#   そこの描画だけ誰も見ていないことになる。
set -e
cd "$(dirname "$0")"
[ -x ./higgins ] || { echo "./higgins が無い（sh build.sh を先に）" >&2; exit 1; }
[ -f higgins.otf ] || { echo "higgins.otf が無い（sh make_font.sh）" >&2; exit 1; }
OUT="${TMPDIR:-/tmp}/hg-shots"
mkdir -p "$OUT"
ng=0
for f in *.script; do
    stop=$(grep -o 'ZENMAI_STOP=[0-9]*' "$f" | head -1 | cut -d= -f2)
    if [ -z "$stop" ]; then
        last=$(sed 's/#.*//' "$f" | grep -oE '^[0-9]+-[0-9]+' | cut -d- -f2 | sort -n | tail -1)
        [ -n "$last" ] || { echo "$f: フレームが読めない" >&2; ng=1; continue; }
        stop=$((last + 200))
    fi
    rm -f "$OUT/$f.raw"
    SDL_VIDEODRIVER=offscreen ZENMAI_SCRIPT="$f" ZENMAI_STOP="$stop" \
        ZENMAI_RAW="$OUT/$f.raw" ZENMAI_FONT="$PWD/higgins.otf" \
        ./higgins > /dev/null 2>&1 || true
    if [ ! -s "$OUT/$f.raw" ]; then
        printf '%-22s ★絵が出なかった（stop=%s）\n' "$f" "$stop"
        ng=1
        continue
    fi
    printf '%-22s %s\n' "$f" "$(md5sum "$OUT/$f.raw" | cut -d' ' -f1)"
done
exit $ng
