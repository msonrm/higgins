#!/usr/bin/env python3
"""labo の korean-composer.ts からハングル合成の表を抽出して korean_tables.h を生成。

手写しの転記事故を避けるため、正典（TS）を直接パースする。
★`gen_tables.py`（かな表）と同じ判断 —— **同じ正典から自分で焼く**。
  生成物を pin すると「上流が加工した生成物」を掴むことがある（2026-08-31 の教訓）。

    python3 gen_korean.py [korean-composer.ts のパス]

★入力（どのボタンでどの行を出すか）は labo web の `stepKorean` が正典で、
  こちらは**合成の表だけ**。GIME（Swift / Kotlin）にしかない 자모 모드は写さない
  —— Higgins の韓国語の席には載せない（TODO の段階 3）。
"""
import re
import sys
from pathlib import Path

DEFAULT = Path.home() / "development/logical-layout-labo/web/src/engine/korean-composer.ts"
path = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT
if not path.exists():
    sys.exit(f"正典が見つからない: {path}")
src = re.sub(r"//[^\n]*", "", path.read_text())     # 行コメントを落とす


def block(name):
    """`export const NAME ... = 〈括弧〉…〈対応する閉じ〉` の中身を返す。"""
    m = re.search(r"export const " + name + r"[^=]*=\s*[\[{(]", src)
    if not m:
        sys.exit(f"gen_korean: {name} が見つからない")
    open_ch = src[m.end() - 1]
    depth, i = 1, m.end()
    while depth:
        if src[i] in "[{(":
            depth += 1
        elif src[i] in "]})":
            depth -= 1
        i += 1
    return src[m.end():i - 1]


def ints(name):
    return [int(x) for x in re.findall(r"-?\d+", block(name))]


def pairs(name):
    return [(int(a), int(b)) for a, b in re.findall(r"(\d+)\s*:\s*(\d+)", block(name))]


def nested(name):
    out = []
    for outer, inner in re.findall(r"(\d+)\s*:\s*\{([^}]*)\}", block(name)):
        for a, b in re.findall(r"(\d+)\s*:\s*(\d+)", inner):
            out.append((int(outer), int(a), int(b)))
    return out


onset_row = ints("KOREAN_ONSET_FOR_ROW")
coda_row = ints("KOREAN_CODA_FOR_ROW")
nuc_base = ints("KOREAN_NUCLEUS_BASE")
nuc_shift = ints("KOREAN_NUCLEUS_SHIFTED")
# ★形が変われば黙って半分動く、では困るので数を主張しておく。
assert len(onset_row) == 10 and len(coda_row) == 10, (onset_row, coda_row)
assert len(nuc_base) == 5 and len(nuc_shift) == 5, (nuc_base, nuc_shift)

onset_cycle = pairs("KOREAN_ONSET_CYCLE")
coda_cycle = pairs("KOREAN_CODA_CYCLE")
add_i = pairs("KOREAN_NUCLEUS_ADD_I")
add_aeo = pairs("KOREAN_NUCLEUS_ADD_AEO")
double_coda = nested("KOREAN_DOUBLE_CODA")


def row(vals):
    return "    " + " ".join(f"{v}," for v in vals)


def pair_table(name, data, note):
    lines = [f"/* {note} */",
             f"enum {{ {name}_N = {len(data)} }};",
             f"static const unsigned char {name}[][2] = {{"]
    lines += [f"    {{ {a}, {b} }}," for a, b in data]
    lines += ["};", ""]
    return lines


out = [
    "/* native/gen_korean.py が labo の korean-composer.ts から生成。手で編集しない。",
    " * 正典 = web/src/engine/korean-composer.ts（合成の表）。 */",
    "#ifndef HG_KOREAN_TABLES_H",
    "#define HG_KOREAN_TABLES_H",
    "",
    "/* 音節 = 0xAC00 + (초성 * 21 + 중성) * 28 + 종성 */",
    "enum { KO_ONSET_N = 19, KO_NUCLEUS_N = 21, KO_CODA_N = 28 };",
    "",
    "/* 子音行（D-pad / LB+D-pad）→ 초성 / 종성 */",
    "static const unsigned char KO_ONSET_FOR_ROW[10] = {",
    row(onset_row),
    "};",
    "static const unsigned char KO_CODA_FOR_ROW[10] = {",
    row(coda_row),
    "};",
    "",
    "/* フェイスボタン → 중성（RT で y 系 + ㅣ の層に替わる） */",
    "static const unsigned char KO_NUCLEUS_BASE[5] = {",
    row(nuc_base),
    "};",
    "static const unsigned char KO_NUCLEUS_SHIFTED[5] = {",
    row(nuc_shift),
    "};",
    "",
]
out += pair_table("KO_ONSET_CYCLE", onset_cycle, "초성 平音→激音→濃音（rs↑）")
out += pair_table("KO_CODA_CYCLE", coda_cycle, "종성 받침サイクル（rs↑・받침があればこちらが先）")
out += pair_table("KO_ADD_I", add_i, "ㅣ 付加による複合母音（RT 単押し）")
out += pair_table("KO_ADD_AEO", add_aeo, "ㅏ/ㅓ 付加による複合母音（rs→）")
out += [
    "/* 겹받침: [第 1 종성][第 2 종성] → 겹받침 */",
    f"enum {{ KO_DOUBLE_CODA_N = {len(double_coda)} }};",
    "static const unsigned char KO_DOUBLE_CODA[][3] = {",
]
out += [f"    {{ {a}, {b}, {c} }}," for a, b, c in double_coda]
out += ["};", "", "#endif", ""]

(Path(__file__).parent / "korean_tables.h").write_text("\n".join(out), encoding="utf-8")
print(f"OK: korean_tables.h（초성サイクル {len(onset_cycle)} / 받침サイクル {len(coda_cycle)} / "
      f"ㅣ付加 {len(add_i)} / ㅏㅓ付加 {len(add_aeo)} / 겹받침 {len(double_coda)}）")
