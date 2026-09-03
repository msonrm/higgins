#!/usr/bin/env python3
"""labo の GIME（Swift）からデーヴァナーガリーの表を抽出して devanagari_tables.h を生成。

手写しの転記事故を避けるため、正典を直接パースする。
★`gen_korean.py` / `gen_tables.py` と同じ判断 —— **同じ正典から自分で焼く**。

★★**正典は GIME の Swift**（`GamepadResolver.swift` / `DevanagariComposer.swift`）。
  韓国語は labo web の TS を正典にしたが、★**web にデーヴァナーガリーの実装は無い**。
  韓国語ではその選択のせいで `revertCoda` の配慮が丸ごと落ちた（実機 1 巡目で発覚）ので、
  今回は**実装のある側**を正典にする。

    python3 gen_devanagari.py [GIME の Sources のパス]
"""
import re
import sys
from pathlib import Path

DEFAULT = Path.home() / "development/logical-layout-labo/Sources/GIME"
base = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT
resolver = base / "GamepadResolver.swift"
composer = base / "DevanagariComposer.swift"
for p in (resolver, composer):
    if not p.exists():
        sys.exit(f"gen_devanagari: 正典が見つからない: {p}")

def strip_comments(t):
    return re.sub(r"//[^\n]*", "", t)

RES = strip_comments(resolver.read_text())
COM = strip_comments(composer.read_text())

def block(src, name):
    """`let NAME ... = 〈括弧〉…〈対応する閉じ〉` の中身を返す。"""
    m = re.search(r"let " + name + r"\b[^=]*=\s*[\[{(]", src)
    if not m:
        sys.exit(f"gen_devanagari: {name} が見つからない")
    depth, i = 1, m.end()
    while depth:
        if src[i] in "[{(":
            depth += 1
        elif src[i] in "]})":
            depth -= 1
        i += 1
    return src[m.end():i - 1]

def chars(text):
    """`"क", "ख"` から文字を順に拾う。"""
    return re.findall(r'"([^"]+)"', text)

def cp(ch):
    return f"0x{ord(ch):04X}"

def row(name, vals, width=5):
    body = ", ".join(cp(c) if c else "0" for c in vals)
    return f"static const unsigned short {name}[{width}] = {{ {body} }};"

# ---- varga（LS 方向）→ 子音 5 個 ----
varga_src = block(RES, "devaVargaConsonants")
vargas = [chars(x) for x in re.findall(r"\[([^\[\]]*)\]", varga_src)]
if len(vargas) != 5 or any(len(v) != 5 for v in vargas):
    sys.exit(f"gen_devanagari: varga の形が違う: {[len(v) for v in vargas]}")

# ---- 非 varga ----
semi = chars(block(RES, "devaNonVargaSemivowel"))
sib = chars(block(RES, "devaNonVargaSibilant"))
if len(semi) != 4 or len(sib) != 4:
    sys.exit("gen_devanagari: 非 varga の数が違う")

# ---- 面ボタン → 母音 ----
# VowelButton: a=0(RB) / i=1(左) / u=2(上) / e=3(右) / o=4(下)
# ★Higgins の vowel index と**同じ並び**（main.c の面ボタン判定と一致することを確認済み）。
BTN = {"a": 0, "i": 1, "u": 2, "e": 3, "o": 4}

def vowel_map(name):
    out = [""] * 5
    for key, ch in re.findall(r"\.(\w+)\s*:\s*\"([^\"]+)\"", block(RES, name)):
        if key not in BTN:
            sys.exit(f"gen_devanagari: 知らない VowelButton: {key}")
        out[BTN[key]] = ch
    return out

indep = vowel_map("devaFaceVowelIndependent")
matra = vowel_map("devaFaceVowelMatra")
indep_lt = vowel_map("devaFaceVowelIndependentLT")
matra_lt = vowel_map("devaFaceVowelMatraLT")

# ★RB（index 0）は表に無い —— `handleDevanagariInput` が直に ओ / ो を入れている。
#   ★**表と実装に散らばっているので、ここで 1 か所に集める**（読む側は表だけ見ればよくなる）。
#   出どころ: GamepadInputManager.swift の「RB edge: ओ（単押し）/ nukta（LT 同時押し）」
if indep[0] or matra[0]:
    sys.exit("gen_devanagari: RB の枠が埋まっている（上流が変わった）")
indep[0], matra[0] = "ओ", "ो"

# ---- 短 → 長（post-shift）----
def pair_map(src, name):
    return re.findall(r'"([^"]+)"\s*:\s*"([^"]+)"', block(src, name))

vlong = pair_map(COM, "devaVowelShortToLong")
mlong = pair_map(COM, "devaMatraShortToLong")

# ---- candra（LT + RS↑ の post-shift）----
# ★英語由来語（डॉक्टर / कॉलेज / ऑफिस）に軒並み付くので日常語の綴りに要る。
vcandra = pair_map(COM, "devaVowelToCandra")
mcandra = pair_map(COM, "devaMatraToCandra")

out = ['/* native/gen_devanagari.py が labo の GIME（Swift）から生成。手で編集しない。',
       ' * 正典 = Sources/GIME/GamepadResolver.swift（配置）',
       ' *        Sources/GIME/DevanagariComposer.swift（長短の対）',
       ' * ★web にデーヴァナーガリーの実装は無いので、**実装のある側**を正典にしている。 */',
       "#ifndef HG_DEVANAGARI_TABLES_H",
       "#define HG_DEVANAGARI_TABLES_H",
       "",
       "/* varga（LS 方向: 中立=प / ↑=क / →=च / ↓=ट / ←=त）→ 子音 5 個。",
       "   並びは varnamala 順 = [無気無声, 有気無声, 無気有声, 有気有声, 鼻音]。",
       "   ★D-pad ↑→↓← が index 0..3、LB が index 4（鼻音）。 */",
       "enum { DEVA_VARGA_N = 5, DEVA_STOP_N = 5 };",
       "static const unsigned short DEVA_VARGA[DEVA_VARGA_N][DEVA_STOP_N] = {"]
for v in vargas:
    out.append("    { " + ", ".join(cp(c) for c in v) + " },")
out += ["};", "",
        "/* varga の見出し（行インジケータに出す ―― 日本語の「あ」に当たる）。",
        "   ★無気無声（index 0）を代表にする ―― varnamala の頭。 */",
        row("DEVA_VARGA_HEAD", [v[0] for v in vargas]),
        "",
        "/* 非 varga 層（L3 で入る）。LT 離し = 半母音 / LT 押し = 摩擦音 + ह。",
        "   ★D-pad ↑→↓← が index 0..3。 */",
        "static const unsigned short DEVA_SEMIVOWEL[4] = { " + ", ".join(cp(c) for c in semi) + " };",
        "static const unsigned short DEVA_SIBILANT[4]  = { " + ", ".join(cp(c) for c in sib) + " };",
        "",
        "/* 面ボタン（vowel index: 0=RB / 1=□左 / 2=△上 / 3=○右 / 4=✕下）→ 母音。",
        "   ★**matra を先に試し、子音が無ければ独立母音**に落ちる（Composer が決める）。",
        "   0 = そのボタンに母音は無い。 */",
        row("DEVA_VOWEL_INDEP", indep),
        row("DEVA_VOWEL_MATRA", matra),
        "/* LT 同時押しの拡張（面ボタンが足りないぶんを補う）。 */",
        row("DEVA_VOWEL_INDEP_LT", indep_lt),
        row("DEVA_VOWEL_MATRA_LT", matra_lt),
        "",
        "/* 短 → 長（RS → の post-shift）。{短, 長} の対。 */",
        f"static const unsigned short DEVA_VOWEL_LONG[{len(vlong)}][2] = {{"]
for a, b in vlong:
    out.append(f"    {{ {cp(a)}, {cp(b)} }},")
out += ["};", f"static const unsigned short DEVA_MATRA_LONG[{len(mlong)}][2] = {{"]
for a, b in mlong:
    out.append(f"    {{ {cp(a)}, {cp(b)} }},")
out += ["};", "",
        "/* candra 形（LT + RS↑ の post-shift）。{もとの母音, candra 形} の対。",
        "   ★長短の対と同じ「後から差し替える」形 —— candra は母音記号を**置換**する",
        "     操作で、anusvara のように末尾へ足すのではない。 */",
        f"static const unsigned short DEVA_VOWEL_CANDRA[{len(vcandra)}][2] = {{"]
for a, b in vcandra:
    out.append(f"    {{ {cp(a)}, {cp(b)} }},")
out += ["};", f"static const unsigned short DEVA_MATRA_CANDRA[{len(mcandra)}][2] = {{"]
for a, b in mcandra:
    out.append(f"    {{ {cp(a)}, {cp(b)} }},")
out += ["};", "", "#endif", ""]

Path("devanagari_tables.h").write_text("\n".join(out))
print(f"OK: devanagari_tables.h（varga {len(vargas)}×{len(vargas[0])} / "
      f"非 varga {len(semi)}+{len(sib)} / 母音 {sum(1 for c in indep if c)} / "
      f"長短の対 {len(vlong)}+{len(mlong)} / candra {len(vcandra)}+{len(mcandra)}）")
