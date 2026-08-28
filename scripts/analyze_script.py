#!/usr/bin/env python3
"""DOCTOR スクリプトの構造を機械的に測る。

手で読むとバイアスが入るので数える。日本語版を書いたあと同じものをかけて、
英語版と突き合わせるために使う（逐語訳の破綻を件数で見るため）。
"""

import argparse
import collections
import io

from eliza_script import classify, is_int, parse, tokenize


def decomp_shape(pat):
    """分解パターンの構成要素を数える。"""
    n_any = n_lit = n_tag = n_alt = 0
    for el in pat:
        if el == "0":
            n_any += 1
        elif isinstance(el, list):
            if el and el[0].startswith("/"):
                n_tag += 1
            elif el and el[0].startswith("*"):
                n_alt += 1
        else:
            n_lit += 1
    return n_any, n_lit, n_tag, n_alt


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("script")
    args = ap.parse_args()

    forms = parse(tokenize(io.open(args.script, encoding="utf-8").read()))
    greeting, rules = forms[0], [classify(f) for f in forms[1:]]

    kinds = collections.Counter(r["kind"] for r in rules)
    print("=" * 62)
    print("挨拶:", " ".join(greeting))
    print(f"トップレベルの規則: {len(rules)}")
    for k, n in kinds.most_common():
        print(f"  {k:12} {n:4}")

    print("\n--- 語の置換（= の右辺）: 入力の語を書き換えてから照合する ---")
    substs = [(r["keyword"], r["subst"]) for r in rules if r.get("subst")]
    print(f"件数 {len(substs)}")
    for k, v in substs:
        print(f"  {k:12} -> {v}")

    print("\n--- DLIST（タグ）---")
    tagmap = collections.defaultdict(list)
    for r in rules:
        if r["kind"] == "DLIST":
            for t in r["tags"]:
                tagmap[t].append(r["keyword"])
    for t, words in sorted(tagmap.items()):
        print(f"  /{t:10} {len(words):3} 語: {' '.join(words)}")

    print("\n--- rank（優先順位）の分布 ---")
    ranks = collections.Counter(r.get("rank") for r in rules if r.get("rank"))
    for rk in sorted(ranks, reverse=True):
        kws = [r["keyword"] for r in rules if r.get("rank") == rk]
        print(f"  rank {rk:3}: {ranks[rk]:3} 語  {' '.join(kws[:12])}"
              + (" ..." if len(kws) > 12 else ""))
    print(f"  rank 無し(=既定 0): {sum(1 for r in rules if not r.get('rank'))}")

    print("\n--- 変換規則（分解 → 再構成）---")
    n_decomp = n_reasm = 0
    reasm_per_decomp = collections.Counter()
    shapes = collections.Counter()
    max_slot = 0
    for r in rules:
        for tr in r.get("transforms", []):
            if not tr or not isinstance(tr[0], list):
                continue
            n_decomp += 1
            pat, reasms = tr[0], tr[1:]
            n_reasm += len(reasms)
            reasm_per_decomp[len(reasms)] += 1
            shapes[decomp_shape(pat)] += 1
            for rs in reasms:
                for el in rs if isinstance(rs, list) else []:
                    if is_int(el):
                        max_slot = max(max_slot, int(el))
    print(f"  分解パターン {n_decomp} / 再構成テンプレート {n_reasm}"
          f"  = 1 パターンあたり平均 {n_reasm / n_decomp:.2f} 通り")
    print("  1 パターンが持つテンプレート数の分布:",
          dict(sorted(reasm_per_decomp.items())))
    print(f"  再構成で参照される最大スロット番号: {max_slot}")
    print("  分解パターンの形（0の数, リテラル語, /タグ, *選択肢）上位:")
    for shape, n in shapes.most_common(8):
        print(f"    {shape} : {n}")

    print("\n--- 上位語・下位語への『変換』があるか ---")
    print("  DLIST は分解側にしか現れない（照合用）か? "
          f"再構成側にタグ参照: "
          f"{sum(1 for r in rules for tr in r.get('transforms', []) for rs in tr[1:] if isinstance(rs, list) and any(isinstance(e, list) and e and e[0].startswith('/') for e in rs))} 件")


if __name__ == "__main__":
    main()
