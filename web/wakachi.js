// 日本語の簡易分かち書き —— tools/wakachi.py の JS 版。
// ELIZA スクリプト自身のキーワードを辞書にして最長一致で切る。
// ★キーワードは必ず切り出せることが構造的に保証される。辞書外は 1 塊で残るので、
//   埋め戻し（スロット）には元の字面がそのまま入る。★冪等。

function elizaTokenizeSexp(text) {
    const out = [];
    for (let line of text.split("\n")) {
        const i = line.indexOf(";");
        if (i >= 0) line = line.slice(0, i);
        out.push(...line.replace(/\(/g, " ( ").replace(/\)/g, " ) ").split(/\s+/).filter(s => s));
    }
    return out;
}

function elizaParseSexp(tokens) {
    let pos = 0;
    const read = () => {
        const tok = tokens[pos++];
        if (tok !== "(") return tok;
        const lst = [];
        while (tokens[pos] !== ")") lst.push(read());
        pos++;
        return lst;
    };
    const forms = [];
    while (pos < tokens.length) forms.push(read());
    return forms;
}

// 照合に使われる語 = 切り出せなければならない語
function collectVocab(forms) {
    const vocab = new Set();
    const walkPattern = (pat) => {
        for (const el of pat) {
            if (Array.isArray(el)) {
                if (el.length && typeof el[0] === "string" && el[0].startsWith("*")) {
                    const head = el[0].replace(/^\*+/, "");
                    if (head) vocab.add(head);
                    for (const e of el.slice(1)) if (typeof e === "string") vocab.add(e);
                }
            } else if (el !== "0") {
                vocab.add(el);
            }
        }
    };
    for (const form of forms.slice(1)) {          // 先頭は挨拶
        if (!Array.isArray(form) || !form.length) continue;
        const head = form[0];
        if (head === "MEMORY") {
            vocab.add(form[1]);
            for (const rule of form.slice(2)) {
                if (!Array.isArray(rule)) continue;
                const eq = rule.indexOf("=");     // ★= の右は再構成なので辞書に入れない
                walkPattern(eq >= 0 ? rule.slice(0, eq) : rule);
            }
            continue;
        }
        if (head !== "NONE") vocab.add(head);
        let rest = form.slice(1);
        if (rest[0] === "=") { vocab.add(rest[1]); rest = rest.slice(2); }
        if (/^\d+$/.test(rest[0])) rest = rest.slice(1);
        if (rest[0] === "DLIST") rest = rest.slice(2);
        for (const tr of rest) {
            if (Array.isArray(tr) && tr.length && Array.isArray(tr[0])) walkPattern(tr[0]);
        }
    }
    return new Set([...vocab].filter(w => w && !/^\d+$/.test(w) && w !== "=" && w !== "/"));
}

function wakachi(text, vocab, maxlen) {
    const out = [];
    let unknown = "";
    const flush = () => { if (unknown) { out.push(unknown); unknown = ""; } };
    let i = 0;
    while (i < text.length) {
        if (/\s/.test(text[i])) { flush(); i++; continue; }   // 既にある区切りは尊重
        let hit = null;
        for (let len = Math.min(maxlen, text.length - i); len > 0; len--) {
            const cand = text.substr(i, len);
            if (vocab.has(cand)) { hit = cand; break; }
        }
        if (hit) { flush(); out.push(hit); i += hit.length; }
        else { unknown += text[i]; i++; }
    }
    flush();
    return out.join(" ");
}

function makeWakachi(scriptText) {
    const forms = elizaParseSexp(elizaTokenizeSexp(scriptText));
    const vocab = collectVocab(forms);
    let maxlen = 1;
    for (const w of vocab) maxlen = Math.max(maxlen, w.length);
    return { vocab, maxlen, split: (s) => wakachi(s, vocab, maxlen) };
}
