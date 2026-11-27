// node で Web 版の芯（webui/out/core.js）に native の台本を食べさせ、本文に積まれた行を出す。
//
//   node webui/run.mjs                    native/*.script を全部（起動画面の台本は飛ばす）
//   node webui/run.mjs conv.script ...    台本を絞る
//   HG_STATE=1 node webui/run.mjs ...     最後の入力欄の状態も出す
//
// ★時間軸は native と同じ「パッドを読んだ回数」。native は席を HIGGINS_SEAT=ja で固定し、
//   最初の 1 回を carry_over_pad が読むので、ここも 1 回目を hg_start に、2 回目から hg_step に回す。
// ★Start（メニュー）は芯の外（JS）なので、メニューを開く台本は native と違う道になる。
import { readFileSync, readdirSync } from 'node:fs'
import { createRequire } from 'node:module'
import path from 'node:path'

const require = createRequire(import.meta.url)
const HERE = path.dirname(new URL(import.meta.url).pathname)
const NATIVE = path.join(HERE, '../native')
const VENDOR = path.join(HERE, '../web/vendor/hechima-wasm')
const HigginsCore = require(path.join(HERE, 'out/core.js'))
const HechimaModule = require(path.join(VENDOR, 'hechima-wasm.js'))
const dict = readFileSync(path.join(VENDOR, 'mozc.data'))

const BIT = {
  SEL: 1 << 0, L3: 1 << 1, R3: 1 << 2, START: 1 << 3, UP: 1 << 4, RIGHT: 1 << 5, DOWN: 1 << 6,
  LEFT: 1 << 7, L2: 1 << 8, R2: 1 << 9, L1: 1 << 10, R1: 1 << 11, TRI: 1 << 12, CIR: 1 << 13,
  X: 1 << 14, SQ: 1 << 15,
  LSL: 1 << 16, LSR: 1 << 17, LSU: 1 << 18, LSD: 1 << 19,
  RSL: 1 << 20, RSR: 1 << 21, RSU: 1 << 22, RSD: 1 << 23,
}
const SEAT_JA = 1

function parse(text) {
  const rows = []
  for (const raw of text.split('\n')) {
    const line = raw.replace(/#.*/, '')
    const m = /^\s*(\d+)\s*-\s*(\d+)\s*:(.*)$/.exec(line)
    if (!m) continue
    let mask = 0
    for (const t of m[3].split(/[+\s]+/).filter(Boolean)) {
      if (!(t in BIT)) throw new Error(`知らないボタン名 ${t}`)
      mask |= BIT[t]
    }
    rows.push([+m[1], +m[2], mask])
  }
  return rows
}

function stopOf(text, rows) {
  const m = /ZENMAI_STOP=(\d+)/.exec(text)
  if (m) return +m[1]
  return Math.max(...rows.map((r) => r[1])) + 200
}

function frame(rows, n) {
  let mask = 0
  for (const [a, b, k] of rows) if (n >= a && n <= b) mask |= k
  const ax = [0x80, 0x80, 0x80, 0x80]
  if (mask & BIT.LSL) ax[2] = 0x00
  if (mask & BIT.LSR) ax[2] = 0xff
  if (mask & BIT.LSU) ax[3] = 0x00
  if (mask & BIT.LSD) ax[3] = 0xff
  if (mask & BIT.RSL) ax[0] = 0x00
  if (mask & BIT.RSR) ax[0] = 0xff
  if (mask & BIT.RSU) ax[1] = 0x00
  if (mask & BIT.RSD) ax[1] = 0xff
  return [mask & 0xffff, ...ax]
}

async function run(text) {
  const rows = parse(text)
  if (!rows.length) return ['★ボタン行が無い']
  const stop = stopOf(text, rows)
  const H = await HechimaModule({ print: () => {}, printErr: () => {} })
  H.FS.writeFile('/mozc.data', dict)
  H.ccall('hechima_init', 'number', ['string'], ['/mozc.data'])
  const M = await HigginsCore({ print: () => {}, printErr: (s) => console.error(s), hechima: H })
  M.hechima = H
  const C = {
    init: M.cwrap('hg_init', 'number', []),
    conv: M.cwrap('hg_conv_ready', 'number', []),
    start: M.cwrap('hg_start', null, ['number', 'number', 'number', 'number', 'number', 'number']),
    step: M.cwrap('hg_step', 'number', ['number', 'number', 'number', 'number', 'number']),
    take: M.cwrap('hg_take', 'string', []),
    state: M.cwrap('hg_state', 'string', ['number']),
  }
  const out = []
  if (C.init() !== 4) out.push('★ELIZA の席が読めない')
  if (!C.conv()) out.push('★変換が使えない')
  const log = () => {
    for (const e of JSON.parse(C.take())) {
      if (e.k === 'scroll') continue
      out.push(`${e.k.padEnd(5)} ${e.t}`)
    }
  }
  C.start(SEAT_JA, ...frame(rows, 1))
  log()
  for (let n = 2; n < stop; n++) {
    const f = frame(rows, n)
    const r = C.step(...f)
    if (r & 2) out.push('(Start)')
    log()
  }
  if (process.env.HG_STATE) {
    const s = JSON.parse(C.state(0))
    out.push(`欄    ${JSON.stringify(s.text)} caret=${s.caret} ua=${s.ua} ub=${s.ub} fa=${s.fa} fb=${s.fb}`)
    if (s.cands.length) out.push(`候補  ${s.cands.join(' / ')} sel=${s.sel}`)
  }
  return out
}

const only = process.argv.slice(2)
const names = (only.length ? only : readdirSync(NATIVE).filter((f) => f.endsWith('.script') && !f.startsWith('lang')))
  .sort()
for (const f of names) {
  console.log(`== ${f}`)
  for (const l of await run(readFileSync(path.join(NATIVE, f), 'utf8'))) console.log('  ' + l)
}
