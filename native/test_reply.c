/* ★実機に載っているのと同じ wakachi / tsunagi / eliza_bridge を、SDL を開かずに走らせる。
 *   Zenmai の cmd_test_host.c と同じ手 —— 画面を出さずに、**通る道だけ**を検査する。
 *
 *     ./test_reply < experiments/ja_doctor.in     日本語
 *     ./test_reply ko < experiments/ko_doctor.in  韓国語
 *     ./test_reply hi < experiments/hi_doctor.in  ヒンディー語
 *     1 行目 = 挨拶、以降 1 行 1 応答。
 *
 * ★これが段階 B の芯。wakachi / tsunagi は Python・JS・C の 3 実装になったので、
 *   **3 つが同じ応答を返す**ことをゴールデン（experiments/ja_doctor.out）で縛る。
 *   片方の経路でしか通らない値は、もう片方で必ず落ちる。
 */
#include <stdio.h>
#include <string.h>

#include "eliza_bridge.h"
#include "hindi.h"
#include "josa.h"
#include "text.h"
#include "tsunagi.h"
#include "tsunagi_ko.h"
#include "wakachi.h"

extern const char _binary_script_txt_start[];
extern const char _binary_script_txt_end[];
extern const char _binary_script_ko_txt_start[];
extern const char _binary_script_ko_txt_end[];
extern const char _binary_script_hi_txt_start[];
extern const char _binary_script_hi_txt_end[];

/* ★言語で入口と出口が替わる。中（ELIZA）は同じもの。
   ★★ヒンディー語は**出口が無い**（tsunagi が Devanagari を素通しするので
     日本語と同じ道でよい）。入口だけが替わる。 */
static int ko, hi;

static void print_reply(const char *utf8)
{
    uint16_t out[4096];
    char buf[8192];
    const int max = (int)(sizeof out / sizeof *out);
    const int n = ko ? tsunagi_ko(utf8, out, max) : tsunagi(utf8, out, max);
    u16_to_u8(out, n, buf, (int)sizeof buf);
    printf("%s\n", buf);
}

int main(int argc, char **argv)
{
    /* ★引数で席を選ぶ（既定 = 日本語）。CLI 版と同じ応答が出るかを言語ごとに見る。 */
    ko = (argc > 1 && argv[1][0] == 'k');
    hi = (argc > 1 && argv[1][0] == 'h');
    const int slot = ko ? HG_KO : hi ? HG_HI : HG_JA;
    const char *start = ko ? _binary_script_ko_txt_start
                       : hi ? _binary_script_hi_txt_start : _binary_script_txt_start;
    const char *end = ko ? _binary_script_ko_txt_end
                     : hi ? _binary_script_hi_txt_end : _binary_script_txt_end;
    if (!hg_eliza_init(slot, start, (int)(end - start), 0)) {   /* 0 = 本体と同じ（main.c の注記） */
        fprintf(stderr, "スクリプトが読めない\n");
        return 1;
    }
    print_reply(hg_eliza_greeting(slot));

    char line[4096];
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0])
            continue;
        uint16_t in16[2048];
        char wa[8192];
        const int n = u8_to_u16(line, -1, in16, (int)(sizeof in16 / sizeof *in16));
        if (ko)
            josa(in16, n, wa, (int)sizeof wa);
        else if (hi)
            hindi_in(in16, n, wa, (int)sizeof wa);
        else
            wakachi(in16, n, wa, (int)sizeof wa);
        print_reply(hg_eliza_response(slot, wa));
    }
    return 0;
}
