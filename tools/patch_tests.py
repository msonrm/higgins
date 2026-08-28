#!/usr/bin/env python3
"""eliza.cpp から検査用の変種を焼く。

upstream の run_tests() は **失敗したときしか何も出力しない**。
つまり無言が緑に見えるので、テストが 1 件も登録されていなくても気づけない
（起動時に走るので、走っていないことにも気づけない）。それを潰すために 2 つ作る:

  --mode count   本番と同じロジックのまま、件数だけ表示する
  --mode canary  count に加えて、わざと落ちるテストを 1 個足す

canary が赤にならなければ、検査そのものが死んでいる。
upstream は書き換えない（vendor は clone したまま保つ）。
"""

import argparse
import io
import sys

RUN_TESTS_OLD = r'''void run_tests()
{
    for (auto & t : test_routines)
        t();
    if (fault_count)
        std::cout << fault_count << " total failures\n";
}'''

RUN_TESTS_NEW = r'''void run_tests()
{
    for (auto & t : test_routines)
        t();
    std::cout << "[count] " << test_routines.size() << " test funcs, "
              << test_count << " assertions, " << fault_count << " failures\n";
    if (fault_count)
        std::cout << fault_count << " total failures\n";
}'''

NS_END = "} //namespace micro_test_library"

CANARY = """

// ★カナリア: わざと落ちるテスト。これが赤にならなければ検査が死んでいる。
DEF_TEST_FUNC(canary_deliberate_failure)
{
    TEST_EQUAL(1, 2);
}
"""


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--mode", choices=("count", "canary"), required=True)
    ap.add_argument("src")
    ap.add_argument("dst")
    args = ap.parse_args()

    src = io.open(args.src, encoding="utf-8").read()

    if RUN_TESTS_OLD not in src:
        sys.exit("patch_tests.py: run_tests() が見つからない。upstream が変わった可能性がある")
    out = src.replace(RUN_TESTS_OLD, RUN_TESTS_NEW, 1)

    if args.mode == "canary":
        if NS_END not in out:
            sys.exit("patch_tests.py: micro_test_library の終端が見つからない")
        out = out.replace(NS_END, NS_END + CANARY, 1)

    io.open(args.dst, "w", encoding="utf-8").write(out)


if __name__ == "__main__":
    main()
