#!/usr/bin/env python3
"""韓国語出力の繋ぎ —— josa.py の対。tsunagi.py の韓国語版。

★**日本語版と役割が違う。** 日本語の tsunagi は「日本語に隣接する空白を全部消す」
だったが、韓国語は**空白に意味がある**（띄어쓰기）ので全部は消せない。
消すのは josa.py が印（`·`）を付けた境目**だけ**。

★★そしてここが 조사 の**実現**の場でもある。入り口で代表形に寄せた
는・가・를・와・로 を、**直前の音節の받침を見て**実際の形に戻す:

  당신 + ·는 → 당신은     （받침 ㄴ あり）
  어머니 + ·는 → 어머니는  （받침なし）

★これはスクリプトを書くときにも効く —— テンプレート側に `·를` と書いておけば、
埋め戻された語の末尾に合わせて 을/를 が選ばれる。**書く側は交替を気にしなくていい。**
（日本語版の「テンプレートに助詞を足してはいけない」という制約が、韓国語では消える）
"""

import re
import sys

MARK = "·"

# 代表形 → (받침あり, 받침なし)。★出し側（josa.py の JOSA_CANON）と対。
JOSA_FORM = {
    "는": ("은", "는"),
    "가": ("이", "가"),
    "를": ("을", "를"),
    "와": ("과", "와"),
    "로": ("으로", "로"),
}

HANGUL_START, HANGUL_END = 0xAC00, 0xD7A3


def batchim(ch):
    """末尾の音節の종성 index。0 = 받침なし。ハングル以外は 0 とみなす。"""
    code = ord(ch)
    if not (HANGUL_START <= code <= HANGUL_END):
        return 0
    return (code - HANGUL_START) % 28


def realize(prev, josa):
    """代表形の 조사 を、直前の語に合わせた形にする。"""
    form = JOSA_FORM.get(josa)
    if not form:
        return josa
    b = batchim(prev[-1]) if prev else 0
    if josa == "로" and b == 8:      # ★ㄹ 받침 だけは「로」を取る（서울로）
        return "로"
    return form[0] if b else form[1]


def tsunagi_ko(s):
    # ★印（`·`）は**独立したトークン**。次の語を前の語にくっつける合図で、
    #   そこが「元は 1 語だった」ことを表す（josa.py の注記）。
    out, glue = [], False
    for tok in s.split(" "):
        if not tok:
            continue
        if tok == MARK:
            glue = True
            continue
        if glue and out:
            out[-1] += realize(out[-1], tok)
            glue = False
        else:
            out.append(tok)
            glue = False
    s = " ".join(out)
    # 引用符の内側の空白を落とす（“ 1 ” → “1”）
    s = re.sub("“ +", "“", s)
    s = re.sub(" +”", "”", s)
    # 句読点の前の空白（分かち書きで入ったもの）
    s = re.sub(r" +([,.])", r"\1", s)
    return s


def main():
    for line in sys.stdin:
        print(tsunagi_ko(line.rstrip("\n")))


if __name__ == "__main__":
    main()
