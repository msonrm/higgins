#!/usr/bin/env python3
"""ELIZA スクリプトから「切り出せなければならない語」を集めて native/vocab.h に焼く。

    python3 tools/gen_vocab.py doctor/ja.txt native/vocab.h
    python3 tools/gen_vocab.py doctor/ko.txt native/vocab_ko.h VOCAB_KO

★**語彙を集める規則は tools/wakachi.py（collect_vocab）が唯一の正典**で、
  ここはその出力を C の表にするだけ —— 規則そのものを C へ移植すると、
  Python・JS・C の 3 か所に同じ規則を持つことになり、必ずどれかが腐る
  （Higgins は既に wakachi を Python と JS の 2 か所に持っている）。
  ★C 側へ移すのは**最長一致だけ**（native/wakachi.c・30 行ほど）。

★**毎回焼く**（native/build.sh が呼ぶ）。生成物を追跡すると、スクリプトを直したのに
  古い語彙のまま動く、という形でいつか必ず踏む。
"""

import io
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from eliza_script import parse, tokenize
from wakachi import collect_vocab


def main():
    if len(sys.argv) not in (3, 4):
        sys.exit(__doc__)
    script_path, out_path = sys.argv[1], sys.argv[2]
    # ★言語ごとに別の表を焼くので、名前は引数で受ける（既定 = 日本語の VOCAB）。
    name = sys.argv[3] if len(sys.argv) == 4 else "VOCAB"

    forms = parse(tokenize(io.open(script_path, encoding="utf-8").read()))
    # ★forms[0] は挨拶（hello_message）。照合には使われないので語彙に入れない
    #   —— wakachi.py の main と同じ切り方。
    vocab = sorted(collect_vocab(forms[1:]))

    # ★BMP の外は殻の uint16_t に入らない。落とさず気づけるように止める
    #   （静かに消えると「打ったのに切れない」の原因が語彙側だと分からなくなる）。
    for w in vocab:
        for ch in w:
            if ord(ch) > 0xFFFF:
                sys.exit(f"gen_vocab: BMP 外の字が語彙にある: {w!r} ({ch!r})")

    pool, items = [], []
    for w in vocab:
        items.append((len(pool), len(w)))
        pool.extend(ord(c) for c in w)

    maxlen = max((len(w) for w in vocab), default=1)
    lines = [
        f"/* tools/gen_vocab.py が {os.path.basename(script_path)} から生成。手で編集しない。",
        " * 語彙を集める規則の正典は tools/wakachi.py の collect_vocab。 */",
        f"#ifndef HG_{name}_H",
        f"#define HG_{name}_H",
        "",
        f"static const unsigned short {name}_POOL[] = {{",
    ]
    for i in range(0, len(pool), 12):
        lines.append("    " + " ".join(f"0x{c:04X}," for c in pool[i:i + 12]))
    lines += [
        "};",
        "",
        f"static const struct {{ unsigned short off, len; }} {name}[] = {{",
    ]
    for off, ln in items:
        lines.append(f"    {{ {off}, {ln} }},")
    lines += [
        "};",
        "",
        f"enum {{ {name}_N = {len(items)}, {name}_MAXLEN = {maxlen} }};",
        "",
        "#endif",
        "",
    ]
    io.open(out_path, "w", encoding="utf-8").write("\n".join(lines))
    print(f"OK: {out_path} ({len(items)} 語 / 最長 {maxlen} 字 / プール {len(pool)})")


if __name__ == "__main__":
    main()
