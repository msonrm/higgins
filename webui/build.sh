#!/bin/sh
# Web 版（DOM で描く）を焼く。
#
#   sh webui/build.sh        → webui/out/core.{js,wasm}
#
# ★画面は焼かない —— FreeType / HarfBuzz / render.c / glyph_ft.c / フォントは入らない
#   （字の整形と折返しはブラウザがやる）。焼くのは打鍵の状態機械・合成器・分かち書き・
#   ELIZA・変換の繋ぎと、それを JS へ出す webui/core.c だけ。
# ★Asyncify も使わない（主ループは JS の rAF。辞書は JS が先に読む）。
set -e
cd "$(dirname "$0")"
HERE=$(pwd)
N=../native
OUT=out
PY="${PY:-python3}"

export EM_CACHE="${EM_CACHE:-$HOME/.cache/emscripten}"
export EM_FROZEN_CACHE=0
if [ ! -d "$EM_CACHE/sysroot" ] && [ -d /usr/share/emscripten/cache/sysroot ]; then
    mkdir -p "$(dirname "$EM_CACHE")"
    cp -a --reflink=auto /usr/share/emscripten/cache "$EM_CACHE"
fi

# ---- 1. 表と台本（native/build.sh と同じ生成器・同じ入力）----
SCRIPT="${SCRIPT:-../doctor/ja.txt}"
SCRIPT_EN="${SCRIPT_EN:-../vendor/eliza/scripts/ELIZA-script-DOCTOR-original-1966-CACM-appendix.txt}"
SCRIPT_KO="${SCRIPT_KO:-../doctor/ko.txt}"
SCRIPT_HI="${SCRIPT_HI:-../doctor/hi.txt}"
(
    cd "$N"
    $PY ../tools/gen_vocab.py "$SCRIPT" vocab.h
    $PY ../tools/gen_vocab.py "$SCRIPT_KO" vocab_ko.h VOCAB_KO
    $PY ../tools/gen_josa.py josa_tables.h tsunagi_ko_tables.h
    $PY gen_korean.py
    $PY gen_devanagari.py
    cp "$SCRIPT" script.txt
    cp "$SCRIPT_EN" script_en.txt
    cp "$SCRIPT_KO" script_ko.txt
    cp "$SCRIPT_HI" script_hi.txt
) > /dev/null

rm -rf "$OUT"
mkdir -p "$OUT/obj"

for s in script script_en script_ko script_hi; do
    sz=$(wc -c < "$N/$s.txt")
    cat > "$OUT/obj/$s.s" <<EOF
	.section .rodata.$s,"",@
	.globl _binary_${s}_txt_start
_binary_${s}_txt_start:
	.incbin "$HERE/$N/$s.txt"
	.size _binary_${s}_txt_start, $sz
	.globl _binary_${s}_txt_end
_binary_${s}_txt_end:
	.size _binary_${s}_txt_end, 0
EOF
done

# ---- 2. 焼く ----
CFLAGS="${CFLAGS:--O2 -Wall -Wno-unused-variable -Wno-unused-const-variable} -I$N"
CXXFLAGS="${CXXFLAGS:--std=c++20 -O1 -w} -I$N"

OBJS=""
for f in text.c wakachi.c tsunagi.c josa.c tsunagi_ko.c hindi.c korean.c devanagari.c conv.c input.c; do
    o="$OUT/obj/${f%.c}.o"
    # shellcheck disable=SC2086
    emcc $CFLAGS -c "$N/$f" -o "$o"
    OBJS="$OBJS $o"
done
for f in core.c hechima_dom.c; do
    o="$OUT/obj/${f%.c}.o"
    # shellcheck disable=SC2086
    emcc $CFLAGS -c "$f" -o "$o"
    OBJS="$OBJS $o"
done
# shellcheck disable=SC2086
em++ $CXXFLAGS -c "$N/eliza_bridge.cpp" -o "$OUT/obj/eliza_bridge.o"
for s in script script_en script_ko script_hi; do
    emcc -c "$OUT/obj/$s.s" -o "$OUT/obj/$s.o"
    OBJS="$OBJS $OUT/obj/$s.o"
done

LINK="-O2 -sSTACK_SIZE=1048576 -sALLOW_MEMORY_GROWTH -sMODULARIZE -sEXPORT_NAME=HigginsCore \
  -sENVIRONMENT=web,node -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,UTF8ToString \
  -sDEFAULT_LIBRARY_FUNCS_TO_INCLUDE=\$stringToNewUTF8,\$UTF8ToString"
# shellcheck disable=SC2086
em++ -o "$OUT/core.js" $OBJS "$OUT/obj/eliza_bridge.o" $LINK

echo "OK: $OUT/core.js ($(du -h "$OUT/core.js" | cut -f1)) + core.wasm ($(du -h "$OUT/core.wasm" | cut -f1))"
