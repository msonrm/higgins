#!/usr/bin/env python3
"""ヒンディー語 DOCTOR の**キーワードと rank の一覧**を出す。

★★**この台を作った理由**（2026-09-03）: rank を一括で書き換える正規表現が、
意図した範囲の外（`हमेशा` / `अक्सर` / `हरबार`）まで **1 → 3 に上げていた**。
すると `माँ`(2) や `डर`(3) が「いつも」に負け、**応答が静かに入れ替わる** ——
例外も警告も出ず、返ってくる文はどれも文法的に正しいので、目では絶対に分からない。
★ゴールデン（応答 18 往復）も**その組み合わせを踏んでいなかった**ので緑のままだった。

→ ★**rank は「効いていること」ではなく「値そのもの」を縛る。**
`experiments/hi_ranks.txt` と突き合わせれば、意図しない rank の変化が必ず表に出る。

  python3 tools/gen_hi_ranks.py doctor/hi.txt        # 一覧を出す
  python3 tools/gen_hi_ranks.py doctor/hi.txt --check experiments/hi_ranks.txt
"""

import io
import sys

sys.path.insert(0, "tools")
from eliza_script import is_int, parse, tokenize


def ranks(path):
    forms = parse(tokenize(io.open(path, encoding="utf-8").read()))
    out = []
    for f in forms[1:]:
        if not isinstance(f, list) or not f or f[0] == "MEMORY" or f[0] == "NONE":
            continue
        rest = list(f[1:])
        if rest and rest[0] == "=":
            rest = rest[2:]
        rank = int(rest[0]) if rest and is_int(rest[0]) else 0
        out.append("%s %d" % (f[0], rank))
    return out


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "doctor/hi.txt"
    got = ranks(path)
    if "--check" in sys.argv:
        ref = sys.argv[sys.argv.index("--check") + 1]
        want = [l for l in io.open(ref, encoding="utf-8").read().splitlines() if l.strip()]
        if got == want:
            print("OK: rank は %d 件とも基準どおり" % len(got))
            return 0
        print("FAIL: rank が基準と違う")
        w, g = dict(x.split() for x in want), dict(x.split() for x in got)
        for k in sorted(set(w) | set(g)):
            if w.get(k) != g.get(k):
                print("  %-14s 基準 %-4s → いま %s" % (k, w.get(k, "（無し）"), g.get(k, "（無し）")))
        print("  ★意図した変更なら: python3 tools/gen_hi_ranks.py %s > %s" % (path, ref))
        return 1
    print("\n".join(got))
    return 0


if __name__ == "__main__":
    sys.exit(main())
