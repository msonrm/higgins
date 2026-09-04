/* 画面に出す文言 —— ★ここ 1 か所に集める。
 *
 * ★★**添字を持つ形にしてあるが、いまは日本語 1 つだけ**（`UI_LANG_N == 1`）。
 *   切り替えの器を先に作っておくと、訳が揃ったときに**添字を動かすだけ**で済む。
 *   ★逆に、文言を main.c の中へ直に書くと「どこに何があるか」を数え上げる形になり、
 *   いつか 1 つ取り残す（Zenmai が `ui_data.h` に集めているのと同じ判断）。
 *
 * ★★**席（話す相手）と UI 言語は別の軸。** ヒンディー語で打ちたい人が説明は
 *   日本語で読みたい、ということが普通に起きる —— ★**初めての席ほど説明が要るのに、
 *   席に追従させると、その説明がいちばん読めない言語で出る**。だから混ぜない。
 *
 * ★**ここに出る字は全部こちらのもの**で、DOCTOR の応答は 1 つも通らない
 *   （Zenmai の ui_data.h と同じ切り分け ―― 「その文字列は誰のものか」）。
 * ★**同梱フォントに無い字は黙って空白で描かれる。** 記号を足すときは
 *   `native/make_font.sh` の範囲を先に見ること（矢印 U+2190-21FF・
 *   幾何学模様 U+25A0-25FF・互換ジャモ U+3130-318F はどれも入っている）。
 *   ★デーヴァナーガリー（U+0900-097F）は**2 本目の面**から出る（glyph_ft.c の face_of）。
 */
#ifndef HG_UI_H
#define HG_UI_H

/* ★★**UI 言語は 3 つ（日 / 英 / 韓）。ヒンディー語席の説明は英語で出す。**
   （2026-09-04・msonrm さんの判断）—— ★理由は**描画**: 図と表は `draw24` を
   1 字ずつ呼ぶので、**デーヴァナーガリーの文は合字にならない**（母音記号が前に
   回り、子音が縦に積む）。★表に出る `क〜` などは**単字なので合字が起きない**ので、
   「席の説明は出すが、UI 言語としては持たない」で問題が消える。
   ★UI 言語をヒンディー語まで増やすなら、先に図と表を `shape_run` の経路へ移すこと。 */
enum { UI_JA = 0, UI_EN = 1, UI_KO = 2, UI_LANG_N = 3 };

/* ★★**席（話す相手）。** もとは main.c にあったが、★**席ごとの文言を持つのはここ**
   なので、名前で並べられるようにこちらへ移した（2026-09-04）。
   ★★**並びは「英語 → 日本語 → 韓国語 → ヒンディー語」** —— 起動画面の一覧も
   Select の巡回も**同じ順**にするため（実機の指摘 ―― 2 つの順が違うと混乱する）。
   ★原典が英語なので英語が先頭（Zenmai と同じ判断）。
   ★★**この enum の値を変えても中身がずれない**ように、下の配列は**名前で書く**
   （designated initializer）—— 位置で書くと、並べ替えた日に**黙って別の席の説明が出る**。 */
enum { SEAT_EN = 0, SEAT_JA = 1, SEAT_KO = 2, SEAT_HI = 3, SEAT_N = 4 };
enum { UI_SEAT_N = SEAT_N };

/* 図と表の下に置く説明行。★**図にも表にも出せないものだけ**をここに書く。 */
enum { UI_NOTE_N = 2 };

/* スティックの表。★★**箇条書きをやめて格子にした**（実機の指摘・2026-09-04）——
   ★左右のスティック × 5 つの向きは**同じ形の情報が 10 個並ぶ**ので、文にすると
   どれがどれか読み取れない。列は ↑ ↓ ← → 押し込み、行は 右 / 左。
   ★1 列目は見出し（「スティック」「右」「左」）で、**「スティック」は 2 行に
   共通する語をくくり出したもの** —— 各行に「右スティック」と書かずに済む。 */
enum { UI_STK_COLS = 6, UI_STK_ROWS = 3 };

/* 起動画面（言語の選択）。★★**題・説明・作品名は言語に依らない**（Zenmai と同じ）——
   固有名詞に近く、どの言語で読む人にも同じものを指すため。
   ★作品名に 4 席の名を並べたくなるが、**हिन्दी は合字を持つ**ので出さない
   （上の UI_LANG_N の注記）。 */
/* ★★**起動画面で選ぶのは「席」（話す相手）**（実機の指摘・2026-09-04）——
   入り口で English を選んだのに本文が日本語で始まるのは、やはり変だった。
   ★名前は**その言語の自称**で書く（言語に依らないので UiText の外）。
   ★★`हिन्दी` は**合字を持つ**（न् + द）ので、図と表の描画を `shape_run` の経路へ
   移してから出している —— `draw24` を並べるだけでは正しい絵にならない。 */
static const char *const UI_SEAT_NAME[UI_SEAT_N] = {
    [SEAT_JA] = "日本語", [SEAT_EN] = "English",
    [SEAT_KO] = "한국어", [SEAT_HI] = "हिन्दी",
};

static const char *const UI_BOOT_TITLE = "Higgins";
static const char *const UI_BOOT_SUB   = "ELIZA and a game controller";
static const char *const UI_BOOT_WORK  = "DOCTOR (1966)";

/* メニューの項目。★menu[] の並びと同じ（menu_open がこの値を返す）。
   ★★**「つづける」は置かない**（実機の指摘・2026-09-04）—— Start で開いて
     Start で閉じられるので、閉じるためだけの項目は要らない（Zenmai も同じ）。
   ★UI_MENU_CONT は**項目ではなく「何も選ばずに閉じた」**を表すので負の値。 */
enum { UI_MENU_CONT = -1, UI_MENU_HELP = 0, UI_MENU_LICENSE = 1, UI_MENU_QUIT = 2,
       UI_MENU_N = 3 };

/* 同梱しているものと、その出どころ。
   ★★**画面に出すのは帰属、全文はファイル**（配布物の `licenses/`）—— OFL も MIT も
     「著作権表示とライセンス文を配布物に含める」ことを求めるので、**同梱していれば
     要件は満たす**。画面の表は、それを**見える形にする**ためのもの。
   ★★**行は英語のまま。** 固有名詞と法的表記なので、訳すと別のものを指してしまう
     （`Fcitx contributors` を「Fcitx の貢献者」と書いたら、それは誰でもない）。
     ★UI 言語で書くのは見出しと注記だけ。
   ★★**`portmaster/build-port.sh` が入れる `licenses/` と 1 対 1**。片方だけ増やすと
     画面が嘘になる —— 増減するときは両方を直すこと。
   ★DOCTOR の日本語・韓国語・ヒンディー語訳は Higgins のもの（1 行目に含まれる）。 */
enum { UI_LIC_N = 7 };
typedef struct { const char *name, *license, *who; } UiLicense;
static const UiLicense UI_LIC[UI_LIC_N] = {
    { "Higgins",              "MIT",          "msonrm" },
    { "ELIZA + DOCTOR 1966",  "CC0 1.0",      "Anthony Hay" },
    { "Zenmai shell",         "MIT",          "msonrm" },
    { "Mozc",                 "BSD 3-Clause", "Google Inc." },
    { "fcitx5-mozc",          "BSD 3-Clause", "Fcitx project" },
    { "Noto Sans CJK JP",     "OFL 1.1",      "Adobe, Google" },
    { "Noto Sans Devanagari", "OFL 1.1",      "Noto Project" },
};

typedef struct {
    const char *menu[UI_MENU_N];
    const char *lic_title;               /* ライセンス頁の見出し */
    const char *lic_note[2];             /* その下の注記（★ここだけ UI 言語） */
    /* 肩の札。★L1 / L2 / R2 は席ごとの固定文字列、**R1 は動く札**（その行の
       いちばん左のマスそのもの）なので前置きだけを持つ。
       ★ヒンディー語の L1 も動く（varga ごとに鼻音が替わる）ので前置きだけ。 */
    const char *l2[UI_SEAT_N];
    const char *r2[UI_SEAT_N];
    const char *l1[UI_SEAT_N];
    const char *r1;
    /* スティックの表。head = 1 行目（席に依らない）/ stick = 右・左・R2+左 の 3 行。
       ★どちらも 1 列目が見出し。空文字は**そこに役が無い**（見出し以外が全部空なら
       その行は描かない ―― 使わない席で空の行が残ると、表が読みにくくなる）。 */
    const char *stick_head[UI_STK_COLS];
    const char *stick[UI_SEAT_N][UI_STK_ROWS][UI_STK_COLS];
    const char *note[UI_SEAT_N][UI_NOTE_N];
    /* 起動画面の案内 2 行。★選んだ席の UI 言語で出す（席の名前は UI_SEAT_NAME）。
       ★★**Select を先に置く** —— 選択肢のすぐ下なので、「ここで選んでも、あとで
       替えられる」が続きとして読める。 */
    const char *boot_sel;
    const char *boot_hint;
} UiText;

static const UiText UI[UI_LANG_N] = {
    [UI_JA] = {
        .menu = { "文字入力方法", "ライセンス", "やめる" },
        .lic_title = "ライセンス",
        .lic_note = {
            "全文は licenses/ フォルダにあります",
            "DOCTOR の訳も Higgins に含まれます",
        },

        /* ★ヒンディー語の L1 だけ前置き（鼻音の字が varga で替わる）。 */
        .l1 = { [SEAT_JA] = "L1 はまやらわ", [SEAT_EN] = "L1 6〜0",
                [SEAT_KO] = "L1 ㅁㅂㅅㅈㅎ", [SEAT_HI] = "L1 " },
        .l2 = { [SEAT_JA] = "L2 小書き", [SEAT_EN] = "L2 シフト",
                [SEAT_KO] = "L2 ㅇ받침", [SEAT_HI] = "L2 シフト" },
        .r2 = { [SEAT_JA] = "R2 ん を", [SEAT_EN] = "R2 0",
                [SEAT_KO] = "R2 ㅑㅕㅛㅠㅣ", [SEAT_HI] = "R2 " },
        .r1 = "R1 ",

        .stick_head = { "スティック", "↑", "↓", "←", "→", "押し込み" },
        .stick = {
            [SEAT_JA] = {   /* 日本語。★左スティックは**変換中の役**を書く（そちらが主）。 */
                { "RS", "濁点", "、。", "BS", "長音 ー", "取消" },
                { "LS", "前候補", "変換", "文節", "文節", "送信" },
                { "R2+LS", "", "", "短く", "長く", "" },
            },
            [SEAT_EN] = {   /* 英語。★変換が無いので左はキャレットの向きそのもの
                   （「移動」と書くと**何が動くのか**が分からない ―― 実機の指摘）。 */
                { "RS", "'", "空白 .,", "BS", "/", "" },
                { "LS", "↑", "↓", "←", "→", "送信" },
                { "R2+LS", "", "", "", "", "" },
            },
            [SEAT_KO] = {   /* 韓国語。★→ は複合母音（ㅗ→ㅘ / ㅜ→ㅝ）。 */
                { "RS", "濃/激", "空白 .?,", "BS", "ㅘ ㅝ", "" },
                { "LS", "↑", "↓", "←", "→", "送信" },
                { "R2+LS", "", "", "", "", "" },
            },
            [SEAT_HI] = {   /* ★★ヒンディー語だけ左スティックが**組（varga）の latch**。
                   ★「組」とだけ書いても伝わらない（実機の指摘）ので、**その組の
                   代表の子音に「〜」を付ける** —— क〜 = क の組（क ख ग घ ङ）。
                   ★押し込みは非 varga 層（य र ल व）なので同じ書き方で य〜。
                   ★キャレットと送信は **R2 と組む**ので 3 行目に落ちる。 */
                { "RS", "ं ँ", "空白 ।,", "BS", "長母音", "" },
                { "LS", "क〜", "ट〜", "त〜", "च〜", "य〜" },
                { "R2+LS", "↑", "↓", "←", "→", "送信" },
            },
        },

        .boot_sel = "セレクトボタンで相手を替えられます",
        .boot_hint = "スタートボタンでメニュー",

        .note = {
            [SEAT_JA] = {   /* 日本語 */
                "左手で行、右手で段。同時に押すと 1 字入る",
                "L2 ＋ R2 で「っ」",
            },
            [SEAT_EN] = {   /* 英語 */
                "左手で行、右手で列。1 つの行に 3〜4 字ある",
                "L2 を単押しするたびシフトが回る（⇧ → △ → なし）",
            },
            [SEAT_KO] = {   /* 韓国語 */
                "左手で子音、右手で母音。R2 は母音より先に押す",
                "十字キーだけを押すと받침になる（L2 = ㅇ받침）",
            },
            [SEAT_HI] = {   /* ★ヒンディー語。1 行目に latch の作法（倒すたび切替・中立は प）を
                   まとめ、2 行目は**非 varga 層で L2 を足したとき**にあてる ——
                   表の `य〜` だけでは श ष स ह に届かない（実機の指摘・2026-09-04）。 */
                "LS で組、十字キーで子音、右手で母音",
                "य〜 の層で L2 を足すと श ष स ह　L2 ＋ RS↑ = ॉ",
            },
        },
    },
    [UI_EN] = {
        .menu = { "How to type", "Licenses", "Quit" },
        .lic_title = "Licenses",
        .lic_note = {
            "Full texts are in the licenses/ folder",
            "The DOCTOR translations are part of Higgins",
        },

        .l1 = { [SEAT_JA] = "L1 はまやらわ", [SEAT_EN] = "L1 6-0",
                [SEAT_KO] = "L1 ㅁㅂㅅㅈㅎ", [SEAT_HI] = "L1 " },
        .l2 = { [SEAT_JA] = "L2 small", [SEAT_EN] = "L2 Shift",
                [SEAT_KO] = "L2 ㅇ final", [SEAT_HI] = "L2 Shift" },
        .r2 = { [SEAT_JA] = "R2 ん を", [SEAT_EN] = "R2 0",
                [SEAT_KO] = "R2 ㅑㅕㅛㅠㅣ", [SEAT_HI] = "R2 " },
        .r1 = "R1 ",

        .stick_head = { "Stick", "↑", "↓", "←", "→", "Press" },
        .stick = {
            [SEAT_JA] = {   /* Japanese seat */
                { "RS", "voicing", "、。", "BS", "long ー", "cancel" },
                { "LS", "prev", "convert", "phrase", "phrase", "send" },
                { "R2+LS", "", "", "shrink", "grow", "" },
            },
            [SEAT_EN] = {   /* English seat */
                { "RS", "'", "space .,", "BS", "/", "" },
                { "LS", "↑", "↓", "←", "→", "send" },
                { "R2+LS", "", "", "", "", "" },
            },
            [SEAT_KO] = {   /* Korean seat */
                { "RS", "asp/tense", "space .?,", "BS", "ㅘ ㅝ", "" },
                { "LS", "↑", "↓", "←", "→", "send" },
                { "R2+LS", "", "", "", "", "" },
            },
            [SEAT_HI] = {   /* Hindi seat */
                { "RS", "ं ँ", "space ।,", "BS", "long vowel", "" },
                { "LS", "क~", "ट~", "त~", "च~", "य~" },
                { "R2+LS", "↑", "↓", "←", "→", "send" },
            },
        },

        .boot_sel = "SELECT changes who you talk to",
        .boot_hint = "Press START for the menu",

        .note = {
            [SEAT_JA] = {   /* Japanese seat */
                "Left hand picks the row, right hand the vowel",
                "L2 + R2 gives the small tsu",
            },
            [SEAT_EN] = {   /* English seat */
                "Left hand picks the row, right hand the column",
                "Tap L2 to cycle the shift (⇧ → △ → off)",
            },
            [SEAT_KO] = {   /* Korean seat */
                "Left hand consonant, right hand vowel. R2 comes first",
                "D-pad alone adds a final consonant (L2 = ㅇ)",
            },
            [SEAT_HI] = {   /* Hindi seat */
                "LS=varga, D-pad=consonant, right=vowel",
                "In य~, add L2 for श ष स ह　L2 + RS↑ = ॉ",
            },
        },
    },
    [UI_KO] = {
        .menu = { "문자 입력 방법", "라이선스", "종료" },
        .lic_title = "라이선스",
        .lic_note = {
            "전문은 licenses/ 폴더에 있습니다",
            "DOCTOR 번역도 Higgins에 포함됩니다",
        },

        .l1 = { [SEAT_JA] = "L1 はまやらわ", [SEAT_EN] = "L1 6-0",
                [SEAT_KO] = "L1 ㅁㅂㅅㅈㅎ", [SEAT_HI] = "L1 " },
        .l2 = { [SEAT_JA] = "L2 작은 가나", [SEAT_EN] = "L2 시프트",
                [SEAT_KO] = "L2 ㅇ받침", [SEAT_HI] = "L2 시프트" },
        .r2 = { [SEAT_JA] = "R2 ん を", [SEAT_EN] = "R2 0",
                [SEAT_KO] = "R2 ㅑㅕㅛㅠㅣ", [SEAT_HI] = "R2 " },
        .r1 = "R1 ",

        .stick_head = { "스틱", "↑", "↓", "←", "→", "누름" },
        .stick = {
            [SEAT_JA] = {   /* 일본어 자리 */
                { "RS", "탁점", "、。", "BS", "장음 ー", "취소" },
                { "LS", "이전", "한자 변환", "문절", "문절", "보내기" },
                { "R2+LS", "", "", "축소", "확대", "" },
            },
            [SEAT_EN] = {   /* 영어 자리 */
                { "RS", "'", "공백 .,", "BS", "/", "" },
                { "LS", "↑", "↓", "←", "→", "보내기" },
                { "R2+LS", "", "", "", "", "" },
            },
            [SEAT_KO] = {   /* 한국어 자리 */
                { "RS", "격/경음", "공백 .?,", "BS", "ㅘ ㅝ", "" },
                { "LS", "↑", "↓", "←", "→", "보내기" },
                { "R2+LS", "", "", "", "", "" },
            },
            [SEAT_HI] = {   /* 힌디어 자리 */
                { "RS", "ं ँ", "공백 ।,", "BS", "장모음", "" },
                { "LS", "क〜", "ट〜", "त〜", "च〜", "य〜" },
                { "R2+LS", "↑", "↓", "←", "→", "보내기" },
            },
        },

        .boot_sel = "SELECT 로 상대를 바꿉니다",
        .boot_hint = "START 로 메뉴",

        .note = {
            [SEAT_JA] = {   /* 일본어 자리 */
                "왼손은 행, 오른손은 단. 동시에 누르면 한 글자",
                "L2 + R2 = 작은 っ",
            },
            [SEAT_EN] = {   /* 영어 자리 */
                "왼손은 행, 오른손은 열. 한 행에 3~4 글자",
                "L2 를 한 번씩 누르면 시프트가 순환 (⇧ → △ → 없음)",
            },
            [SEAT_KO] = {   /* 한국어 자리 */
                "왼손은 자음, 오른손은 모음. R2 를 먼저 누르세요",
                "십자키만 누르면 받침이 됩니다 (L2 = ㅇ받침)",
            },
            [SEAT_HI] = {   /* 힌디어 자리 */
                "LS=자음군, 십자키=자음, 오른손=모음",
                "य〜 층에서 L2 를 더하면 श ष स ह　L2 + RS↑ = ॉ",
            },
        },
    },
};

#endif
