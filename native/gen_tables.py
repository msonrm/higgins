#!/usr/bin/env python3
"""labo の gamepad-kana-table.ts から入力テーブルを抽出して tables.h を生成。

手写しの転記事故を避けるため、正典（TS）を直接パースする。

    python3 gen_tables.py [gamepad-kana-table.ts のパス]

★★**Zenmai の gen_tables.py を借りてはいけない。** あちらは同じ名前で同じ正典を
  読むが、**Zork 用の「PS1 適応」を 3 つ施している** —— や行の「」を空枠にし、
  わ行を わ/を/ん/ー に組み替え、う→ゔ を落とす。Higgins は自由文を打つので
  どれも要らない（実機で msonrm さんに指摘された・2026-08-31）。

★**生成物を pin するという判断が裏目に出た例。** `tools/vendor_shell.sh` で
  Zenmai の `tables.h` を取り込んでいたが、それは**Zenmai 用に歪めた表**だった。
  ★「上流の生成物」と「上流が加工した生成物」は違う ——
  **同じ正典から自分で焼く**のが正しい。
"""
import re
import sys
from pathlib import Path

DEFAULT = Path.home() / "development/logical-layout-labo/web/src/engine/gamepad-kana-table.ts"
path = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT
if not path.exists():
    sys.exit(f"正典が見つからない: {path}")
src = path.read_text()


def block(name):
    m = re.search(name + r'[^=]*=[^[(]*[\[(]', src)
    depth, i = 1, m.end()
    while depth:
        if src[i] in '[(':
            depth += 1
        elif src[i] in '])':
            depth -= 1
        i += 1
    return src[m.end():i - 1]


def table10x5(name):
    rows = re.findall(r'\[((?:\s*"[^"]*",?\s*)+)\]', block(name))
    out = []
    for r in rows:
        cells = re.findall(r'"([^"]*)"', r)
        assert len(cells) == 5, cells
        out.append(cells)
    assert len(out) == 10, name
    return out


def pairs(name):
    return re.findall(r'\["([^"]+)",\s*"([^"]+)"\]', block(name))


kana = table10x5('KANA_TABLE')
eng = table10x5('ENGLISH_TABLE')
youon = pairs('YOUON_POSTSHIFT_MAP')
daku = pairs('DAKUTEN_MAP')
handaku = pairs('HANDAKUTEN_MAP')

# ★**Zenmai が手を入れていた 3 箇所は、Higgins ではそのまま使う。**
#   正典の側が変わったことに気づけるよう、中身だけ確かめておく
#   （黙って別の表が焼かれるのを防ぐ。★assert が落ちたら正典を見に行くこと）。
assert kana[7] == ['や', '「', 'ゆ', '」', 'よ'], kana[7]      # や行に かぎ括弧
assert kana[9] == ['わ', 'ゐ', '？', 'ゑ', 'を'], kana[9]      # わ行に ？

# ★**Higgins 適応はこれ 1 つだけ**（実機フィードバック・2026-08-31）——
#   わ行を**スマホのフリック入力の並び**にする: 中央=わ / 左=を / 上=ん / 右=ー。
#   ★理由は**指が覚えている**こと。Zenmai（PS1 版）で同じ配置を使ってきたので、
#     正典どおりの わ/ゐ/？/ゑ/を に戻すと打ち間違える（実機で「マッスルメモリーが
#     Zork のものになっていて違和感がある」と言われた）。
#   ★ゐ / ゑ は現代語で使わないので、置き換えて失うものが無い。
#   ★**Zenmai との違いは下（お段）に「？」を残すこと** —— 自由文には要る
#     （Zenmai はコマンド入力なので空枠にしていた）。フリックの下は「〜」だが、
#     こちらは疑問文を打つほうが多い。
#   ★★Zenmai の「PS1 適応」を外したのに Higgins の適応を入れることになるが、
#     理由が違う —— あちらは**作品（Zork）の都合**、こちらは**指の都合**。
kana[9] = ['わ', 'を', 'ん', 'ー', '？']
assert ('う', 'ゔ') in daku                                    # ゔ は Noto にある


def u(c):
    return f'0x{ord(c):04X}'


with open(Path(__file__).parent / 'tables.h', 'w') as f:
    f.write('/* gen_tables.py が labo の gamepad-kana-table.ts から生成。手で編集しない。\n')
    f.write('   ★Zenmai の tables.h とは違う —— あちらは Zork 用に や行の「」を潰し、\n')
    f.write('   わ行を組み替え、ゔ を落としてある。こちらは正典そのまま。 */\n')
    f.write('static const unsigned short GP_KANA[10][5] = {\n')
    for r in kana:
        f.write('  {' + ','.join(u(c) if c else '0' for c in r) + '},\n')
    f.write('};\n')
    f.write('static const unsigned short GP_ENG[10][5] = {\n')
    for r in eng:
        f.write('  {' + ','.join(u(c) if c else '0' for c in r) + '},\n')
    f.write('};\n')
    f.write('typedef struct { unsigned short from; unsigned short to[2]; } GpYouon;\n')
    f.write(f'enum {{ GP_YOUON_N = {len(youon)}, GP_DAKU_N = {len(daku)}, GP_HANDAKU_N = {len(handaku)} }};\n')
    f.write('static const GpYouon GP_YOUON[GP_YOUON_N] = {\n')
    for a, b in youon:
        to = [u(c) for c in b] + (['0'] if len(b) == 1 else [])
        f.write(f'  {{{u(a)}, {{{",".join(to)}}}}},\n')
    f.write('};\n')
    f.write('typedef struct { unsigned short sei, on; } GpAB;\n')
    f.write('static const GpAB GP_DAKU[GP_DAKU_N] = {\n')
    for a, b in daku:
        f.write(f'  {{{u(a)}, {u(b)}}},\n')
    f.write('};\n')
    f.write('static const GpAB GP_HANDAKU[GP_HANDAKU_N] = {\n')
    for a, b in handaku:
        f.write(f'  {{{u(a)}, {u(b)}}},\n')
    f.write('};\n')

print(f'tables.h: kana 10x5 / eng 10x5 / youon {len(youon)} / daku {len(daku)} / handaku {len(handaku)}')
