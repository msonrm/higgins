#!/bin/sh
# Higgins 本体 → SDL2 の実行ファイル（PortMaster の aarch64 と、開発機のデスクトップ）。
# ついでに検査用のホストビルド（test_reply）も焼く。
#
# ★Zenmai の build-sdl.sh が下敷き。違いは:
#   - 字は **FreeType 固定**（glyph_ft.c）。Higgins に PS1 版は無いので、焼いた
#     ビットマップ版（PS1 との画素一致を確かめる検査用）は要らない。
#     ★そもそも、かな漢字変換を載せると**出る漢字が無限**になるので焼けない
#   - Zork のもの（story / cmd / translate / ruby / jp_text）が無い
#   - ELIZA（C++20）が入るので、リンクは C++ で行う
#
#   sh build.sh                   → ./higgins と ./test_reply
#   ZENMAI_WINDOW=1 ./higgins       窓で出す（既定は全画面）
set -e
cd "$(dirname "$0")"
OUT="${1:-higgins}"
CC="${CC:-cc}"
CXX="${CXX:-c++}"
PY="${PY:-python3}"
SCRIPT="${SCRIPT:-../doctor/ja.txt}"

# ---- 1. 語彙を焼く ----
# ★**毎回焼く**。生成物を残すと、スクリプトを直したのに切れ方が変わらない、という
#   形でいつか踏む（規則の正典は tools/wakachi.py の collect_vocab）。
$PY ../tools/gen_vocab.py "$SCRIPT" vocab.h

# ---- 2. スクリプトを実行ファイルに焼き込む ----
# ★ファイルを外に置くと「同梱してある」という主張が配布の形に依存してしまう
#   （Zenmai が story.bin でやったのと同じ手）。
# ★objcopy のシンボル名は**入力ファイル名から作られる**ので、必ずこの名前で置くこと
#   （_binary_script_txt_start / _end を test_reply.c と main.c が参照している）。
cp "$SCRIPT" script.txt
ARCH=$(uname -m)
case "$ARCH" in
    aarch64) OFMT="elf64-littleaarch64"; OBIN="aarch64" ;;
    x86_64)  OFMT="elf64-x86-64";        OBIN="i386:x86-64" ;;
    *) echo "未知のアーキテクチャ: $ARCH（objcopy の書式を足すこと）" >&2; exit 1 ;;
esac

# ★毎回消してから作る。古い .o が混ざると、消したはずのコードが**黙って**動き続ける。
OBJ=obj
rm -rf "$OBJ"
mkdir -p "$OBJ"
objcopy -I binary -O "$OFMT" -B "$OBIN" script.txt "$OBJ/script_txt.o"

# ---- 3. 焼く ----
CFLAGS="${CFLAGS:--O2 -Wall} -DZM_SDL -I. $(pkg-config --cflags sdl2 freetype2)"
# ★-w（警告抑制）は vendor の eliza.cpp だから。upstream を書き換えない方針なので、
#   -Wall を付けても直せない警告で出力が埋まるだけになる（Makefile と同じ判断）。
CXXFLAGS="${CXXFLAGS:--std=c++20 -O1 -w} -I."
LDFLAGS="$(pkg-config --libs sdl2 freetype2)"
# ★libstdc++ は静的に。配布先（PortMaster の各 CFW）に C++ ランタイムがあるとは
#   限らず、あっても版が古いことがある。glibc 自体は動的のまま（build-port.sh の注記）。
LDCXX="-static-libstdc++"

SRC_C="main.c text.c wakachi.c tsunagi.c content_stub.c render.c plat_sdl.c glyph_ft.c input.c"

OBJS=""
for f in $SRC_C; do
    o="$OBJ/${f%.c}.o"
    # shellcheck disable=SC2086
    $CC $CFLAGS -c "$f" -o "$o"
    OBJS="$OBJS $o"
done
# shellcheck disable=SC2086
$CXX $CXXFLAGS -c eliza_bridge.cpp -o "$OBJ/eliza_bridge.o"

# shellcheck disable=SC2086
$CXX -o "$OUT" $OBJS "$OBJ/eliza_bridge.o" "$OBJ/script_txt.o" $LDFLAGS $LDCXX

# ---- 4. 検査用のホストビルド ----
# ★**本体と同じ .o を使う**。別に焼き直すと「検査したもの」と「載っているもの」が
#   いつか食い違う（[[hechima-dual-path-hazard]] の形）。
# shellcheck disable=SC2086
$CC $CFLAGS -c test_reply.c -o "$OBJ/test_reply.o"
# shellcheck disable=SC2086
$CXX -o test_reply "$OBJ/test_reply.o" "$OBJ/text.o" "$OBJ/wakachi.o" "$OBJ/tsunagi.o" \
     "$OBJ/eliza_bridge.o" "$OBJ/script_txt.o" $LDCXX

echo "OK: $OUT ($(du -h "$OUT" | cut -f1)) / test_reply"
