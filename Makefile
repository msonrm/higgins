CXX      ?= g++
# ★-w（警告抑制）は vendor のコードだから。upstream は書き換えない方針なので、
#   -Wall を付けても直せない警告で出力が埋まるだけになる。
#   自前の C++ を足すときは、そちらのターゲットだけ -Wall -Wextra に分けること。
CXXFLAGS ?= -std=c++20 -O1 -w

BUILD     := build
ELIZA_SRC := vendor/eliza/src/eliza.cpp

.PHONY: all test test-ja test-ja-doctor test-web test-api test-native clean

all: $(BUILD)/eliza

$(BUILD)/eliza: $(ELIZA_SRC)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $<

# ★eliza.cpp の run_tests() は「失敗したときしか何も出力しない」。
#   つまり無言が緑に見えるので、テストが 1 件も登録されていなくても気づけない。
#   そこで 2 つ焼いて、両方の結果が揃ってはじめて緑とする:
#     eliza-count  … 本番と同じロジック + 件数表示   → 「1 件以上あって 0 失敗」
#     eliza-canary … それにわざと落ちるテストを 1 個 → 「1 失敗」（検査が生きている証拠）
test: $(BUILD)/eliza-count $(BUILD)/eliza-canary test-ja test-ja-doctor test-web test-api test-native
	@printf '\n' | $(BUILD)/eliza-count | grep -qE '[1-9][0-9]* assertions' \
	  || { echo 'FAIL: テストが 1 件も走っていない'; exit 1; }
	@printf '\n' | $(BUILD)/eliza-count | grep -q '0 failures' \
	  || { echo 'FAIL: 本番ビルドでテストが落ちている'; exit 1; }
	@printf '\n' | $(BUILD)/eliza-canary | grep -q '1 failures' \
	  || { echo 'FAIL: カナリアが赤にならない = 検査そのものが死んでいる'; exit 1; }
	@printf '\n' | $(BUILD)/eliza-count | grep '\[count\]'
	@echo 'OK: 検査は生きていて、本番は緑'

$(BUILD)/eliza-count: $(ELIZA_SRC) tools/patch_tests.py
	@mkdir -p $(BUILD)
	python3 tools/patch_tests.py --mode count $< $(BUILD)/eliza-count.cpp
	$(CXX) $(CXXFLAGS) -o $@ $(BUILD)/eliza-count.cpp

$(BUILD)/eliza-canary: $(ELIZA_SRC) tools/patch_tests.py
	@mkdir -p $(BUILD)
	python3 tools/patch_tests.py --mode canary $< $(BUILD)/eliza-canary.cpp
	$(CXX) $(CXXFLAGS) -o $@ $(BUILD)/eliza-canary.cpp

# ★日本語のゴールデン。仮説（主語を省けば置換なしで視点が反転する）の実証は
#   済んでいるので、ここから先は**回帰の基準線**として使う ——
#   分かち書き（split_user_input）を入れたあと、この挙動が保たれているか。
#   期待値は実測を焼いたものだが、なぜ正しいかは experiments/README.md にある。
test-ja: $(BUILD)/eliza
	@python3 tools/wakachi.py experiments/ja_hypothesis.txt \
	    < experiments/ja_hypothesis.in \
	  | $(BUILD)/eliza --nobanner experiments/ja_hypothesis.txt \
	  | diff -u experiments/ja_hypothesis.out - > /dev/null \
	  || { echo 'FAIL: 日本語ゴールデンが一致しない'; exit 1; }
	@echo 'OK: 日本語ゴールデン一致'

# ★日本語 DOCTOR のゴールデン。wakachi（分かち書き）→ eliza → tsunagi（繋ぎ）の
#   3 段を通した会話を丸ごと突き合わせる。訳を直すたびにここで差分が見える。
test-ja-doctor: $(BUILD)/eliza
	@python3 tools/wakachi.py doctor/ja.txt < experiments/ja_doctor.in \
	  | $(BUILD)/eliza --nobanner doctor/ja.txt \
	  | python3 tools/tsunagi.py \
	  | diff -u experiments/ja_doctor.out - > /dev/null \
	  || { echo 'FAIL: 日本語 DOCTOR のゴールデンが一致しない'; exit 1; }
	@echo 'OK: 日本語 DOCTOR ゴールデン一致'

# ★web/ の JS 3 本を Node で動かし、C++ 版と同じ応答が出るかを見る。
#   ブラウザを開かずに Web 版の回帰を守るため。★独立した 2 実装が一致することは
#   スクリプトの解釈が実装依存でないことの証拠にもなる。
test-web:
	@command -v node > /dev/null || { echo 'SKIP: node が無い'; exit 0; }
	@mkdir -p $(BUILD)
	@node tools/web_test.mjs | grep -v '^$$' > $(BUILD)/web.out
	@grep -v '^$$' experiments/ja_doctor.out | diff -u - $(BUILD)/web.out > /dev/null \
	  || { echo 'FAIL: Web 版（JS）が C++ 版と違う応答を返す'; \
	       grep -v '^$$' experiments/ja_doctor.out | diff -u - $(BUILD)/web.out | head -20; exit 1; }
	@echo 'OK: Web 版と C++ 版の応答が一致'

# ★index.html が呼ぶ hechima / keymap-engine の API 名が実在するかを見る。
#   名前を間違えると**沈黙して動かない**（worker のロード失敗も沈黙する）ので、
#   vendoring を差し替えたときに API 変更をここで捕まえる。
#   ※ worker と wasm の実行は Node では確かめられない。そこは実機（ブラウザ）で見る。
test-api:
	@command -v node > /dev/null || { echo 'SKIP: node が無い'; exit 0; }
	@node tools/api_check.mjs > /dev/null \
	  || { echo 'FAIL: hechima / keymap-engine の API 名が合わない'; node tools/api_check.mjs; exit 1; }
	@echo 'OK: hechima / keymap-engine の API 名は実在する'

# ★native（PortMaster 版）の応答が CLI 版と一致するか。
#   実機に載っているのと**同じ** wakachi / tsunagi / eliza_bridge を、SDL を開かずに
#   走らせる（native/test_reply）。
#   ★これが段階 B の芯 —— wakachi / tsunagi は Python・JS・C の **3 実装**になったので、
#   片方の経路でしか通らない値は、もう片方で必ず落ちる。3 つをここで縛る。
#   ※ SDL / FreeType が無い環境では飛ばす（1 つのシェルで書くこと ―― make は行ごとに
#      別シェルなので、`|| exit 0` では次の行が止まらない）。
test-native:
	@if ! pkg-config --exists sdl2 freetype2 2>/dev/null; then \
	    echo 'SKIP: sdl2 / freetype2 が無い'; \
	else \
	    mkdir -p $(BUILD); \
	    sh native/build.sh > /dev/null; \
	    ./native/test_reply < experiments/ja_doctor.in | grep -v '^$$' > $(BUILD)/native.out; \
	    grep -v '^$$' experiments/ja_doctor.out | diff -u - $(BUILD)/native.out > /dev/null \
	      || { echo 'FAIL: native 版が CLI 版と違う応答を返す'; \
	           grep -v '^$$' experiments/ja_doctor.out | diff -u - $(BUILD)/native.out | head -20; \
	           exit 1; }; \
	    echo 'OK: native 版と CLI 版の応答が一致'; \
	fi

clean:
	rm -rf $(BUILD)
