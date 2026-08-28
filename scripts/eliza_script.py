#!/usr/bin/env python3
"""ELIZA スクリプト（S 式）の読み取り。analyze_script.py と wakachi.py が使う。"""


def tokenize(text):
    out = []
    for line in text.splitlines():
        i = line.find(";")          # ; 以降はコメント
        if i >= 0:
            line = line[:i]
        out.extend(line.replace("(", " ( ").replace(")", " ) ").split())
    return out


def parse(tokens):
    """トップレベルの S 式を順に返す。"""
    pos = 0

    def read():
        nonlocal pos
        tok = tokens[pos]
        pos += 1
        if tok == "(":
            lst = []
            while tokens[pos] != ")":
                lst.append(read())
            pos += 1
            return lst
        return tok

    forms = []
    while pos < len(tokens):
        forms.append(read())
    return forms


def is_int(s):
    return isinstance(s, str) and s.isdigit()


def classify(form):
    """1 つのトップレベル規則を分解して素性を返す。"""
    head = form[0] if form else None
    if head == "MEMORY":
        return {"kind": "MEMORY", "keyword": form[1], "rules": form[2:]}
    if head == "NONE":
        return {"kind": "NONE", "keyword": "NONE",
                "transforms": [f for f in form[1:] if isinstance(f, list)]}

    rest = list(form[1:])
    subst = None
    rank = None
    if rest and rest[0] == "=":
        subst = rest[1]
        rest = rest[2:]
    if rest and is_int(rest[0]):
        rank = int(rest[0])
        rest = rest[1:]

    if rest and rest[0] == "DLIST":
        tags = [t for t in rest[1] if t != "/"] if isinstance(rest[1], list) else []
        tags = [t.lstrip("/") for t in tags]
        return {"kind": "DLIST", "keyword": head, "subst": subst, "tags": tags}

    if len(rest) == 1 and isinstance(rest[0], list) and rest[0] and rest[0][0] == "=":
        return {"kind": "EQUIV", "keyword": head, "subst": subst, "rank": rank,
                "target": rest[0][1]}

    if not rest:
        return {"kind": "SUBST_ONLY", "keyword": head, "subst": subst}

    return {"kind": "RULE", "keyword": head, "subst": subst, "rank": rank,
            "transforms": [f for f in rest if isinstance(f, list)]}
