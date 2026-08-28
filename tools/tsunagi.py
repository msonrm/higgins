#!/usr/bin/env python3
"""日本語出力の空白を繋ぐ —— wakachi.py の対。

ELIZA のテンプレートはトークン列なので、そのまま出すと
「謝ら ない で ください」と空白だらけになる。埋め戻し（スロット）を含む以上
テンプレートを 1 トークンで書くことはできないので、★出力側で繋ぐ。

★ただし全部の空白を消すと、ユーザーが打った英文が壊れる
（「I am sad」→「Iamsad」）。**日本語文字に隣接する空白だけ**を消す。

  入力: なぜ 学校が 嫌い な の です か
  出力: なぜ学校が嫌いなのですか

  入力: どうして 私 が I am sad と 思う の です か
  出力: どうして私がI am sadと思うのですか      ← 英文の中の空白は残る
"""

import re
import sys

# ひらがな・カタカナ・漢字・全角の句読点と記号・長音
JA = r"ぁ-ゟ゠-ヿ一-鿿　-〿！-｠"

_after = re.compile(f"(?<=[{JA}]) +")
_before = re.compile(f" +(?=[{JA}])")


def tsunagi(s):
    return _before.sub("", _after.sub("", s))


def main():
    for line in sys.stdin:
        print(tsunagi(line.rstrip("\n")))


if __name__ == "__main__":
    main()
