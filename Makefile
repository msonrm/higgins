CXX      ?= g++
# ★-w（警告抑制）は vendor のコードだから。upstream は書き換えない方針なので、
#   -Wall を付けても直せない警告で出力が埋まるだけになる。
#   自前の C++ を足すときは、そちらのターゲットだけ -Wall -Wextra に分けること。
CXXFLAGS ?= -std=c++20 -O1 -w

BUILD     := build
ELIZA_SRC := vendor/eliza/src/eliza.cpp

.PHONY: all test clean

all: $(BUILD)/eliza

$(BUILD)/eliza: $(ELIZA_SRC)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $<

# ★eliza.cpp の run_tests() は「失敗したときしか何も出力しない」。
#   つまり無言が緑に見えるので、テストが 1 件も登録されていなくても気づけない。
#   そこで 2 つ焼いて、両方の結果が揃ってはじめて緑とする:
#     eliza-count  … 本番と同じロジック + 件数表示   → 「1 件以上あって 0 失敗」
#     eliza-canary … それにわざと落ちるテストを 1 個 → 「1 失敗」（検査が生きている証拠）
test: $(BUILD)/eliza-count $(BUILD)/eliza-canary
	@printf '\n' | $(BUILD)/eliza-count | grep -qE '[1-9][0-9]* assertions' \
	  || { echo 'FAIL: テストが 1 件も走っていない'; exit 1; }
	@printf '\n' | $(BUILD)/eliza-count | grep -q '0 failures' \
	  || { echo 'FAIL: 本番ビルドでテストが落ちている'; exit 1; }
	@printf '\n' | $(BUILD)/eliza-canary | grep -q '1 failures' \
	  || { echo 'FAIL: カナリアが赤にならない = 検査そのものが死んでいる'; exit 1; }
	@printf '\n' | $(BUILD)/eliza-count | grep '\[count\]'
	@echo 'OK: 検査は生きていて、本番は緑'

$(BUILD)/eliza-count: $(ELIZA_SRC) scripts/patch_tests.py
	@mkdir -p $(BUILD)
	python3 scripts/patch_tests.py --mode count $< $(BUILD)/eliza-count.cpp
	$(CXX) $(CXXFLAGS) -o $@ $(BUILD)/eliza-count.cpp

$(BUILD)/eliza-canary: $(ELIZA_SRC) scripts/patch_tests.py
	@mkdir -p $(BUILD)
	python3 scripts/patch_tests.py --mode canary $< $(BUILD)/eliza-canary.cpp
	$(CXX) $(CXXFLAGS) -o $@ $(BUILD)/eliza-canary.cpp

clean:
	rm -rf $(BUILD)
