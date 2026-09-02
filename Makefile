CXX      ?= g++
# ★-w（警告抑制）は vendor のコードだから。upstream は書き換えない方針なので、
#   -Wall を付けても直せない警告で出力が埋まるだけになる。
#   自前の C++ を足すときは、そちらのターゲットだけ -Wall -Wextra に分けること。
CXXFLAGS ?= -std=c++20 -O1 -w

BUILD     := build
ELIZA_SRC := vendor/eliza/src/eliza.cpp

.PHONY: all test test-ja test-ja-doctor test-ja-alias test-ko-doctor test-ko-coverage test-web test-api test-native clean

all: $(BUILD)/eliza

$(BUILD)/eliza: $(ELIZA_SRC)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $<

# ★eliza.cpp の run_tests() は「失敗したときしか何も出力しない」。
#   つまり無言が緑に見えるので、テストが 1 件も登録されていなくても気づけない。
#   そこで 2 つ焼いて、両方の結果が揃ってはじめて緑とする:
#     eliza-count  … 本番と同じロジック + 件数表示   → 「1 件以上あって 0 失敗」
#     eliza-canary … それにわざと落ちるテストを 1 個 → 「1 失敗」（検査が生きている証拠）
test: $(BUILD)/eliza-count $(BUILD)/eliza-canary test-ja test-ja-doctor test-ja-alias test-ko-doctor test-ko-coverage test-web test-api test-native
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

# ★別名のゴールデン（ママ / パパ / さびしい / 楽しい …）。**ja_doctor とは別に持つ** ——
#   あちらは会話の流れを見るもので、代表形（母 / 悲しい）しか通らないため、
#   「別名から飛んだ先で分解規則が合わない」という穴を 1 つも踏まない（実際に踏んでいなかった）。
#
# ★★**CLI と native を同じ期待値に突き合わせる**のが肝。両者は組み込みエラー
#   メッセージの設定が違う（CLI = ON / native = OFF）ので、`HMMM` などが 1 つでも
#   出れば必ず差分になる。★つまりこの 1 本が「英語が画面に出ていないこと」の検査を兼ねる。
test-ja-alias: $(BUILD)/eliza
	@python3 tools/wakachi.py doctor/ja.txt < experiments/ja_alias.in \
	  | $(BUILD)/eliza --nobanner doctor/ja.txt \
	  | python3 tools/tsunagi.py \
	  | diff -u experiments/ja_alias.out - > /dev/null \
	  || { echo 'FAIL: 別名ゴールデンが一致しない'; exit 1; }
	@if pkg-config --exists sdl2 freetype2 2>/dev/null; then \
	    sh native/build.sh > /dev/null; \
	    ./native/test_reply < experiments/ja_alias.in | grep -v '^$$' > $(BUILD)/alias-native.out; \
	    grep -v '^$$' experiments/ja_alias.out | diff -u - $(BUILD)/alias-native.out > /dev/null \
	      || { echo 'FAIL: native が CLI と違う = 組み込みの英語メッセージが出ている疑い'; \
	           grep -v '^$$' experiments/ja_alias.out | diff -u - $(BUILD)/alias-native.out | head -20; \
	           exit 1; }; \
	fi
	@echo 'OK: 別名ゴールデン一致（組み込みの英語メッセージも出ていない）'

# ★韓国語 DOCTOR のゴールデン。josa（조사・어미 を剥がす）→ eliza → tsunagi_ko（繋ぐ）。
#   ★日本語と道具が違うのは、**韓国語には既に띄어쓰기がある**から ——
#   分かち書きではなく「語にくっついた 조사・어미 を剥がす」だけでよい。
test-ko-doctor: $(BUILD)/eliza
	@python3 tools/josa.py doctor/ko.txt < experiments/ko_doctor.in \
	  | $(BUILD)/eliza --nobanner doctor/ko.txt \
	  | python3 tools/tsunagi_ko.py \
	  | diff -u experiments/ko_doctor.out - > /dev/null \
	  || { echo 'FAIL: 韓国語 DOCTOR のゴールデンが一致しない'; exit 1; }
	@echo 'OK: 韓国語 DOCTOR ゴールデン一致'

# ★★別名の穴を**機械で**探す（日本語で 13 件出た形）。
#   台本ではなく**スクリプトからキーワードを取り出して全件叩く**ので、
#   規則を増やしても検査が置いていかれない。
#   ★判定は「応答にラテン文字が出たら失敗」—— スクリプトは 100% 韓国語なので、
#     英字が出るのはエンジンの組み込みメッセージが漏れたときだけ。
#   ★実際に 8 件（제가 / 내가 の 4 形ずつ）を初回で掘り当てた。
test-ko-coverage: $(BUILD)/eliza
	@python3 tools/coverage_ko.py doctor/ko.txt

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
#   ★**入力（合成）もここで見る** —— native/test_input は SDL を開かずに
#      gp_step + ko_apply を走らせる。★`input.h` は前から「ゴールデンを test_input で
#      照合する」と書いていたが、**その test_input は存在しなかった**（2026-09-02）。
#   ★★**韓国語も同じ縛りに入れる**（josa / tsunagi_ko は Python と C の 2 実装）。
#      規則の表は Python から焼くので（tools/gen_josa.py）、ずれるとしたら手順の側。
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
	    ./native/test_reply ko < experiments/ko_doctor.in | grep -v '^$$' > $(BUILD)/native-ko.out; \
	    grep -v '^$$' experiments/ko_doctor.out | diff -u - $(BUILD)/native-ko.out > /dev/null \
	      || { echo 'FAIL: native の韓国語が CLI 版と違う応答を返す'; \
	           grep -v '^$$' experiments/ko_doctor.out | diff -u - $(BUILD)/native-ko.out | head -20; \
	           exit 1; }; \
	    ./native/test_input > $(BUILD)/ko-input.out; \
	    diff -u experiments/ko_input.out $(BUILD)/ko-input.out > /dev/null \
	      || { echo 'FAIL: 韓国語の入力（合成）が期待と違う'; \
	           diff -u experiments/ko_input.out $(BUILD)/ko-input.out | head -20; exit 1; }; \
	    echo 'OK: native 版と CLI 版の応答が一致（日本語・韓国語）/ 韓国語の合成も一致'; \
	fi

clean:
	rm -rf $(BUILD)
