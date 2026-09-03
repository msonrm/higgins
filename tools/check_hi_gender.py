#!/usr/bin/env python3
"""ヒンディー語 DOCTOR の**性別の一貫性**を見る。

★★**ヒンディー語は 1 人称の動詞が話者の性で変わる**（मैं समझता हूँ / मैं समझती हूँ）。
日本語・英語・韓国語では起きない問題で、★**機械に性別が生じる**。
`doctor/hi.txt` は**男性に決め打っている**（頭書き 3）。

★★**この検査を書いた理由**: はじめは「与格・非人称の言い回しで性別を避ける」方針で
書いたが、**方針を書いただけで検査を付けなかったので 2 か所で静かに漏れていた**
（`सकता` / `करता`）。★方針は検査が無ければ必ず崩れる。

★★**女性形そのものを禁じてはいけない。** ヒンディー語の動詞は主語の性に一致するので、
DOCTOR が女性名詞について語れば女性形が出るのが正しい:

    (क्या यह याद बार-बार आती है)    ← आती は女性名詞 याद に一致（話者の性ではない）
    (बात आपकी हो रही थी, मेरी नहीं)  ← रही / थी は बात（女性）に一致

→ ★**見るのは「`मैं` の後ろに来る動詞」だけ**。そこが DOCTOR 自身を主語にする場所。

  python3 tools/check_hi_gender.py doctor/hi.txt
"""

import io
import sys

sys.path.insert(0, "tools")
from eliza_script import parse, tokenize

# 1 人称単数で性が出る動詞形。★対で持つ（片方だけ足すと検査が非対称になる）。
PAIRS = [
    ("सकता", "सकती"), ("चाहता", "चाहती"), ("करता", "करती"), ("जानता", "जानती"),
    ("समझता", "समझती"), ("सोचता", "सोचती"), ("मानता", "मानती"), ("पाता", "पाती"),
    ("रहता", "रहती"), ("देता", "देती"), ("लेता", "लेती"), ("कहता", "कहती"),
    ("गया", "गई"), ("हुआ", "हुई"), ("रहा", "रही"), ("आया", "आई"),
    ("पाया", "पाई"), ("था", "थी"),
]
MASC = {m for m, f in PAIRS}
FEM = {f for m, f in PAIRS}
FEM_TO_MASC = {f: m for m, f in PAIRS}


def texts(form):
    """規則の中の**地の文**（再構成・MEMORY の右辺）を 1 つずつ返す。

    ★分解パターンと PRE の中身は返さない —— あちらは**ユーザーの文に当てる側**で、
      そこに現れる語は DOCTOR の言葉ではない。
    """
    out = []

    def walk(node, in_reasm):
        if not isinstance(node, list):
            return
        if node and isinstance(node[0], list):        # ((分解) (再構成) …)
            for reasm in node[1:]:
                walk(reasm, True)
            return
        if node and node[0] in ("PRE",):              # (PRE (…) (=X)) は前変換
            return
        if in_reasm:
            out.append([w for w in node if isinstance(w, str)])

    if form and form[0] == "MEMORY":
        for rule in form[2:]:
            if isinstance(rule, list) and "=" in rule:
                out.append(rule[rule.index("=") + 1:])
        return out
    for e in form[1:]:
        walk(e, False)
    return out


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "doctor/hi.txt"
    forms = parse(tokenize(io.open(path, encoding="utf-8").read()))

    bad, seen = [], []
    for form in forms:
        if not isinstance(form, list):
            continue
        for words in texts(form):
            if "मैं" not in words:
                continue
            # ★「मैं」より後ろだけを見る。前にあるものは別の主語に一致しうる。
            tail = words[words.index("मैं") + 1:]
            for w in tail:
                if w in FEM:
                    bad.append((" ".join(words), w, FEM_TO_MASC[w]))
                elif w in MASC:
                    seen.append((" ".join(words), w))

    print(f"「मैं」を主語にする地の文 {len(seen) + len(bad)} 件 "
          f"（性別の出る動詞を含むもの）")
    if bad:
        print(f"\n★FAIL: 女性形が {len(bad)} 件（DOCTOR は男性に決め打っている）:")
        for line, w, m in bad:
            print(f"  {w} → {m}   「{line}」")
        return 1
    print("OK: DOCTOR の 1 人称はすべて男性形")
    for line, w in seen:
        print(f"  {w:8} 「{line}」")
    return 0


if __name__ == "__main__":
    sys.exit(main())
