'use strict'
/**
 * Higgins Web 版（DOM で描く）のホスト。
 *
 * ★打鍵の解釈・合成・ELIZA・変換は C の芯（out/core.js = webui/core.c）がやる。
 *   ここは **パッドを読んで芯へ渡し、返ってきた状態を HTML に描く**だけ。
 * ★1 フレーム = 1/60 秒で芯を進める（芯の時計 —— 長押しのリピート・句読点の巡回の窓 ——
 *   はフレームで数えている）。画面の更新が 120Hz でも速くならないよう、時間で刻む。
 * ★入力はゲームコントローラだけ（キーボードは読まない）。起動画面の席だけはマウスでも選べる。
 * ★参照は開発用の形（`../native/` `../web/vendor/hechima-wasm/` `out/`）。dist.sh が畳む。
 */
;(async () => {
  const $ = (id) => document.getElementById(id)
  const HECHIMA = '../web/vendor/hechima-wasm/'
  const SEAT_N = 4
  const SEAT_CODE = ['EN', 'JA', 'KO', 'HI']
  const PAGE = 5                      // ★候補窓の 1 ページ（conv.h の CONV_PAGE と同じ）

  // ---- パッドのビット（native/plat.h と同じ並び）----
  const B = {
    SELECT: 1 << 0, L3: 1 << 1, R3: 1 << 2, START: 1 << 3, UP: 1 << 4, RIGHT: 1 << 5,
    DOWN: 1 << 6, LEFT: 1 << 7, L2: 1 << 8, R2: 1 << 9, L1: 1 << 10, R1: 1 << 11,
    TRI: 1 << 12, CIR: 1 << 13, X: 1 << 14, SQ: 1 << 15,
  }
  const FACE = B.CIR | B.X | B.TRI | B.SQ

  // ★Gamepad API の "standard" 配置は**位置**で決まっている（[0]=下 [1]=右 [2]=左 [3]=上）。
  //   写し方は wasm/plat_web.c と同じ（L2 / R2 はアナログの 1/4 で押した扱い）
  let padInfo = null
  function readPad() {
    if (!navigator.getGamepads) return null
    let g = null
    for (const x of navigator.getGamepads()) if (x && x.connected) { g = x; break }
    padInfo = g ? { id: g.id, std: g.mapping === 'standard' } : null
    if (!g) return null
    const b = (i) => { const x = g.buttons[i]; return !!x && (x.pressed || x.value > 0.5) }
    const t = (i) => { const x = g.buttons[i]; return !!x && (x.value > 0.25 || x.pressed) }
    let p = 0
    if (b(0)) p |= B.X
    if (b(1)) p |= B.CIR
    if (b(2)) p |= B.SQ
    if (b(3)) p |= B.TRI
    if (b(4)) p |= B.L1
    if (b(5)) p |= B.R1
    if (t(6)) p |= B.L2
    if (t(7)) p |= B.R2
    if (b(8)) p |= B.SELECT
    if (b(9)) p |= B.START
    if (b(10)) p |= B.L3
    if (b(11)) p |= B.R3
    if (b(12)) p |= B.UP
    if (b(13)) p |= B.DOWN
    if (b(14)) p |= B.LEFT
    if (b(15)) p |= B.RIGHT
    const ax = (i) => {
      let v = Math.round((g.axes[i] || 0) * 32767)
      v = Math.max(-32768, Math.min(32767, v))
      return Math.max(0, Math.min(255, (v + 32768) >> 8))
    }
    return { p, axes: [ax(2), ax(3), ax(0), ax(1)] }   // ★右 X・右 Y・左 X・左 Y（plat.h の並び）
  }
  const NONE = { p: 0, axes: [0x80, 0x80, 0x80, 0x80] }

  // 左スティックの上下（メニューを動かす）。★大きい方の軸だけ（native の ls_dirs と同じ）
  const lsVert = (axes) => {
    const dx = axes[2] - 0x80, dy = axes[3] - 0x80
    if (Math.abs(dx) < 0x40 && Math.abs(dy) < 0x40) return 0
    if (Math.abs(dx) >= Math.abs(dy)) return 0
    return dy < 0 ? -1 : 1
  }

  // ---- 状態表示 ----
  const status = { dict: '…', pad: '' }
  const showStatus = () => {
    const dict = `変換 ${status.dict}`
    $('stat').textContent = dict
    const ps = $('intro-status')
    ps.textContent = ''
    const line = (text, warn) => {
      const d = document.createElement('div')
      if (warn) d.className = 'warn'
      d.textContent = text
      ps.appendChild(d)
    }
    line(`${dict}${status.pad ? ' ／ ' + status.pad : ''}`)
    if (padInfo && !padInfo.std) line('★このコントローラは標準の並びではありません（ボタンの位置が図と違うかもしれません）', true)
  }

  // ---- 変換エンジン（裏で読む）----
  async function fetchBytes(url, onProgress, tryGz = true) {
    const tryGet = async (u) => {
      const r = await fetch(u)
      if (!r.ok) throw new Error(`${u}: ${r.status}`)
      return r
    }
    let res, gz = false
    if (tryGz && typeof DecompressionStream !== 'undefined') {
      try { res = await tryGet(url + '.gz'); gz = true } catch { res = null }
    }
    if (!res) res = await tryGet(url)
    const total = +res.headers.get('content-length') || 0
    const reader = res.body.getReader()
    const parts = []
    let got = 0
    for (;;) {
      const { done, value } = await reader.read()
      if (done) break
      parts.push(value)
      got += value.length
      if (total) onProgress(Math.min(99, Math.floor((got * 100) / total)))
    }
    let buf = new Uint8Array(got)
    let o = 0
    for (const p of parts) { buf.set(p, o); o += p.length }
    if (gz && buf.length > 2 && buf[0] === 0x1f && buf[1] === 0x8b) {
      const s = new Blob([buf]).stream().pipeThrough(new DecompressionStream('gzip'))
      buf = new Uint8Array(await new Response(s).arrayBuffer())
    }
    if (!buf.length || buf[0] === 0x3c) throw new Error(`${url} が不正（HTML が返った？）`)
    return buf
  }
  const fixedOffsetTzName = () => {
    const off = -new Date().getTimezoneOffset()
    const a = Math.abs(off)
    const p = (n) => String(n).padStart(2, '0')
    return `Fixed/UTC${off >= 0 ? '+' : '-'}${p(Math.floor(a / 60))}:${p(a % 60)}:00`
  }
  const loadScript = (src) => new Promise((res, rej) => {
    const s = document.createElement('script')
    s.src = src
    s.onload = res
    s.onerror = () => rej(new Error(`${src} が読めない`))
    document.head.appendChild(s)
  })
  const hechimaReady = (async () => {
    try {
      await loadScript(HECHIMA + 'hechima-wasm.js')
      const [dict, H] = await Promise.all([
        fetchBytes(HECHIMA + 'mozc.data', (p) => { status.dict = `${p}%`; showStatus() }),
        window.HechimaModule({
          locateFile: (p) => HECHIMA + p,
          preRun: [(m) => { m.ENV.TZ = fixedOffsetTzName() }],
          print: () => {},
          printErr: (s) => console.warn('[hechima]', s),
        }),
      ])
      H.FS.writeFile('/mozc.data', dict)
      const r = H.ccall('hechima_init', 'number', ['string'], ['/mozc.data'])
      if (r !== 0) throw new Error(`hechima_init = ${r}`)
      return H
    } catch (e) {
      console.error(e)
      status.dict = '読めない（かなのまま話します）'
      showStatus()
      return null
    }
  })()

  // ---- 芯 ----
  const M = await window.HigginsCore({ print: (s) => console.log(s), printErr: (s) => console.warn(s) })
  const C = {
    init: M.cwrap('hg_init', 'number', []),
    conv: M.cwrap('hg_conv_ready', 'number', []),
    start: M.cwrap('hg_start', null, ['number', 'number', 'number', 'number', 'number', 'number']),
    resync: M.cwrap('hg_resync', null, ['number', 'number', 'number', 'number', 'number']),
    step: M.cwrap('hg_step', 'number', ['number', 'number', 'number', 'number', 'number']),
    take: M.cwrap('hg_take', 'string', []),
    state: M.cwrap('hg_state', 'string', ['number']),
    ui: M.cwrap('hg_ui', 'string', ['number']),
  }
  if (C.init() !== SEAT_N) console.error('★ELIZA の席が全部は読めなかった')
  const UI = [0, 1, 2].map((l) => JSON.parse(C.ui(l)))
  hechimaReady.then((H) => {
    if (!H) return
    M.hechima = H
    status.dict = C.conv() ? 'OK' : '使えない'
    showStatus()
  })

  // ================= 起動画面 =================
  let mode = 'boot'                    // boot / talk / menu / lic
  let bootSel = 0                      // ★先頭 ＝ 原典の英語（native と同じ）
  const seatsEl = $('boot-seats')
  const u0 = UI[0]
  $('boot-sub').textContent = u0.sub
  $('boot-work').textContent = u0.work
  $('title-tag').textContent = ' —— ' + u0.sub
  const seatBtns = u0.seats.map((name, i) => {
    const b = document.createElement('button')
    b.type = 'button'
    b.className = 'intro-work'
    const t = document.createElement('span')
    t.className = 'in'
    t.textContent = name
    b.appendChild(t)
    b.lang = ['en', 'ja', 'ko', 'hi'][i]
    b.addEventListener('mouseenter', () => { bootSel = i; drawBoot() })
    b.addEventListener('click', () => { bootSel = i; startTalk(i, NONE) })
    seatsEl.appendChild(b)
    return b
  })
  // ★席の名前も枠の上下中央へ（操作図の札と同じ測り方・inkDy）。★語なので、語そのものの形では
  //   なく**その字の本体の高さ**で寄せる —— 英語は大文字の高さ（English の g の下の伸びに
  //   引っ張られて全体が持ち上がらないように）、ヒンディー語は頭線から基準線（ि の上の飾りを除く）。
  //   日本語・韓国語は字の箱がそのまま本体なので語で測る。
  const BOOT_BODY = ['H', null, null, 'क']
  function drawBoot() {
    seatBtns.forEach((b, i) => {
      b.classList.toggle('on', i === bootSel)
      const t = b.firstChild
      t.style.top = inkDy(b, BOOT_BODY[i] || t.textContent) + 'em'
    })
    const u = UI[u0.seatUi[bootSel]]
    $('boot-sel').textContent = u.bootSel
    $('boot-hint').textContent = u.bootHint
  }
  function showBoot() {
    mode = 'boot'
    document.body.classList.add('boot')
    $('intro').hidden = false
    drawBoot()
  }

  // ================= 会話 =================
  const screen = $('screen')
  const line = $('line')
  const caretEl = $('caret')
  let curSeat = -1
  let lastP = -1

  function startTalk(seat, pad) {
    screen.textContent = ''
    curSeat = -1
    C.start(seat, pad.p, ...pad.axes)
    mode = 'talk'
    document.body.classList.remove('boot')
    $('intro').hidden = true
    drain()
    render(pad.p)
  }

  function addPara(cls, text) {
    const p = document.createElement('p')
    if (cls) p.className = cls
    p.textContent = text
    screen.appendChild(p)
    return p
  }

  // 芯に溜まった出来事を本文へ。
  function drain() {
    const evs = JSON.parse(C.take())
    let grew = false
    for (const e of evs) {
      if (e.k === 'scroll') {
        screen.scrollBy({ top: e.d * Math.max(80, screen.clientHeight * 0.35), behavior: 'smooth' })
        continue
      }
      if (e.k === 'hello') {
        // ★席が替わった印は**新しい相手の挨拶**（native と同じ）。区切りの線と相手の名前だけ足す
        if (screen.childElementCount) screen.appendChild(document.createElement('hr'))
      }
      const p = addPara(e.k === 'you' ? 'you' : '', e.t)
      if (e.k !== 'you') p.lang = ['en', 'ja', 'ko', 'hi'][curSeatOfNext()]
      grew = true
    }
    if (grew) screen.scrollTop = screen.scrollHeight
  }
  // ★挨拶・返事の言語（lang 属性 = 字形の選択に効く）。状態を読み直して決める
  const curSeatOfNext = () => JSON.parse(C.state(0)).seat

  // ---- 入力欄 ----
  function renderLine(st) {
    for (const n of [...line.childNodes]) if (n !== caretEl) n.remove()
    const s = st.text
    const cuts = [...new Set([0, s.length, st.ua, st.ub, st.fa, st.fb].filter((x) => x >= 0 && x <= s.length))]
      .sort((a, b) => a - b)
    const pieces = []
    for (let i = 0; i + 1 < cuts.length; i++) {
      const a = cuts[i], b = cuts[i + 1]
      if (a === b) continue
      let cls = ''
      if (st.fa >= 0 && a >= st.fa && b <= st.fb) cls = 'focus'
      else if (a >= st.ua && b <= st.ub) cls = 'pend'
      const span = document.createElement('span')
      if (cls) span.className = cls
      span.textContent = s.slice(a, b)
      line.insertBefore(span, caretEl)
      pieces.push({ a, b, node: span.firstChild, span })
    }
    // ★キャレットは**字の間に要素を挟まない**（挟むとデーヴァナーガリーの合字が割れる）。
    //   Range で位置を測って、上に重ねる
    if (st.caret < 0) { caretEl.hidden = true } else {
      caretEl.hidden = false
      const lr = line.getBoundingClientRect()
      let rect = null
      const pc = pieces.find((q) => st.caret >= q.a && st.caret <= q.b && !(st.caret === q.b && pieces.some((r) => r.a === q.b)))
      if (pc) {
        const r = document.createRange()
        r.setStart(pc.node, st.caret - pc.a)
        r.collapse(true)
        rect = r.getClientRects()[0] || r.getBoundingClientRect()
      }
      const lh = parseFloat(getComputedStyle(line).lineHeight) || 28
      if (rect && rect.height) {
        caretEl.style.left = (rect.left - lr.left + line.scrollLeft) + 'px'
        caretEl.style.top = (rect.top - lr.top + line.scrollTop + (rect.height - lh * 0.8) / 2) + 'px'
      } else {
        caretEl.style.left = '0px'
        caretEl.style.top = (parseFloat(getComputedStyle(line).paddingTop) + lh * 0.1) + 'px'
      }
      caretEl.style.height = (lh * 0.8) + 'px'
      // ★欄が 3 行を超えて流れたら、キャレットが見えるところへ
      const ct = parseFloat(caretEl.style.top)
      if (ct < line.scrollTop) line.scrollTop = ct
      else if (ct + lh > line.scrollTop + line.clientHeight) line.scrollTop = ct + lh - line.clientHeight
    }
    return pieces
  }

  // ---- 候補窓（native の draw_cands の並べ方: 追加候補は上に別枠、本来の候補は 5 件で 1 ページ）----
  function renderCands(st, pieces) {
    const box = $('cands')
    const n = st.cands.length
    if (!st.conv || !n) { box.hidden = true; return }
    box.textContent = ''
    const na = st.addl, nc = n - na, sel = st.sel
    const inAddl = sel < na
    const page = inAddl ? 0 : Math.floor((sel - na) / PAGE)
    const pages = Math.ceil(nc / PAGE)
    const row = (i) => {
      const d = document.createElement('div')
      d.className = 'c' + (i === sel ? ' on' : '')
      d.textContent = st.cands[i]
      box.appendChild(d)
    }
    for (let i = 0; i < na; i++) row(i)
    if (na && nc) box.appendChild(document.createElement('hr'))
    for (let i = na + page * PAGE; i < Math.min(n, na + (page + 1) * PAGE); i++) row(i)
    if (pages > 1) {
      const dots = document.createElement('div')
      dots.className = 'dots'
      for (let i = 0; i < pages; i++) {
        const d = document.createElement('i')
        if (i === page) d.className = 'on'
        dots.appendChild(d)
      }
      box.appendChild(dots)
    }
    box.hidden = false
    // ★注目文節の左端に合わせる
    const f = pieces.find((q) => q.a === st.fa)
    const pr = $('prompt').getBoundingClientRect()
    let x = f ? f.span.getBoundingClientRect().left - pr.left : line.offsetLeft
    x = Math.max(0, Math.min(x - 8, pr.width - box.offsetWidth))
    box.style.left = x + 'px'
  }

  // ---- 操作図（Zenmai の #pad の造形。札は芯が入力表から引く）----
  const pad = $('pad')
  const padEl = {}
  ;(function buildPad() {
    const el = (cls, parent, tag = 'div') => {
      const d = document.createElement(tag)
      d.className = cls
      parent.appendChild(d)
      return d
    }
    const wrap = el('gp', pad)
    const side = (s) => {
      const col = el('gp-side', wrap)
      const sh = el('gp-shoulders', col)
      // ★肩は「外側が 1、内側が 2」（native の図と同じ。実機の指摘で決まった向き）
      for (const k of s === 'l' ? ['L1', 'L2'] : ['R2', 'R1']) padEl[k] = el('gp-trigger', sh)
      const grid = el(s === 'l' ? 'gp-grid gp-dpad' : 'gp-grid gp-face', col)
      const map = s === 'l'
        ? [null, 'UP', null, 'LEFT', 'C', 'RIGHT', null, 'DOWN', null]
        : [null, 'TRI', null, 'SQ', null, 'CIR', null, 'X', null]
      for (const k of map) {
        const c = el(k === null ? 'gp-btn gp-blank' : 'gp-btn', grid)
        if (k) padEl[k] = c
      }
    }
    side('l')
    const mid = el('gp-mid', wrap)
    padEl.seats = SEAT_CODE.map((c) => { const d = el('gp-seat sans', mid); d.textContent = c; return d })
    side('r')
  })()
  $('pad-btn').addEventListener('click', () => document.body.classList.toggle('pad-closed'))

  // ---- 札を枠の上下中央へ ----
  // ★★**推定しない。字の形（インク）を測る**（native の ink_dy と同じ考え）。
  //   flex の中央寄せが揃えるのは**行の箱**で、字の形ではない。行の箱の中で字がどこに来るかは
  //   フォントの ascent / descent で決まり、しかも**どのフォントに落ちるかは環境で替わる**
  //   （実測: 開発機ではかなとハングルが 43px の枠で 4〜5px 上に寄っていた。英字と
  //   デーヴァナーガリーはほぼ中央）。
  // ★★**札ごとに、その字の形で寄せる。** はじめは native の右下の箱にならって「席で出うる字を
  //   まとめて測り、ずれを 1 つにする」形にしたが、どの見本を選んでも**どこかの字が外れた**
  //   （英語は大文字に合わせると小文字 a c e が 4px 下、ヒンディー語は ओ の母音記号や
  //   झ इ の下の伸びが範囲を広げて子音が下へ）。★native の箱は**1 つの枠の中身が入れ替わる**
  //   ので跳ねが見えたが、この図は**1 字ずつ別の丸**なので、各々が中央にあるほうが揃って見える。
  // ★値は em で持つ（図の大きさは画面幅で替わるが、比は替わらない）。字ごとに覚えておく。
  const inkCtx = document.createElement('canvas').getContext('2d')
  const inkCache = new Map()
  // elm（枠）の中の `.in` に字が入った状態で呼ぶ。返すのは「字の形を枠の上下中央へ
  // 寄せるのに要るずれ」（em）。text = 形を測る字（席の見本か、その札の字）。
  // ★★**基準線の位置は推定せず、置いて測る。** はじめは canvas の fontBoundingBox から
  //   計算していたが、あれは**最初の指定フォント**の寸法で、字を描いている**代替フォント**
  //   （開発機では IPAGothic）とは違い、半分しか直らなかった（実測）。
  //   ★canvas で測るのは字の形（actualBoundingBox）だけ —— こちらは描く字そのもの。
  function inkDy(elm, text) {
    const box = elm.querySelector('.in')
    if (!text || !box) return 0
    const cs = getComputedStyle(elm)
    const fs = parseFloat(cs.fontSize) || 16
    const key = [cs.fontFamily, cs.fontWeight, fs, elm.lang || pad.lang, elm.clientHeight, text].join('|')
    if (inkCache.has(key)) return inkCache.get(key)
    box.style.top = '0'
    // ★測るあいだだけ中身を**ふつうの行**にする —— `.in` は flex なので、目印が字と
    //   同じ行に入らず別の段に置かれる（面の札で踏んだ。肩は baseline 揃えなので偶々合った）。
    //   1 行・line-height 1 の箱なので、flex でも block でも枠の中の高さと位置は同じ。
    box.style.display = 'block'
    const probe = document.createElement('span')
    probe.style.cssText = 'display:inline-block;width:0;height:0;vertical-align:baseline'
    box.appendChild(probe)
    const er = elm.getBoundingClientRect()
    const base = probe.getBoundingClientRect().top - er.top      // 枠の上端から基準線まで（px）
    probe.remove()
    box.style.display = ''
    inkCtx.font = `${cs.fontStyle} ${cs.fontWeight} 100px ${cs.fontFamily}`
    if ('lang' in inkCtx) inkCtx.lang = elm.lang || pad.lang || 'ja'
    const m = inkCtx.measureText(text)
    const k = fs / 100
    const inkMid = base - (m.actualBoundingBoxAscent - m.actualBoundingBoxDescent) / 2 * k
    const dy = (er.height / 2 - inkMid) / fs
    const v = Number.isFinite(dy) && er.height ? dy : 0
    inkCache.set(key, v)
    return v
  }
  // ★フォントは遅れて届く（ハングルの逃げ場は字が出てから読む）—— 届いたら測り直す
  if (document.fonts) document.fonts.addEventListener('loadingdone', () => {
    inkCache.clear()
    lastP = -1
    if (mode === 'boot') drawBoot()
  })
  window.addEventListener('resize', () => { inkCache.clear(); lastP = -1 })

  const inner = (elm) => {
    elm.textContent = ''
    const s = document.createElement('span')
    s.className = 'in'
    elm.appendChild(s)
    return s
  }
  const label = (elm, main, sub, dy) => {
    const box = inner(elm)
    box.style.top = (dy || 0) + 'em'
    if (!sub) { box.textContent = main || ''; return }
    for (const [cls, t] of [['gp-l1', main], ['gp-l2', sub]]) {
      const s = document.createElement('span')
      s.className = cls
      s.textContent = t
      box.appendChild(s)
    }
  }
  // 肩の札。ui.h の文言は「L1 はまやらわ」の形なので、ボタン名を落として役だけにする
  const role = (s) => (s || '').replace(/^[LR][12]\s?/, '')
  const shoulder = (elm, text, ch, isChar) => {
    const box = inner(elm)
    const t = role(text)
    if (t) box.appendChild(document.createTextNode(t))
    if (ch) {
      const c = document.createElement('span')
      c.className = t ? 'gp-c' : ''
      c.textContent = ch
      box.appendChild(c)
    }
    elm.classList.toggle('gp-char', !!isChar)
    box.style.top = inkDy(elm, t + (ch || '')) + 'em'
  }

  function renderPad(st, p) {
    const P = st.pad
    const on = (elm, yes) => elm.classList.toggle('on', !!yes)
    shoulder(padEl.L1, P.l1, P.l1c)
    shoulder(padEl.L2, P.l2, '')
    shoulder(padEl.R2, P.r2, P.r2c, true)
    shoulder(padEl.R1, '', P.face.r1, true)
    const sub = P.dsub || {}
    // ★面と十字は札ごとに、その字の形で寄せる
    //   （★英語の十字は 2 段 ＝ 数字 ＋ 英字の塊なので測らない ―― 塊の箱で中央に来ている）
    const cells = [[padEl.UP, P.dpad.u, sub.u], [padEl.LEFT, P.dpad.l, sub.l], [padEl.RIGHT, P.dpad.r, sub.r],
      [padEl.DOWN, P.dpad.d, sub.d], [padEl.C, P.dpad.c, sub.c],
      [padEl.TRI, P.face.tri], [padEl.SQ, P.face.sq], [padEl.CIR, P.face.cir], [padEl.X, P.face.x]]
    for (const [e, m, s] of cells) label(e, m, s, 0)
    padEl.C.classList.toggle('gp-blank', !P.center)
    for (const [e, m, s] of cells) if (m && !s) e.firstChild.style.top = inkDy(e, m) + 'em'
    on(padEl.L1, p & B.L1); on(padEl.L2, p & B.L2); on(padEl.R2, p & B.R2); on(padEl.R1, p & B.R1)
    on(padEl.UP, p & B.UP); on(padEl.LEFT, p & B.LEFT); on(padEl.RIGHT, p & B.RIGHT); on(padEl.DOWN, p & B.DOWN)
    on(padEl.C, P.center && !(p & (B.UP | B.DOWN | B.LEFT | B.RIGHT)))
    on(padEl.TRI, p & B.TRI); on(padEl.SQ, p & B.SQ); on(padEl.CIR, p & B.CIR); on(padEl.X, p & B.X)
    padEl.seats.forEach((d, i) => d.classList.toggle('on', i === st.seat))
  }

  function render(p) {
    const st = JSON.parse(C.state(p))
    const lang = ['en', 'ja', 'ko', 'hi'][st.seat]
    line.lang = lang
    pad.lang = lang
    if (st.seat !== curSeat) {
      curSeat = st.seat
      const u = UI[st.ui]
      $('place').textContent = UI[0].seats[st.seat]
      const note = $('pad-note')
      note.textContent = ''
      for (const t of u.note[st.seat]) {
        const d = document.createElement('div')
        d.textContent = t
        note.appendChild(d)
      }
      note.lang = ['ja', 'en', 'ko'][st.ui]
    }
    const pieces = renderLine(st)
    renderCands(st, pieces)
    // ★右下の札 = いまの行（シフトの印は前に）
    const pb = $('pad-btn')
    pb.textContent = ''
    if (st.mark) {
      const m = document.createElement('span')
      m.className = 'mark'
      m.textContent = st.mark
      pb.appendChild(m)
    }
    pb.appendChild(document.createTextNode(st.ind))
    renderPad(st, p)
  }

  // ================= メニュー / ライセンス =================
  let menuSel = 0
  const menuItems = () => {
    const u = UI[JSON.parse(C.state(0)).ui]
    return [['lic', u.menu.license], ['quit', u.menu.quit]]
  }
  function drawMenu() {
    const box = $('menu-box')
    box.textContent = ''
    menuItems().forEach(([k, t], i) => {
      const d = document.createElement('div')
      d.className = 'menu-item' + (i === menuSel ? ' on' : '')
      d.textContent = t
      d.addEventListener('click', () => { menuSel = i; menuPick() })
      box.appendChild(d)
    })
  }
  function openMenu() {
    mode = 'menu'
    menuSel = 0
    $('menu').hidden = false
    drawMenu()
  }
  function closeDialogs(padNow) {
    $('menu').hidden = true
    $('lic').hidden = true
    mode = 'talk'
    // ★押されたままのものを「もう見た」ことにする（選んだボタンで字が入らないように）
    C.resync(padNow.p, ...padNow.axes)
    acc = 0
  }
  function menuPick() {
    const [k] = menuItems()[menuSel]
    $('menu').hidden = true
    if (k === 'lic') openLic()
    else if (k === 'quit') showBoot()   // ★native の「やめる」= 起動画面へ戻る（ブラウザに戻り先は無い）
  }
  function openLic() {
    mode = 'lic'
    const u = UI[JSON.parse(C.state(0)).ui]
    $('lic-title').textContent = u.licTitle
    const tbl = $('lic-tbl')
    tbl.textContent = ''
    for (const [n, l, w] of u.lic) {
      const tr = document.createElement('tr')
      for (const [cls, t] of [['n', n], ['l', l], ['w', w]]) {
        const td = document.createElement('td')
        td.className = cls
        td.textContent = t
        tr.appendChild(td)
      }
      tbl.appendChild(tr)
    }
    const note = $('lic-note')
    note.textContent = ''
    const a = document.createElement('a')
    a.href = 'licenses/'
    a.target = '_blank'
    a.textContent = u.licNote[0]
    note.appendChild(a)
    note.appendChild(document.createElement('br'))
    note.appendChild(document.createTextNode(u.licNote[1]))
    $('lic').hidden = false
  }

  // ================= 主ループ =================
  const FRAME = 1000 / 60
  let acc = 0, last = performance.now()
  let prev = 0, prevLs = 0
  let noticeAt = 0

  function padNotice(now) {
    if (now < noticeAt) return
    noticeAt = now + 500
    const v = $('veil')
    if (!navigator.getGamepads) {
      status.pad = 'このブラウザはゲームコントローラを読めません'
      v.textContent = 'このブラウザはゲームコントローラを読めません。\nThis browser cannot read game controllers.'
      v.hidden = false
    } else if (!padInfo) {
      status.pad = 'コントローラ: なし'
      v.textContent = 'ゲームコントローラを繋いで、どれかボタンを押してください。\nConnect a game controller and press any button.'
      v.hidden = false
    } else {
      status.pad = `コントローラ: ${padInfo.id}`
      v.hidden = true
    }
    showStatus()
  }

  function tick(now) {
    const dt = Math.min(100, now - last)
    last = now
    const pd = readPad() || NONE
    padNotice(now)
    const p = pd.p
    const edge = p & ~prev
    const lv = lsVert(pd.axes)
    const lsEdge = lv !== prevLs ? lv : 0
    prev = p
    prevLs = lv

    if (mode === 'boot') {
      const up = (edge & B.UP) || lsEdge < 0, down = (edge & B.DOWN) || lsEdge > 0
      if (up) { bootSel = (bootSel + SEAT_N - 1) % SEAT_N; drawBoot() }
      if (down) { bootSel = (bootSel + 1) % SEAT_N; drawBoot() }
      if (edge & (FACE | B.START | B.L3)) startTalk(bootSel, pd)
    } else if (mode === 'menu') {
      const n = menuItems().length
      if ((edge & B.UP) || lsEdge < 0) { menuSel = (menuSel + n - 1) % n; drawMenu() }
      if ((edge & B.DOWN) || lsEdge > 0) { menuSel = (menuSel + 1) % n; drawMenu() }
      if (edge & B.START) closeDialogs(pd)
      else if (edge & (FACE | B.L3)) {
        menuPick()
        if (mode === 'menu') closeDialogs(pd)
      }
    } else if (mode === 'lic') {
      if (edge & (B.START | FACE)) closeDialogs(pd)
    } else if (mode === 'talk') {
      acc += dt
      let dirty = false, n = 0
      while (acc >= FRAME && n < 6) {
        acc -= FRAME
        n++
        const r = C.step(p, ...pd.axes)
        if (r & 1) dirty = true
        if (r & 2) { openMenu(); break }
      }
      if (dirty) drain()
      if (dirty || p !== lastP) { render(p); lastP = p }
    }
    requestAnimationFrame(tick)
  }

  showBoot()
  showStatus()
  requestAnimationFrame(tick)
})()
