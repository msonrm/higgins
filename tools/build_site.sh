#!/usr/bin/env bash
# 公開用の dist/ を作る（Cloudflare Pages 等の静的ホストへそのまま置ける形）。
#
# ★web/ は開発用に doctor/ を ../doctor/ で参照している。dist/ では平坦化するので、
#   index.html は 404 を見て両方のパスを試す作りにしてある。
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIST="$ROOT/dist"

rm -rf "$DIST"
mkdir -p "$DIST/doctor"
cp "$ROOT/web/"*.html "$ROOT/web/"*.js "$DIST/"
cp -r "$ROOT/web/vendor" "$DIST/vendor"
cp "$ROOT/doctor/"*.txt "$DIST/doctor/"
cp "$ROOT/LICENSE" "$DIST/LICENSE.txt"
cp "$ROOT/vendor/eliza/LICENSE" "$DIST/vendor/ELIZA-LICENSE-CC0.txt"

# ★mozc.data は 18.9MB。Cloudflare Pages の 1 ファイル上限 25MB には収まる。
echo "dist/ を作った:"
du -sh "$DIST"
find "$DIST" -type f -size +10M -exec ls -lh {} \; | awk '{print "  大きいファイル: " $5, $9}'
