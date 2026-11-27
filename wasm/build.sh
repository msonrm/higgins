#!/bin/sh
# ブラウザ版（パッド専用）を焼く —— native と**同じ C を** Emscripten で wasm にする。
#
#   sh wasm/build.sh            → wasm/out/higgins.{js,wasm}
#
# ★**ブラウザ用に書き直した層は無い。** 差し替えたのは機械に触る 2 本だけ:
#   plat_sdl.c → plat_web.c（canvas + Gamepad API）/ libhechima.so → hechima_web.c
#   （別モジュールの hechima-wasm を JS 越しに呼ぶ）。main.c・input.c・render.c・conv.c・
#   ELIZA は native とまったく同じソース —— ★labo の JS エンジンを使わないのは、
#   上流に無い直し（받침のキャンセル・RT の印・シフトの巡回・デーヴァナーガリー）を
#   **全部踏み直すことになる**から（TODO「上流で 1 か所だった判断が、移植で割れて落ちる」）。
#
# ★Debian の emcc はキャッシュが凍結されていて FreeType / HarfBuzz のポートを作れない。
#   既定ではユーザー側のキャッシュ（~/.cache/emscripten）を使い、無ければシステムのものを写す
#   （btrfs なら reflink でほぼ容量を食わない）。初回だけポートの生成で数分かかる。
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

# ★台本は `_binary_<名前>_start / _end` の名前で埋め込む —— native の objcopy と同じ名前に
#   するので main.c を触らずに済む。wasm には objcopy が無いので `.incbin` で書く。
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
PORTS="-sUSE_FREETYPE=1 -sUSE_HARFBUZZ=1"
CFLAGS="${CFLAGS:--O2 -Wall} -DZM_SDL -DZM_BASE=30 -DZM_HARFBUZZ -I$N $PORTS"
CXXFLAGS="${CXXFLAGS:--std=c++20 -O1 -w} -I$N"

SRC_N="main.c text.c wakachi.c tsunagi.c josa.c tsunagi_ko.c hindi.c korean.c devanagari.c conv.c content_stub.c render.c glyph_ft.c input.c"
OBJS=""
for f in $SRC_N; do
    o="$OUT/obj/${f%.c}.o"
    # ★main.c の main は higgins_main の名前で焼く —— 返ったら exit() する薄い main を
    #   plat_web.c が持つ（そちらの「終わり方」の注記）
    extra=""
    [ "$f" = main.c ] && extra="-Dmain=higgins_main"
    # shellcheck disable=SC2086
    emcc $CFLAGS $extra -c "$N/$f" -o "$o"
    OBJS="$OBJS $o"
done
for f in plat_web.c hechima_web.c; do
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

# ★Asyncify = wait_fields の中でブラウザへ返す（plat_web.c の頭書き）。
# ★INVOKE_RUN=0 —— 呼ぶ側（main.js / shots.mjs）がフォントを仮想 FS に置いてから callMain する。
# ★スタックは広めに取る（ELIZA の照合は再帰する。既定の 64KB では足りない恐れ）。
LINK="-O2 $PORTS -sASYNCIFY -sASYNCIFY_STACK_SIZE=65536 -sSTACK_SIZE=1048576 \
  -sALLOW_MEMORY_GROWTH -sMODULARIZE -sEXPORT_NAME=HigginsModule -sENVIRONMENT=web,node \
  -sINVOKE_RUN=0 -sEXIT_RUNTIME=1 \
  -sEXPORTED_RUNTIME_METHODS=FS,callMain,ENV \
  -sDEFAULT_LIBRARY_FUNCS_TO_INCLUDE=\$stringToNewUTF8,\$UTF8ToString"
# shellcheck disable=SC2086
em++ -o "$OUT/higgins.js" $OBJS "$OUT/obj/eliza_bridge.o" $LINK

echo "OK: $OUT/higgins.js ($(du -h "$OUT/higgins.js" | cut -f1)) + higgins.wasm ($(du -h "$OUT/higgins.wasm" | cut -f1))"
