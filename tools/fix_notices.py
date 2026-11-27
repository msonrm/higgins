#!/usr/bin/env python3
"""hechima から写した THIRD_PARTY_NOTICES.md を、Higgins の中で読める形に直す（2026-11-28）。

★この文書は hechima リポジトリのもの（`tools/vendor_hechima.sh` がそのまま写す）なので、
  文中のパスが hechima の置き方（`site/public/vendor/…`・`LICENSE`・`BUILD_INFO.txt`）のままだった。
  Higgins の中では辿れないので、**Higgins での置き場**か **hechima の URL** に読み替え、頭に断りを付ける。
★手で直さない —— 写し直すたびにこれを通す（vendor_hechima.sh の最後で呼ぶ）。何度通しても同じ結果になる。

使い方: python3 tools/fix_notices.py <THIRD_PARTY_NOTICES.md> ...
"""
import sys

HECHIMA = 'https://github.com/msonrm/hechima/blob/main'
HIGGINS = 'https://github.com/msonrm/higgins/blob/main'
PREFACE = ('> この文書は [hechima](https://github.com/msonrm/hechima)（Higgins が日本語入力に使う部品）の通知を写したもの。\n'
           '> Higgins では、Mozc 由来の成果物は `vendor/hechima-wasm/` にある。\n\n')
SUBS = [
    ('`site/public/vendor/hechima-wasm/`', '`vendor/hechima-wasm/`'),
    ('[site/public/vendor/VENDOR.md](site/public/vendor/VENDOR.md) と\n同梱の BUILD_INFO.txt を参照）',
     f'[hechima の VENDOR.md]({HECHIMA}/site/public/vendor/VENDOR.md) を参照。\n'
     f'Higgins が取り込んだ実体の md5 は [vendor/VENDOR.md]({HIGGINS}/vendor/VENDOR.md)）'),
    ('のライセンスは [LICENSE](LICENSE)（MIT', f'のライセンスは [LICENSE]({HECHIMA}/LICENSE)（MIT'),
]

for path in sys.argv[1:]:
    s = open(path, encoding='utf-8').read()
    for old, new in SUBS:
        s = s.replace(old, new)
    if PREFACE not in s:
        # 見出し（1 行目）のすぐ下に断りを置く
        head, _, rest = s.partition('\n\n')
        s = head + '\n\n' + PREFACE + rest
    open(path, 'w', encoding='utf-8').write(s)
    left = [o for o, _ in SUBS if o in s] + (['site/public'] if 'site/public/vendor/hechima-wasm' in s else [])
    print(f'{path}: {"★読み替え残り " + repr(left) if left else "OK"}')
