#!/usr/bin/env python3
"""日本語 DOCTOR と対話する。

    python3 tools/talk.py doctor/ja.txt

wakachi（分かち書き）→ eliza → tsunagi（繋ぎ）を繋ぐだけの足場。
★hechima を挿す前にここを通しておくのは、後で「変換の問題」と
「ELIZA の問題」を切り分けられるようにするため。

★eliza の stdout は pipe だとフルバッファリングされてデッドロックするので
stdbuf -oL で行バッファに落とす。
"""

import argparse
import io
import os
import re
import subprocess
import sys

# ★反応（相槌・ト書き）は「（…）」か「短い文＋句点」で始まる。句には句点を入れていないので
#   機械的に切れる。反応と句は性質が違うので、表示でも分ける。
REACT_RE = re.compile(r"^(（[^）]*）|[^。]{1,14}。)(.+)$")


def split_reaction(text):
    m = REACT_RE.match(text)
    return (m.group(1), m.group(2)) if m else (None, text)

from eliza_script import parse, tokenize
from tsunagi import tsunagi
from wakachi import collect_vocab, wakachi

HERE = os.path.dirname(os.path.abspath(__file__))
ELIZA = os.path.join(HERE, os.pardir, "build", "eliza")


class Session:
    def __init__(self, script_path, eliza_path=ELIZA):
        forms = parse(tokenize(io.open(script_path, encoding="utf-8").read()))
        self.vocab = collect_vocab(forms[1:])
        self.maxlen = max((len(w) for w in self.vocab), default=1)
        self.proc = subprocess.Popen(
            ["stdbuf", "-oL", eliza_path, "--nobanner", script_path],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            text=True, bufsize=1,
        )

    def _read_reply(self):
        while True:
            line = self.proc.stdout.readline()
            if not line:
                return None
            line = line.rstrip("\n")
            if line.strip():
                return tsunagi(line)

    def greeting(self):
        return self._read_reply()

    def say(self, text):
        self.proc.stdin.write(wakachi(text, self.vocab, self.maxlen) + "\n")
        self.proc.stdin.flush()
        return self._read_reply()

    def close(self):
        try:
            self.proc.stdin.write("\n")
            self.proc.stdin.flush()
        except (BrokenPipeError, ValueError):
            pass
        self.proc.wait(timeout=5)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("script", nargs="?", default=os.path.join(HERE, os.pardir, "doctor", "ja.txt"))
    ap.add_argument("--show-tokens", action="store_true", help="分かち書きの結果も見せる")
    args = ap.parse_args()

    s = Session(args.script)
    react, verse = split_reaction(s.greeting() or "")
    if react:
        print(f"  {react}")
    print(verse)
    try:
        while True:
            try:
                text = input("> ").strip()
            except (EOFError, KeyboardInterrupt):
                break
            if not text:
                break
            if args.show_tokens:
                print("  [" + wakachi(text, s.vocab, s.maxlen) + "]", file=sys.stderr)
            reply = s.say(text)
            if reply is None:
                break
            react, verse = split_reaction(reply)
            if react:
                print(f"  {react}")
            print(verse)
    finally:
        s.close()


if __name__ == "__main__":
    main()
