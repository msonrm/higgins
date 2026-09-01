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
# ★英語の相手は **1966 年 CACM の原典そのもの**（vendor の付録を無加工で焼く）。
#   訳でも移植でもないので、ここが「原典がそのまま動く」という主張の実物になる。
SCRIPT_EN="${SCRIPT_EN:-../vendor/eliza/scripts/ELIZA-script-DOCTOR-original-1966-CACM-appendix.txt}"

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
cp "$SCRIPT_EN" script_en.txt
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
objcopy -I binary -O "$OFMT" -B "$OBIN" script_en.txt "$OBJ/script_en_txt.o"

# ---- 3. 焼く ----
CFLAGS="${CFLAGS:--O2 -Wall} -DZM_SDL -I. $(pkg-config --cflags sdl2 freetype2)"
# ★-w（警告抑制）は vendor の eliza.cpp だから。upstream を書き換えない方針なので、
#   -Wall を付けても直せない警告で出力が埋まるだけになる（Makefile と同じ判断）。
CXXFLAGS="${CXXFLAGS:--std=c++20 -O1 -w} -I."
LDFLAGS="$(pkg-config --libs sdl2 freetype2)"
# ★libstdc++ は静的に。配布先（PortMaster の各 CFW）に C++ ランタイムがあるとは
#   限らず、あっても版が古いことがある。glibc 自体は動的のまま（build-port.sh の注記）。
LDCXX="-static-libstdc++"

# ---- 変換エンジン（mozc / libhechima）----
# ★**必須**にしてある。変換なしの経路を残すと二重の道になり、片方でしか通らない値が
#   必ず出る。辞書が読めないときの分岐は**実行時に 1 か所**だけ（conv_ready）。
#   取り込み: tools/vendor_hechima_native.sh
HECHIMA_DIR="vendor/hechima"
HECHIMA_LIB="$HECHIMA_DIR/$ARCH/libhechima.so"
[ -f "$HECHIMA_LIB" ] || {
    echo "libhechima.so が無い: $HECHIMA_LIB" >&2
    echo "  取り込む: bash ../tools/vendor_hechima_native.sh" >&2
    exit 1
}
# ★名前の存在だけは検査する —— C はリンク時に署名を見ないので、上流で引数が
#   変わっても黙って通る（native/hechima.h の注記）。名前が消えたらここで止まる。
for sym in hechima_set_profile hechima_init hechima_convert hechima_resize hechima_learn; do
    nm -D --defined-only "$HECHIMA_LIB" | grep -q " T $sym$" \
        || { echo "libhechima に $sym が無い（版が古い？）" >&2; exit 1; }
done
# ★rpath は $ORIGIN 起点。開発機ではそのまま動き、PortMaster では
#   起動スクリプトが LD_LIBRARY_PATH を通す（どちらでも解決できる）。
# ★配布では実行ファイルの隣、開発機では vendor/ の下。どちらでも解決できるよう両方。
LDFLAGS="$LDFLAGS -L$HECHIMA_DIR/$ARCH -lhechima -Wl,-rpath,\$ORIGIN:\$ORIGIN/$HECHIMA_DIR/$ARCH"

SRC_C="main.c text.c wakachi.c tsunagi.c conv.c content_stub.c render.c plat_sdl.c glyph_ft.c input.c"

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
$CXX -o "$OUT" $OBJS "$OBJ/eliza_bridge.o" "$OBJ/script_txt.o" "$OBJ/script_en_txt.o" \
     $LDFLAGS $LDCXX

# ---- 4. 検査用のホストビルド ----
# ★**本体と同じ .o を使う**。別に焼き直すと「検査したもの」と「載っているもの」が
#   いつか食い違う（[[hechima-dual-path-hazard]] の形）。
# shellcheck disable=SC2086
$CC $CFLAGS -c test_reply.c -o "$OBJ/test_reply.o"
# shellcheck disable=SC2086
$CXX -o test_reply "$OBJ/test_reply.o" "$OBJ/text.o" "$OBJ/wakachi.o" "$OBJ/tsunagi.o" \
     "$OBJ/eliza_bridge.o" "$OBJ/script_txt.o" "$OBJ/script_en_txt.o" $LDCXX

# 変換だけを SDL 抜きで走らせる小道具（★UI の前に変換の成立を見るため）
# shellcheck disable=SC2086
$CC $CFLAGS -c test_conv.c -o "$OBJ/test_conv.o"
# shellcheck disable=SC2086
$CC -o test_conv "$OBJ/test_conv.o" "$OBJ/conv.o" "$OBJ/text.o" \
    -L$HECHIMA_DIR/$ARCH -lhechima -Wl,-rpath,\$ORIGIN:\$ORIGIN/$HECHIMA_DIR/$ARCH

echo "OK: $OUT ($(du -h "$OUT" | cut -f1)) / test_reply"
