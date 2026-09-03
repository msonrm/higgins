#!/usr/bin/env python3
"""josa.py / tsunagi_ko.py の表を C の表に焼く。

    python3 tools/gen_josa.py native/josa_tables.h native/tsunagi_ko_tables.h

★**規則の正典は tools/josa.py と tools/tsunagi_ko.py**で、ここはその表を C へ写すだけ。
  gen_vocab.py と同じ判断 —— 表を C に手で書くと、Python・JS・C の 3 か所に
  同じ規則を持つことになり、必ずどれかが腐る。
  ★C へ移すのは**手順（切り方・繋ぎ方）だけ**（native/josa.c / tsunagi_ko.c）。

★**毎回焼く**（native/build.sh が呼ぶ）。生成物は追跡しない。
"""

import io
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from josa import CONTRACTIONS, JOSA_CANON, JOSA_SET, TAILS
from tsunagi_ko import JOSA_FORM


def u16(s, width):
    cells = [f"0x{ord(c):04X}" for c in s] + ["0"] * (width - len(s))
    return "{" + ",".join(cells) + "}"


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    tails_path, forms_path = sys.argv[1], sys.argv[2]

    tmax = max(len(t) for t in TAILS)
    cmax = max(len(JOSA_CANON.get(t, t)) for t in TAILS)
    for src, (a, b) in CONTRACTIONS.items():
        if len(a) != 1 or len(b) != 1:
            sys.exit(f"gen_josa: 縮約の展開先が 1 音節でない: {src} → {a}/{b}")
    smax = max(len(s) for s in CONTRACTIONS)

    lines = [
        "/* tools/gen_josa.py が tools/josa.py から生成。手で編集しない。",
        " * 規則の正典は tools/josa.py（TAILS / JOSA_CANON / CONTRACTIONS）。 */",
        "#ifndef HG_JOSA_TABLES_H",
        "#define HG_JOSA_TABLES_H",
        "",
        "/* 語から剥がす 조사・어미。★**長い順**（josa.py が長さで並べている）。",
        "   s = 表層 / c = 代表形（交替する 조사 はここで寄せる）。 */",
        f"enum {{ KO_TAIL_MAX = {tmax}, KO_CANON_MAX = {cmax}, KO_TAIL_N = {len(TAILS)} }};",
        "/* ★`josa` = 조사 か（어미 なら 0）。剥がしてよいかの判定に要る ——",
        "   조사 は名詞に付くので、残りが 1 音節で辞書にも無いなら語の一部",
        "   （「사과」の「과」）。★어미 には掛けない（1 音節の語幹は普通）。 */",
        "static const struct { unsigned char len, clen, josa;",
        "                     unsigned short s[KO_TAIL_MAX], c[KO_CANON_MAX]; } KO_TAIL[] = {",
    ]
    for t in TAILS:
        c = JOSA_CANON.get(t, t)
        lines.append(f"    {{ {len(t)}, {len(c)}, {1 if t in JOSA_SET else 0}, "
                     f"{u16(t, tmax)}, {u16(c, cmax)} }},   /* {t} */")
    lines += [
        "};",
        "",
        "/* ★人称代名詞の不規則な縮約（제가 = 저 + 가）。**形態論の事実**なので",
        "   スクリプトではなくここで解く（josa.py の注記）。 */",
        f"enum {{ KO_CONTR_MAX = {smax}, KO_CONTR_N = {len(CONTRACTIONS)} }};",
        "static const struct { unsigned char len; unsigned short s[KO_CONTR_MAX], a, b; }"
        " KO_CONTR[] = {",
    ]
    for src, (a, b) in CONTRACTIONS.items():
        lines.append(f"    {{ {len(src)}, {u16(src, smax)}, 0x{ord(a):04X}, 0x{ord(b):04X} }},"
                     f"   /* {src} → {a} {b} */")
    lines += ["};", "", "#endif", ""]
    io.open(tails_path, "w", encoding="utf-8").write("\n".join(lines))

    lines = [
        "/* tools/gen_josa.py が tools/tsunagi_ko.py から生成。手で編集しない。",
        " * 規則の正典は tools/tsunagi_ko.py（JOSA_FORM）。 */",
        "#ifndef HG_TSUNAGI_KO_TABLES_H",
        "#define HG_TSUNAGI_KO_TABLES_H",
        "",
        "/* 代表形 → (받침あり, 받침なし)。★入り口で寄せた 조사 をここで実現する。 */",
        f"enum {{ KO_FORM_MAX = {max(len(v) for p in JOSA_FORM.values() for v in p)},"
        f" KO_FORM_N = {len(JOSA_FORM)} }};",
        "static const struct { unsigned short canon;",
        "                     unsigned char wlen, nlen;",
        "                     unsigned short w[KO_FORM_MAX], n[KO_FORM_MAX]; } KO_FORM[] = {",
    ]
    w_ = max(len(v) for p in JOSA_FORM.values() for v in p)
    for canon, (with_b, no_b) in JOSA_FORM.items():
        lines.append(f"    {{ 0x{ord(canon):04X}, {len(with_b)}, {len(no_b)}, "
                     f"{u16(with_b, w_)}, {u16(no_b, w_)} }},   /* {canon} → {with_b} / {no_b} */")
    lines += ["};", "", "#endif", ""]
    io.open(forms_path, "w", encoding="utf-8").write("\n".join(lines))

    print(f"OK: {tails_path} ({len(TAILS)} 語尾 / {len(CONTRACTIONS)} 縮約) "
          f"/ {forms_path} ({len(JOSA_FORM)} 交替)")


if __name__ == "__main__":
    main()
