#!/bin/bash
# Higgins — ELIZA（Weizenbaum・1966）と日本語で話す。ゲームパッドだけで打てる。
# PortMaster の起動スクリプト（雛形は portmaster.games/packaging.html）。

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

source $controlfolder/control.txt
[ -f "$controlfolder/mod_${CFW_NAME}.txt" ] && source "$controlfolder/mod_${CFW_NAME}.txt"

get_controls

GAMEDIR="/$directory/ports/higgins"
CONFDIR="$GAMEDIR/conf"
mkdir -p "$CONFDIR"

cd "$GAMEDIR"
> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1

export XDG_DATA_HOME="$CONFDIR"
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
# ★変換エンジン（libhechima.so）は実行ファイルの隣。rpath でも解決できるが、
#   CFW によっては $ORIGIN が効かないことがあるので明示しておく。
export LD_LIBRARY_PATH="$GAMEDIR:$GAMEDIR/libs.${DEVICE_ARCH}:$LD_LIBRARY_PATH"

# ★同梱フォント。環境変数の名が ZENMAI_ なのは、字を描く層（glyph_ft.c）を
#   Zenmai から 1 バイトも変えずに vendoring しているため（上流と md5 で照合できる）。
export ZENMAI_FONT="$GAMEDIR/higgins.otf"
# ★2 本目（デーヴァナーガリー）。★無くても他の席は動く（豆腐になるのはヒンディー語だけ）。
export ZENMAI_FONT_DEVA="$GAMEDIR/higgins-deva.ttf"

# ★かな漢字変換の学習とユーザー辞書の置き場。**置き場所を決めるのは起動スクリプトの
#   仕事**で、ゲーム側は環境変数を見るだけ（Zenmai がセーブでやったのと同じ形）。
#   ポートのフォルダに置くので、SD カードごと持ち運べる。
export HIGGINS_PROFILE="$CONFDIR"

$ESUDO chmod +x "$GAMEDIR/higgins.${DEVICE_ARCH}"

# ★gptokeyb は使わない —— このポートは SDL のゲームコントローラを直接読む。
#   キー入力へ化かす層を挟むと、**同時押しで 1 字を決める**入力（左手＝子音行 +
#   右手＝母音）が壊れる。ここは移植の芯なので譲らない。
pm_platform_helper "$GAMEDIR/higgins.${DEVICE_ARCH}"
"$GAMEDIR/higgins.${DEVICE_ARCH}"

pm_finish
