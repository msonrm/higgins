# Higgins

**ELIZA (1966), run faithfully — then taught to speak four languages, on machines with no keyboard.**

**1966 年の ELIZA を忠実に走らせ、4 言語を教え込み、キーボードの無い機械で打つ。**

> **All four now run on a handheld with no keyboard: Japanese, English (the 1966
> original), Korean and Hindi. Hindi was the last and the most expensive — not for the
> font or the shaping, but because Devanagari needs an input method, and because Hindi
> verbs agree with their subject, so reflecting "I" into "you" is not a closed list.**
>
> **4 つとも動く** —— 日本語・英語（1966 年の原典）・韓国語・ヒンディー語が、
> キーボードの無いハンドヘルド（PortMaster）で打てる。★**いちばん高くついたのは
> ヒンディー語**で、理由はフォントでも shaping でもなく、**デーヴァナーガリーの
> 入力方式**と、**動詞が主語に一致する**こと（＝「私」→「あなた」の反転が
> 閉じた表では書き切れない）だった。

## What it is / これは何か

Joseph Weizenbaum's ELIZA is not a chatbot — it is a **script interpreter**. `DOCTOR` is
merely the most famous script written for it. This project takes that seriously:

1. run the **original 1966 DOCTOR** exactly as published, and
2. write DOCTOR scripts for **Japanese, Korean, English and Hindi**, and
3. run all four on **single-board handhelds with no keyboard**, typed with a D-pad and four buttons.

ELIZA は「チャットボット」ではなく**スクリプト解釈器**で、`DOCTOR` はそこに載せた
最も有名なスクリプトにすぎない。この企画はそれを額面どおりに受け取る ——
**(1)** 1966 年の DOCTOR を発表されたとおりに走らせ、**(2)** 日本語・韓国語・英語・
ヒンディー語の DOCTOR を書き、**(3)** その 4 つを**キーボードの無いハンドヘルド**で、
十字キーと 4 ボタンだけで打つ。

## Build / ビルド

```sh
make        # -> build/eliza
make test   # ★「検査が生きていること」まで確かめる
```

`make test` checks two things, not one: that the tests pass, **and that a deliberately
broken test still turns red**. The upstream test runner prints nothing on success, so
silence must never be mistaken for green.

`make test` は 2 つ確かめる —— テストが通ること、**および、わざと壊したテストが
ちゃんと赤になること**。upstream の検査は成功時に何も出力しないので、
★**無言を緑と取り違えない**ようにするため。

### Browser (gamepad only) / ブラウザ版（コントローラ専用）

```sh
sh wasm/build.sh     # -> wasm/out/higgins.{js,wasm}  (needs emcc)
sh wasm/dist.sh      # -> wasm/dist/  (static files for Cloudflare Pages etc.)
make test-wasm       # same 40 scripts as native/shots.sh, compared by md5
```

The browser build is **the same C as the handheld build**, compiled with Emscripten.
Only the two pieces that touch the machine are swapped: `wasm/plat_web.c` (canvas +
Gamepad API, in place of SDL2) and `wasm/hechima_web.c` (calls the separate
`hechima-wasm` module, in place of `libhechima.so`). It reads **only a game controller**
— no keyboard, no touch.

ブラウザ版は**ハンドヘルド版と同じ C** を Emscripten で焼いたもの。差し替えたのは
機械に触る 2 本だけ —— `wasm/plat_web.c`（SDL2 の代わりに canvas と Gamepad API）と
`wasm/hechima_web.c`（`libhechima.so` の代わりに別モジュールの `hechima-wasm` を呼ぶ）。
★**読むのはゲームコントローラだけ**（キーボード・タッチは読まない）。
★`make test-wasm` が台本 40 本で native と**画素まで同じ絵**を出すことを確かめる
（native の絵はシステムの FreeType の版で変わるので、`make test` には入れていない）。

### Browser, drawn in HTML / ブラウザ版（HTML で描く・`webui/`）

```sh
sh webui/build.sh    # -> webui/out/core.{js,wasm}  (needs emcc; no FreeType / HarfBuzz)
sh webui/dist.sh     # -> webui/dist/
node webui/run.mjs   # feeds native/*.script to the core and prints what was said
npx wrangler pages deploy webui/dist --project-name higgins --branch main   # → https://higgins.pages.dev/
```

Same C core for typing, composing, conversion and ELIZA (`webui/core.c` is a copy of
the input half of `native/main.c`), but **the screen is HTML/CSS** in the look of
Zenmai's web version. It does **not** track the native build any more.

打鍵・合成・変換・ELIZA は同じ C（`webui/core.c` は `native/main.c` の入力側の複製）で、
**画面だけを HTML / CSS で描く**（見た目は Zenmai の Web 版から）。★native とは同期しない。
入力欄の中の行移動と「文字入力方法」の図は無い（操作図とライセンスはある）。

開発中は、リポジトリの直下を配って `wasm/index.html`（または `webui/index.html`）を開く
（例: `python3 -m http.server` → `http://localhost:8000/wasm/`）。
★Gamepad API は **https か localhost でしか動かない**。

## Name / 名前について

Henry Higgins is the phonetician who teaches Eliza Doolittle to speak, in Shaw's
*Pygmalion* (1913) — the play Weizenbaum took the name ELIZA from. Writing the scripts
is the teaching side of that arrangement, which is what this repository is for.

**This project does not use ELIZA as its own name or brand.**

ヘンリー・ヒギンズは、Shaw『ピグマリオン』(1913) でイライザ・ドゥーリトルに
喋り方を教える音声学者。Weizenbaum が ELIZA の名を採ったのがこの戯曲で、
**スクリプトを書くのは「教える側」の仕事**にあたる。

★**ELIZA の名は、この企画の名称・ブランドには用いない。**

## License / ライセンス

MIT for the code in this repository.

`vendor/eliza/` is Anthony Hay's ELIZA implementation under **CC0 1.0**. The original
ELIZA source (MAD-SLIP) and the DOCTOR script it carries were found in the MIT archives
in 2021 and released as **CC0 with the permission of Weizenbaum's estate** — which is
why the original script can be shipped here rather than left for you to find.
Provenance in [`vendor/VENDOR.md`](vendor/VENDOR.md).

本リポジトリのコードは MIT。`vendor/eliza/` は Anthony Hay による ELIZA 実装で
**CC0 1.0**。同梱の DOCTOR は、2021 年に MIT のアーカイブで見つかった原典が
**遺族の許諾のもと CC0 で公開された**もの ★**だから同梱できる**（「各自で用意」に
しなくてよい）。出自は [`vendor/VENDOR.md`](vendor/VENDOR.md)。
