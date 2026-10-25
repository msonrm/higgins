'use strict'
/**
 * ブラウザ側のホスト。native と同じ C（wasm/out/higgins.js）に、フォントと変換エンジンを渡すだけ。
 *
 * ★**入力はゲームコントローラだけ**（plat_web.c が Gamepad API を直に読む）。
 *   このファイルはキーボードもタッチも読まない —— 画面に出すのは案内だけ。
 * ★起動の順序は native と同じ: フォントが揃ったら起動画面を出し、**辞書（19MB）は裏で**
 *   読む。C の側は conv_init で（Asyncify で）待つので、席を選ぶあいだに読み終わることが多い。
 * ★参照は開発用の形（`../native/` `../web/vendor/hechima-wasm/` `out/`）。wasm/dist.sh が
 *   1 階層に畳んで書き換える（Zenmai の build.mjs と同じやり方）。
 */
;(async () => {
  const $ = (id) => document.getElementById(id)
  const canvas = $('screen')
  const status = { font: '…', dict: '…', pad: '' }
  const showStatus = () => {
    $('status').textContent = `フォント ${status.font} ／ 辞書 ${status.dict}${status.pad ? ' ／ ' + status.pad : ''}`
  }
  const veil = (text) => {
    const v = $('veil')
    v.hidden = !text
    if (text) v.textContent = text
  }

  // ★進み具合を出しながら取ってくる。`.gz` があればそちらを展開する（辞書は CDN の
  //   自動圧縮が効かない —— hechima の worker と同じ判断）
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
    // ★印（1f 8b）を見てから展開する —— `.gz` に Content-Encoding を付けて配るホストでは
    //   ブラウザが先に展開しているので、二重に解くと壊れる
    if (gz && buf.length > 2 && buf[0] === 0x1f && buf[1] === 0x8b) {
      const s = new Blob([buf]).stream().pipeThrough(new DecompressionStream('gzip'))
      buf = new Uint8Array(await new Response(s).arrayBuffer())
    }
    // ★未配備で HTML（404 のフォールバック）が返ってくると、黙って壊れたフォントになる
    if (!buf.length || buf[0] === 0x3c) throw new Error(`${url} が不正（HTML が返った？）`)
    return buf
  }

  // ---- 変換エンジン（裏で読む）----
  const fixedOffsetTzName = () => {
    // ★wasm には zoneinfo が無いので、固定オフセットの名前で渡す（hechima の worker と同じ）
    const off = -new Date().getTimezoneOffset()
    const a = Math.abs(off)
    const p = (n) => String(n).padStart(2, '0')
    return `Fixed/UTC${off >= 0 ? '+' : '-'}${p(Math.floor(a / 60))}:${p(a % 60)}:00`
  }
  const HECHIMA = '../web/vendor/hechima-wasm/'
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
      status.dict = r === 0 ? 'OK' : `失敗（${r}）`
      showStatus()
      return { H, r }
    } catch (e) {
      console.error(e)
      status.dict = '読めない（かなのまま話します）'
      showStatus()
      return { H: null, r: -1 }
    }
  })()

  // ---- フォント（起動の前に要る）----
  let font, deva
  try {
    ;[font, deva] = await Promise.all([
      fetchBytes('../native/higgins.otf', (p) => { status.font = `${p}%`; showStatus() }),
      fetchBytes('../native/higgins-deva.ttf', () => {}, false),   // ★244KB なので .gz は置かない
    ])
    status.font = 'OK'
    showStatus()
  } catch (e) {
    console.error(e)
    status.font = '読めない'
    showStatus()
    veil('フォントが読めませんでした。読み込み直してください。')
    return
  }

  // ---- コントローラの案内 ----
  // ★ブラウザは**ボタンを 1 度押すまで**パッドを見せない。繋いだだけでは何も来ないので、
  //   それを画面で言う
  let ended = false
  const padNotice = () => {
    if (ended) return
    const pads = navigator.getGamepads ? [...navigator.getGamepads()].filter((p) => p && p.connected) : []
    if (!navigator.getGamepads) {
      status.pad = 'このブラウザはゲームコントローラを読めません'
      veil('このブラウザはゲームコントローラを読めません。\nChrome / Edge / Firefox / Safari の新しい版でお試しください。')
    } else if (!pads.length) {
      status.pad = 'コントローラ: なし'
      veil('ゲームコントローラを繋いで、どれかボタンを押してください。\n（Higgins はコントローラだけで遊びます）')
    } else {
      const g = pads[0]
      const std = g.mapping === 'standard'
      status.pad = `コントローラ: ${g.id}${std ? '' : '（★標準の並びではない）'}`
      veil('')
      $('warn').hidden = std
    }
    showStatus()
  }
  window.addEventListener('gamepadconnected', padNotice)
  window.addEventListener('gamepaddisconnected', padNotice)
  setInterval(padNotice, 1000)
  padNotice()

  // ---- 本体 ----
  const M = await window.HigginsModule({
    canvas,
    print: (s) => console.log(s),
    printErr: (s) => console.warn(s),
    hechimaInit: async () => {
      const { H, r } = await hechimaReady
      M.hechima = H
      return r
    },
    onExit: () => {
      // ★native では「やめる」= プロセスを終える（ランチャへ戻る）。ブラウザには戻り先が無いので言う
      ended = true
      veil('おしまい。\nページを読み込み直すと、もう一度話せます。')
    },
    preRun: [(m) => {
      Object.assign(m.ENV, {
        ZENMAI_FONT: '/higgins.otf',
        ZENMAI_FONT_DEVA: '/higgins-deva.ttf',
      })
    }],
  })
  M.FS.writeFile('/higgins.otf', font)
  M.FS.writeFile('/higgins-deva.ttf', deva)
  try { M.callMain([]) } catch (e) { if (!(e && e.name === 'ExitStatus')) throw e }
})()
