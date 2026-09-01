/* ★実機に載っているのと同じ wakachi / tsunagi / eliza_bridge を、SDL を開かずに走らせる。
 *   Zenmai の cmd_test_host.c と同じ手 —— 画面を出さずに、**通る道だけ**を検査する。
 *
 *     ./test_reply < experiments/ja_doctor.in
 *     1 行目 = 挨拶、以降 1 行 1 応答。
 *
 * ★これが段階 B の芯。wakachi / tsunagi は Python・JS・C の 3 実装になったので、
 *   **3 つが同じ応答を返す**ことをゴールデン（experiments/ja_doctor.out）で縛る。
 *   片方の経路でしか通らない値は、もう片方で必ず落ちる。
 */
#include <stdio.h>
#include <string.h>

#include "eliza_bridge.h"
#include "text.h"
#include "tsunagi.h"
#include "wakachi.h"

extern const char _binary_script_txt_start[];
extern const char _binary_script_txt_end[];

static void print_reply(const char *utf8)
{
    uint16_t out[4096];
    char buf[8192];
    const int n = tsunagi(utf8, out, (int)(sizeof out / sizeof *out));
    u16_to_u8(out, n, buf, (int)sizeof buf);
    printf("%s\n", buf);
}

int main(void)
{
    const int len = (int)(_binary_script_txt_end - _binary_script_txt_start);
    if (!hg_eliza_init(_binary_script_txt_start, len, 0)) {   /* 0 = 本体と同じ（main.c の注記） */
        fprintf(stderr, "スクリプトが読めない\n");
        return 1;
    }
    print_reply(hg_eliza_greeting());

    char line[4096];
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0])
            continue;
        uint16_t in16[2048];
        char wa[8192];
        const int n = u8_to_u16(line, -1, in16, (int)(sizeof in16 / sizeof *in16));
        wakachi(in16, n, wa, (int)sizeof wa);
        print_reply(hg_eliza_response(wa));
    }
    return 0;
}
