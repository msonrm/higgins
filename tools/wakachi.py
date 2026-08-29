#!/usr/bin/env python3
"""日本語の簡易分かち書き —— ELIZA スクリプト自身のキーワードを辞書にする。

ELIZA の split_user_input() は空白区切り前提で、4 言語のうち日本語だけが
それを満たさない。形態素解析器を持ち込む前に、★スクリプトが既に持っている語で
切ってみる:

  - キーワード・置換語・分解パターンのリテラル語・選択肢 `(*A B C)` を辞書にする
  - 入力を最長一致で分割し、辞書に無い連続部分は 1 トークンにまとめる

要点は ★**「キーワードは必ず切り出せる」ことが構造的に保証される**こと。
辞書外は 1 塊で残るので、埋め戻し（スロット）には元の字面がそのまま入る
（「学校が嫌いです」→「学校 が 嫌い です」→「なぜ 学校 が 嫌い な の です か」）。

★冪等: 既に分かち書きされた入力を通しても結果は変わらない。

段階 1 の足場であって最終形ではない。本番は hechima の文節境界を使う
（IME が変換時に文節を知っているので、分かち書きは副産物として手に入る）。
"""

import argparse
import io
import sys

from eliza_script import classify, parse, tokenize


def collect_vocab(forms):
    """照合に使われる語 = 切り出せなければならない語 を集める。"""
    vocab = set()

    def walk_pattern(pat):
        for el in pat:
            if isinstance(el, list):
                if el and isinstance(el[0], str) and el[0].startswith("*"):
                    vocab.add(el[0].lstrip("*"))
                    vocab.update(e for e in el[1:] if isinstance(e, str))
            elif el != "0":
                vocab.add(el)

    for form in forms:
        r = classify(form)
        if r["kind"] in ("MEMORY",):
            vocab.add(r["keyword"])
            for rule in r["rules"]:
                # ★`(0 母 0 = さっき …)` の = より右は再構成（出力側）なので辞書に入れない。
                #   照合に使われる語だけが「切り出せなければならない語」。
                walk_pattern(rule[:rule.index("=")] if "=" in rule else rule)
            continue
        if r["keyword"] != "NONE":
            vocab.add(r["keyword"])
        if r.get("subst"):
            vocab.add(r["subst"])
        for tr in r.get("transforms", []):
            if tr and isinstance(tr[0], list):
                walk_pattern(tr[0])

    # 数字・記号は語ではない
    return {w for w in vocab if w and not w.isdigit() and w not in ("=", "/")}


# ★ELIZA は "," "." "BUT" を delimiter として節を切り、キーワードのある節だけを使う
#   （eliza.cpp のコンストラクタでハードコード。スクリプトからは変えられない）。
#   日本語の句読点を半角に写せば**エンジン無改造でその仕組みに乗る** ——
#   引用が節単位になって短くなるという副次効果もある。
PUNCT = {"、": " , ", "，": " , ", "。": " . ", "．": " . ",
         "！": " . ", "？": " . ", "!": " . ", "?": " . "}


def normalize_punct(text):
    for k, v in PUNCT.items():
        text = text.replace(k, v)
    return text


def wakachi(text, vocab, maxlen):
    text = normalize_punct(text)
    out, unknown = [], []

    def flush():
        if unknown:
            out.append("".join(unknown))
            unknown.clear()

    i = 0
    while i < len(text):
        if text[i].isspace():          # 既にある区切りは尊重する（冪等性）
            flush()
            i += 1
            continue
        hit = None
        for length in range(min(maxlen, len(text) - i), 0, -1):
            if text[i:i + length] in vocab:
                hit = text[i:i + length]
                break
        if hit:
            flush()
            out.append(hit)
            i += len(hit)
        else:
            unknown.append(text[i])
            i += 1
    flush()
    return " ".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("script", help="辞書を取り出す ELIZA スクリプト")
    ap.add_argument("--show-vocab", action="store_true", help="辞書を出して終わる")
    args = ap.parse_args()

    forms = parse(tokenize(io.open(args.script, encoding="utf-8").read()))
    vocab = collect_vocab(forms[1:])
    maxlen = max((len(w) for w in vocab), default=1)

    if args.show_vocab:
        print(f"{len(vocab)} 語 (最長 {maxlen}):")
        print("  " + " ".join(sorted(vocab)))
        return

    for line in sys.stdin:
        print(wakachi(line.rstrip("\n"), vocab, maxlen))


if __name__ == "__main__":
    main()
