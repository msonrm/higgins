#!/usr/bin/env python3
"""日本語 DOCTOR の被覆検査 —— **キーワードを 1 つずつ全部叩く**。

★★**韓国語・ヒンディー語には前からあったのに、日本語だけ無かった**（2026-09-03 に気づいた）。
2026-09-01 に日本語の穴 13 件を直したときは**手で数えた**ので、
★**同じ穴がもう一度開いても誰も気づかない**状態だった。台のほうを揃える。

判定は 2 つ:

  1. ★★**応答にラテン文字が出たら失敗**。スクリプトは 100% 日本語なので、
     英字が出るのは**エンジンの組み込みメッセージが漏れた**ときだけ
     （`PLEASE CONTINUE` / `HMMM` / ill-formed script の類）。
  2. **NONE に落ちたキーワードを報告**。規則があるのに届いていない ＝
     分かち書き（wakachi.py）でトークンが作られていない、という形の穴。
     ★失敗にはしない —— **`(X ((0) (NEWKEY)))` と宣言しただけの語は NONE が正しい**
     （「知っているが私の話ではない語」）。目視で判断する。

  python3 tools/coverage_ja.py doctor/ja.txt
"""


import io
import re
import subprocess
import sys

sys.path.insert(0, "tools")
from eliza_script import classify, parse, tokenize
from wakachi import wakachi
from tsunagi import tsunagi
from wakachi import collect_vocab

LATIN = re.compile(r"[A-Za-z]")
# 語尾を付けた形も試す。★分かち書きが**切り出せること**まで含めて検査するため。
SUFFIXES = ["", "です", "でした", "が嫌いです"]


def keywords(forms):
    out = []
    for f in forms:
        r = classify(f)
        if r["kind"] in ("MEMORY", "NONE"):
            continue
        kw = r["keyword"]
        if kw and kw not in out:
            out.append(kw)
    return out


def main():
    script = sys.argv[1] if len(sys.argv) > 1 else "doctor/ja.txt"
    forms = parse(tokenize(io.open(script, encoding="utf-8").read()))
    vocab = collect_vocab(forms[1:])
    maxlen = max((len(w) for w in vocab), default=1)
    kws = keywords(forms[1:])

    probes = [(kw, f"{kw}{s}") for kw in kws for s in SUFFIXES]
    fed = "\n".join(wakachi(text, vocab, maxlen) for _, text in probes) + "\n"
    proc = subprocess.run(["./build/eliza", "--nobanner", script],
                          input=fed, capture_output=True, text=True)
    lines = [tsunagi(l) for l in proc.stdout.splitlines() if l.strip()]
    greeting, replies = lines[0], lines[1:]

    if len(replies) != len(probes):
        print(f"FAIL: 応答の数が合わない（{len(replies)} / {len(probes)}）")
        return 1

    english, none_hits = [], []
    # ★NONE の応答文はスクリプトから取る（書き写すと、直したときに検査が置いていかれる）
    none_words = []
    for f in forms[1:]:
        r = classify(f)
        if r["kind"] != "NONE":
            continue
        for group in r["transforms"]:
            for reasm in group[1:]:
                none_words.append(tsunagi(" ".join(reasm)))

    for (kw, text), reply in zip(probes, replies):
        if LATIN.search(reply):
            english.append((kw, text, reply))
        if any(reply.startswith(w) for w in none_words):
            none_hits.append((kw, text))

    print(f"キーワード {len(kws)} 語 × {len(SUFFIXES)} 形 = {len(probes)} 件を通した")
    if english:
        print(f"\n★FAIL: 英語が {len(english)} 件漏れた（組み込みメッセージ）:")
        for kw, text, reply in english[:20]:
            print(f"  {kw:8} 「{text}」 → {reply}")
        return 1
    print("OK: 英語の漏れなし（組み込みメッセージは出ていない）")

    if none_hits:
        by_kw = {}
        for kw, text in none_hits:
            by_kw.setdefault(kw, []).append(text)
        full = [k for k, v in by_kw.items() if len(v) == len(SUFFIXES)]
        print(f"\n注意: NONE に落ちた組み合わせ {len(none_hits)} 件 / "
              f"うち**全形が落ちた語**が {len(full)} 語:")
        if full:
            print("  " + " ".join(full))
    else:
        print("OK: NONE に落ちたキーワードなし")
    return 0


if __name__ == "__main__":
    sys.exit(main())
