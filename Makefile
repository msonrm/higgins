CXX      ?= g++
# ★-w（警告抑制）は vendor のコードだから。upstream は書き換えない方針なので、
#   -Wall を付けても直せない警告で出力が埋まるだけになる。
#   自前の C++ を足すときは、そちらのターゲットだけ -Wall -Wextra に分けること。
CXXFLAGS ?= -std=c++20 -O1 -w

BUILD     := build
ELIZA_SRC := vendor/eliza/src/eliza.cpp

.PHONY: all test test-ja test-ja-doctor clean

all: $(BUILD)/eliza

$(BUILD)/eliza: $(ELIZA_SRC)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $<

# ★eliza.cpp の run_tests() は「失敗したときしか何も出力しない」。
#   つまり無言が緑に見えるので、テストが 1 件も登録されていなくても気づけない。
#   そこで 2 つ焼いて、両方の結果が揃ってはじめて緑とする:
#     eliza-count  … 本番と同じロジック + 件数表示   → 「1 件以上あって 0 失敗」
#     eliza-canary … それにわざと落ちるテストを 1 個 → 「1 失敗」（検査が生きている証拠）
test: $(BUILD)/eliza-count $(BUILD)/eliza-canary test-ja test-ja-doctor
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

clean:
	rm -rf $(BUILD)
