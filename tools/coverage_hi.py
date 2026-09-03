#!/usr/bin/env python3
"""ヒンディー語 DOCTOR の被覆検査 —— **キーワードを 1 つずつ全部叩く**。coverage_ko.py の対。

★これは日本語版で踏んだ穴（別名から飛んだ先で分解パターンが合わず、ELIZA が
**組み込みの英語メッセージ**を返していた・13 件）を、**人が打つ前に機械で見つける**道具。
韓国語では初回で 8 件を掘り当てた。

★★ヒンディー語では**もう 1 つの穴の型**を見張れる ——
**置換したら見出しに当たらなくなる**という形（実測で 3 件）:

  मेरी → आपकी と置換したのに、パターンが `(0 आपका 0)` としか書いていない
  सोचता → सोचते と置換したのに、DLIST のタグが `सोचता` にしか付いていない

★どちらも**置換前の語でパターンを書いた**のが原因で、**画面上は正しい字が出る**ので
目では分からない。ここで全キーワードを叩けば NONE に落ちる形で表に出る。

判定は 2 つ:

  1. ★★**応答にラテン文字が出たら失敗**。スクリプトは 100% ヒンディー語なので、
     英字が出るのは**エンジンの組み込みメッセージが漏れた**ときだけ。
  2. ★★**「規則を持つのに NONE に落ちた語」だけを報告する。**
     韓国語版は NONE を全部並べていたが、それだと**置換専用の語**（`(मुझसे = आपसे)`）や
     **タグ専用の語**（`(लगता DLIST(/विश्वास))`）が毎回 23 語ぶん並ぶ ——
     ★あれは NONE が**正しい**挙動なので、混ぜると本物の穴が雑音に埋もれる。
     英語原典の `(ME = YOU)` `(FEEL DLIST(/BELIEF))` も同じ形。

★形は 4 つ。★**`।` を空白なしで付けた形を必ず入れる** —— 入口（hindi_in.py）が
danda を `.` に写していることまで含めて検査するため（写していないと全件 NONE になる）。

  python3 tools/coverage_hi.py doctor/hi.txt
"""

import io
import re
import subprocess
import sys

sys.path.insert(0, "tools")
from eliza_script import classify, parse, tokenize
from hindi_in import hindi_in

LATIN = re.compile(r"[A-Za-z]")
# ★1 つ目は素の語、2 つ目は **danda を空白なしで**、あとは後置詞と繋辞。
SUFFIXES = ["", "।", " है", " के बारे में"]


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


def has_response(form):
    """その規則が**自分で応答を作れるか**。

    ★偽の警告を消すために要る —— NONE が**正しい**規則が 3 種類ある:
      1. 置換専用（`(मुझसे = आपसे)`）
      2. タグ専用（`(लगता DLIST(/विश्वास))`）
      3. ★**前変換専用**（`(करता 1 ((0 आप 0 करता 0) (PRE … (=मैं))) ((0) (NEWKEY)))`）
         —— 条件が成り立たなければ NEWKEY で落ちるので、単独で叩けば NONE になる。
         **それが設計どおり**（3 人称の文を書き換えないための条件付き規則）。
    ★eliza_script.classify は DLIST と変換規則が同居した形（`(माँ 2 DLIST(...) ((0) ...))`）を
      DLIST として返し、**transforms を落とす**ので、生の S 式から数える。
    """
    for e in form[1:]:
        if not isinstance(e, list) or not e:
            continue
        if isinstance(e[0], str) and e[0].startswith("="):   # (=飛び先)
            return True
        if not isinstance(e[0], list):
            continue
        # ((分解) (再構成) …) —— **地の文を返す再構成が 1 つでもあるか**を見る。
        for reasm in e[1:]:
            if isinstance(reasm, list) and reasm and reasm[0] not in ("PRE", "NEWKEY"):
                return True
    return False


def none_replies(forms):
    """NONE の応答文はスクリプトから取る（書き写すと直したとき検査が置いていかれる）。"""
    out = []
    for f in forms:
        r = classify(f)
        if r["kind"] != "NONE":
            continue
        for group in r["transforms"]:
            for reasm in group[1:]:
                out.append(" ".join(reasm))
    return out


def main():
    script = sys.argv[1] if len(sys.argv) > 1 else "doctor/hi.txt"
    forms = parse(tokenize(io.open(script, encoding="utf-8").read()))
    kws = keywords(forms[1:])
    # ★規則を持たない語（置換専用 / タグ専用）。NONE に落ちるのが正しい。
    ruleless = {f[0] for f in forms[1:] if f and not has_response(f)}

    probes = [(kw, kw + s) for kw in kws for s in SUFFIXES]
    fed = "\n".join(hindi_in(text) for _, text in probes) + "\n"
    proc = subprocess.run(["./build/eliza", "--nobanner", script],
                          input=fed, capture_output=True, text=True)
    lines = [l.strip() for l in proc.stdout.splitlines() if l.strip()]
    replies = lines[1:]

    if len(replies) != len(probes):
        print(f"FAIL: 応答の数が合わない（{len(replies)} / {len(probes)}）")
        return 1

    nones = none_replies(forms[1:])
    english, none_hits = [], []
    for (kw, text), reply in zip(probes, replies):
        if LATIN.search(reply):
            english.append((kw, text, reply))
        if any(reply.startswith(w) for w in nones):
            none_hits.append((kw, text))

    print(f"キーワード {len(kws)} 語 × {len(SUFFIXES)} 形 = {len(probes)} 件を通した")
    if english:
        print(f"\n★FAIL: 英語が {len(english)} 件漏れた（組み込みメッセージ）:")
        for kw, text, reply in english[:20]:
            print(f"  {kw:12} 「{text}」 → {reply}")
        return 1
    print("OK: 英語の漏れなし（組み込みメッセージは出ていない）")

    real = [(kw, t) for kw, t in none_hits if kw not in ruleless]
    n_ruleless = len([k for k in kws if k in ruleless])
    print(f"（うち {n_ruleless} 語は置換専用 / タグ専用で、NONE が正しい）")
    if real:
        by_kw = {}
        for kw, text in real:
            by_kw.setdefault(kw, []).append(text)
        full = [k for k, v in by_kw.items() if len(v) == len(SUFFIXES)]
        print(f"\n★注意: **規則を持つのに** NONE に落ちた組み合わせ {len(real)} 件 / "
              f"うち全形が落ちた語が {len(full)} 語:")
        for kw, texts in sorted(by_kw.items(), key=lambda kv: -len(kv[1]))[:20]:
            print(f"  {kw:12} {len(texts)}/{len(SUFFIXES)} 形  例: 「{texts[0]}」")
    else:
        print("OK: 規則を持つ語はすべて届いている（NONE に落ちたのは規則の無い語だけ）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
