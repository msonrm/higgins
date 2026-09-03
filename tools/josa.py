#!/usr/bin/env python3
"""韓国語の分かち直し —— 조사・어미 を語から切り離す。wakachi.py の韓国語版。

★**日本語ほどの仕事は要らない。** 韓国語には띄어쓰기（分かち書き）が既にあるので、
ELIZA の `split_user_input()` はそのまま通る。要るのは 1 つだけ ——
**語にくっついている 조사・어미 を切り離す**こと。「어머니가」は 1 トークンなので、
キーワード「어머니」が当たらない。

## 切り方（実測で 3 度直した形）

1 つの塊（空白で区切られた 1 語）を、**後ろから 조사・어미 を剥がし、
残りの先頭でキーワードを 1 回だけ拾う**。

  어머니를   → 어머니 · 를
  필요해요   → 필요 · 해 · 요
  요즘       → 요즘            ★切らない（下記）

★**辞書の途中一致をしない**のが肝。素朴に最長一致で走らせると、韓国語では
1 音節のキーワードが**語の中に埋もれて誤爆する**（実測）:

  요즘   → 요 + 즘     「요」（受け皿）が語頭で当たる
  이제   → 이 + 제     「제」（私の）が語末で当たる → 出力が「당신의」になる
  무리예요 → 무리+예+요  「예」（はい）が当たり、DOCTOR が肯定を返す

  ★歯止めは 2 つ: **拾うのは塊の先頭で 1 回だけ**／**1 音節の語は塊を
    使い切るときだけ**（요즘 の「요」は使い切らないので却下される）。

★★**繋ぎの印は独立したトークン `·`** にする。語にくっつけると
（`·를` のように）**その語がパターンで書けなくなる** —— 「처럼」のように
조사 でありながらキーワードでもある語が実際にあるので、印は語の外へ出す。
これで分解パターンもテンプレートも**素の語のまま**書ける。

★★★交替する 조사（은/는・이/가・을/를・과/와・으로/로）は**入り口で代表形に寄せる**。
そうしないと「저는」と「당신은」が別トークンになり、パターンが片方にしか当たらない。
出口（tools/tsunagi_ko.py）で직전の받침を見て実際の形に戻す。

  josa.py doctor/ko.txt < 입력
"""

import argparse
import io
import sys

from wakachi import PUNCT, collect_vocab
from eliza_script import parse, tokenize

MARK = "·"

# ★ELIZA は "," "." "BUT" を delimiter として節を切る（エンジンに焼かれている）。
#   ★**韓国語は ASCII の句読点を打つ**ので、`wakachi.py` の PUNCT（全角と ! ? だけ）
#   では足りない —— `.` が語にくっついたままだと「슬퍼요.」が 1 塊になり、
#   ★**受け皿の「요」が切り出せずに NONE へ落ちる**（韓国語モードが打つのは ASCII）。
PUNCT_KO = dict(PUNCT, **{".": " . ", ",": " , "})


def normalize_punct_ko(text):
    for k, v in PUNCT_KO.items():
        text = text.replace(k, v)
    return text

# 交替する 조사 → 代表形。★受け側（tsunagi_ko.py の JOSA_FORM）と対。片方だけ増やさないこと。
JOSA_CANON = {
    "은": "는", "는": "는",
    "이": "가", "가": "가",
    "을": "를", "를": "를",
    "과": "와", "와": "와",
    "으로": "로", "로": "로",
}

# 語尾から剥がすもの。★長いものから試す（下の strip_tail が長さ順に見る）。
#
# ★「나」「이나」は入れない —— 「나」は一人称のキーワードでもあるので、
#   「어머니나」を剥がすと **DOCTOR が「私」の話だと誤解する**。
# ★「처럼」は入れてよい —— 印が独立トークンなので、剥がしても
#   キーワード「처럼」としてそのまま当たる。
JOSA = [
    "은", "는", "이", "가", "을", "를", "과", "와", "도", "만", "의",
    "에서", "에게", "한테", "께", "에", "부터", "까지", "보다", "처럼",
    "으로", "로", "라도", "밖에", "마다", "조차", "랑", "이랑",
    "이라고", "라고", "이라는", "라는",
]

# 語尾（해요체 中心）。★1 音節のキーワード（요・같・네）を塊の末尾に残すために要る ——
#   「같아요」から「아요」を剥がせば「같」が塊を使い切り、DIT 規則が当たる。
#   ★網羅は狙わない。**当たらない語尾が出たら実測で足す**（融合が絡むので机上では尽きない）。
EOMI = [
    "이에요", "예요", "에요", "아요", "어요", "여요", "해요",
    "으세요", "하세요", "세요", "네요", "군요", "지요", "죠",
    "을까요", "ㄹ까요", "까요", "나요", "거든요",
    "습니다", "입니다", "습니까", "ㅂ니다",
    "아서", "어서", "니까", "는데", "지만", "고요", "요",
]

TAILS = sorted(set(JOSA + EOMI), key=len, reverse=True)
JOSA_SET = set(JOSA)


def josa_ok(tail, rest, vocab):
    """この 조사 を剥がしてよいか（★어미 は対象外・常に True）。

    ★★**조사 は名詞に付く。** 剥がした残りが**1 音節で辞書にも無い**なら、
    それは 조사 ではなく**語の一部**。実測（2026-09-03）:

        사과는 → 사 · 와 · 는   ★「사과」が「사」＋「과」に割れ、しかも
                                 과 → 와 と代表形に寄るので、DOCTOR が
                                 ★**「사와는 안 받아요」とユーザーの言葉を
                                 書き換えて引用していた**
        보이세요 → 보 · 가 · 세요 ／ 많이 → 많 · 가 ／ 다를까요 → 다 · 를 · 까요

    ★**어미 には掛けない** —— 「받아요」の「받」のように、1 音節の語幹は普通なので
      （そこを止めると受け皿の `요` が働かなくなる）。
    """
    if tail not in JOSA_SET:
        return True
    return len(rest) != 1 or rest in vocab

# ★人称代名詞の**不規則な縮約**。ここで解かないと規則の側で書けない ——
#   「제가」は「저 + 가」だが表層が変わるので、素朴に見ると「제」（＝私の）と
#   区別がつかず、置換で「당신의가」になる。
#   ★実測（tools/coverage_ko.py）: スクリプト側で (제가 = 당신이) と書いたら、
#     飛び先の分解パターン (0 당신 0) が「당신이」に合わず、**組み込みの英語**
#     （HMMM / I SEE / GO ON, PLEASE）が返ってきた —— 日本語版で踏んだ穴と同じ形。
#   ★★これは**形態論の事実**なので、スクリプトではなく分かち直しの持ち分。
CONTRACTIONS = {
    "제가": ["저", "가"], "내가": ["나", "가"], "네가": ["너", "가"],
    "절": ["저", "를"], "널": ["너", "를"], "날": ["나", "를"],
    "뭘": ["뭐", "를"], "뭐가": ["뭐", "가"],
}


def split_tail_chunk(chunk):
    """語尾のかたまりを、後ろから既知の 조사・어미 に分ける。

    残った先頭の不明部分は 1 つのかけらとして返す（려요 → 려 + 요）。
    """
    out = []
    while chunk:
        for t in TAILS:
            if chunk.endswith(t):
                out.insert(0, JOSA_CANON.get(t, t))
                chunk = chunk[:-len(t)]
                break
        else:
            out.insert(0, chunk)
            break
    return out


def head_of(run, vocab, maxlen):
    """塊の**先頭で 1 回だけ**辞書を引く。戻り値 = (見出し, 残り) / 見つからなければ None。

    ★1 音節の語は塊を使い切るときだけ認める —— これが無いと
      「요즘」が「요」＋「즘」に割れて受け皿が語頭で誤爆する（実測）。
    """
    for length in range(min(maxlen, len(run)), 0, -1):
        word = run[:length]
        if word in vocab and not (length == 1 and length != len(run)):
            return word, run[length:]
    return None


def split_run(run, vocab, maxlen):
    """1 つの塊をトークンの列にする。

    ★**辞書引きが先、語尾剥がしが後**。逆にすると**語尾で終わる語が壊れる** ——
      実測で「필요」（要る）が「필」＋「요」に割れ、キーワードが消えた
      （DOCTOR が受け皿の相槌を返してしまった）。
    """
    if run in CONTRACTIONS:                           # ⓪ 不規則な縮約を先に解く
        return list(CONTRACTIONS[run])
    hit = head_of(run, vocab, maxlen)                 # ① そのままで引く
    if hit is None:
        head, tails = run, []
        while True:                                    # ② 語尾を剥がしてから引く
            for t in TAILS:
                if len(head) > len(t) and head.endswith(t):
                    if not josa_ok(t, head[:-len(t)], vocab):
                        continue                       # ★조사 ではなく語の一部
                    tails.insert(0, JOSA_CANON.get(t, t))
                    head = head[:-len(t)]
                    break
            else:
                break
        hit = head_of(head, vocab, maxlen)
        if hit is None:
            return [run] if not tails else [head] + tails
        return [hit[0]] + split_tail_chunk(hit[1]) + tails
    return [hit[0]] + split_tail_chunk(hit[1])


def josa(text, vocab, maxlen):
    out = []
    for run in normalize_punct_ko(text).split():
        if run in (",", "."):
            out.append(run)
            continue
        toks = split_run(run, vocab, maxlen)
        out.append(toks[0])
        for t in toks[1:]:
            out.extend([MARK, t])
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
        print(josa(line.rstrip("\n"), vocab, maxlen))


if __name__ == "__main__":
    main()
