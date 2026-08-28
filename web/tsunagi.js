// 日本語出力の空白を繋ぐ —— tools/tsunagi.py の JS 版。wakachi の対。
// ★日本語文字に隣接する空白だけを消す。英文中の空白は残すので
//   「I am sad」が「Iamsad」に壊れない。
const TSUNAGI_JA = "\\u3041-\\u309F\\u30A0-\\u30FF\\u4E00-\\u9FFF\\u3000-\\u303F\\uFF01-\\uFF60";
const TSUNAGI_AFTER = new RegExp(`(?<=[${TSUNAGI_JA}]) +`, "g");
const TSUNAGI_BEFORE = new RegExp(` +(?=[${TSUNAGI_JA}])`, "g");

function tsunagi(s) {
    return s.replace(TSUNAGI_AFTER, "").replace(TSUNAGI_BEFORE, "");
}
