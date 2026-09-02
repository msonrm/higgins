#!/usr/bin/env python3
"""韓国語 DOCTOR の被覆検査 —— **キーワードを 1 つずつ全部叩く**。

★これは日本語版で踏んだ穴（別名から飛んだ先で分解パターンが合わず、
ELIZA が**組み込みの英語メッセージ**を返していた・13 件）を、
**人が打つ前に機械で見つける**ための道具。

日本語版のゴールデンは会話の流れを見るものだったので、代表形（어머니・슬퍼）しか
通らず、★**別名を 1 つも踏んでいなかった**。ここではスクリプトから
キーワードを取り出して**全件**に文を作って通す。台本を書き足す必要が無いので、
★**規則を増やしても検査が置いていかれない**。

判定は 2 つ:

  1. ★★**応答にラテン文字が出たら失敗**。スクリプトは 100% 韓国語なので、
     英字が出るのは**エンジンの組み込みメッセージが漏れた**ときだけ。
     （`PLEASE CONTINUE` / `HMMM` / ill-formed script の類）
  2. **NONE に落ちたキーワードを報告**。規則があるのに届いていない ＝
     分かち直し（josa.py）でトークンが作られていない、という形の穴。
     ★失敗にはしない —— 語によっては NONE が正しいこともあるため、目視で判断する。

  python3 tools/coverage_ko.py doctor/ko.txt
"""

import io
import re
import subprocess
import sys

sys.path.insert(0, "tools")
from eliza_script import classify, parse, tokenize
from josa import josa
from tsunagi_ko import tsunagi_ko
from wakachi import collect_vocab

LATIN = re.compile(r"[A-Za-z]")
# 語尾を付けた形も試す。★分かち直しが**剥がせること**まで含めて検査するため。
SUFFIXES = ["", "요", "예요", "가 싫어요"]


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
    script = sys.argv[1] if len(sys.argv) > 1 else "doctor/ko.txt"
    forms = parse(tokenize(io.open(script, encoding="utf-8").read()))
    vocab = collect_vocab(forms[1:])
    maxlen = max((len(w) for w in vocab), default=1)
    kws = keywords(forms[1:])

    probes = [(kw, f"{kw}{(' ' + s) if s else ''}") for kw in kws for s in SUFFIXES]
    fed = "\n".join(josa(text, vocab, maxlen) for _, text in probes) + "\n"
    proc = subprocess.run(["./build/eliza", "--nobanner", script],
                          input=fed, capture_output=True, text=True)
    lines = [tsunagi_ko(l) for l in proc.stdout.splitlines() if l.strip()]
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
                none_words.append(tsunagi_ko(" ".join(reasm)))

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
