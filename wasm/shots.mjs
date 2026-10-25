// node で wasm 版に台本を食べさせ、フレームバッファの md5 を並べる —— native/shots.sh の対。
//
//   node wasm/shots.mjs                     md5 の一覧（native/shots.sh と同じ書式）
//   node wasm/shots.mjs menu.script ...     台本を絞る
//   sh native/shots.sh | diff - <(node wasm/shots.mjs)   ★native と同じ絵が出るか
//
// ★**native と同じ台本・同じフォント・同じ回し方**（停止フレームの読み方・席の固定・
//   学習をまっさらにする）。違うのは機械の層だけなので、割れたら差分は plat_web.c か
//   hechima_web.c か、ポートの版（FreeType / HarfBuzz）にある。
// ★学習をまっさらにする = **台本ごとに hechima のモジュールを起こし直す**
//   （native は profile のディレクトリを消している。wasm では学習が仮想 FS の中にある）。
import { readFileSync, readdirSync } from 'node:fs'
import { createHash } from 'node:crypto'
import { createRequire } from 'node:module'
import path from 'node:path'

const require = createRequire(import.meta.url)
const HERE = path.dirname(new URL(import.meta.url).pathname)
const NATIVE = path.join(HERE, '../native')
const VENDOR = path.join(HERE, '../web/vendor/hechima-wasm')
const HigginsModule = require(path.join(HERE, 'out/higgins.js'))
const HechimaModule = require(path.join(VENDOR, 'hechima-wasm.js'))

// ★終わりは onExit で受ける（build.sh が EXIT_RUNTIME=1 で焼く）。
//   ★EXIT_RUNTIME=0 だと runtime が「生きている」扱いで onExit が呼ばれず、終わり方も
//   2 通りに割れた —— 変換エンジンを起こす前に終わる台本（起動画面）は**同期で**終わり
//   callMain が黙って握りつぶす / 起こしたあとは Asyncify の再開の中なので reject で漏れる。
// ★reject のほうは onExit のあとにも来るので、ExitStatus だけ握りつぶす
let finish = null
process.on('unhandledRejection', (e) => {
  if (!(e && e.name === 'ExitStatus')) throw e
})

const font = readFileSync(path.join(NATIVE, 'higgins.otf'))
const deva = readFileSync(path.join(NATIVE, 'higgins-deva.ttf'))
const dict = readFileSync(path.join(VENDOR, 'mozc.data'))

// ★停止フレームは台本の頭のコメント（`ZENMAI_STOP=700`）、無ければ最後のフレーム + 200
//   （native/shots.sh と同じ）
function stopOf(text) {
  const m = /ZENMAI_STOP=(\d+)/.exec(text)
  if (m) return m[1]
  let last = -1
  for (const line of text.split('\n')) {
    const r = /^\s*(\d+)-(\d+)/.exec(line.replace(/#.*/, ''))
    if (r) last = Math.max(last, +r[2])
  }
  return last < 0 ? null : String(last + 200)
}

async function hechima() {
  const H = await HechimaModule({ print: () => {}, printErr: () => {} })
  H.FS.writeFile('/mozc.data', dict)
  const r = H.ccall('hechima_init', 'number', ['string'], ['/mozc.data'])
  return { H, r }
}

async function run(name, text) {
  const stop = stopOf(text)
  if (!stop) return { err: 'フレームが読めない' }
  const seat = name.startsWith('lang') ? '' : 'ja'
  const finished = new Promise((res) => { finish = res })
  let hc = null
  const M = await HigginsModule({
    print: () => {},
    printErr: (s) => { if (process.env.HG_VERBOSE) console.error(s) },
    onExit: (code) => { if (finish) finish(code) },
    hechimaInit: async () => {
      hc = await hechima()
      M.hechima = hc.H
      return hc.r
    },
    preRun: [(m) => {
      Object.assign(m.ENV, {
        ZENMAI_SCRIPT: '/t.script', ZENMAI_STOP: stop, ZENMAI_RAW: '/t.raw',
        ZENMAI_FONT: '/higgins.otf', ZENMAI_FONT_DEVA: '/higgins-deva.ttf',
        HIGGINS_SEAT: seat,
      })
    }],
  })
  M.FS.writeFile('/higgins.otf', font)
  M.FS.writeFile('/higgins-deva.ttf', deva)
  M.FS.writeFile('/t.script', text)
  try { M.callMain([]) } catch (e) { if (!(e && e.name === 'ExitStatus')) throw e }
  const code = await finished
  finish = null
  if (code !== 0) return { err: `★止まった（exit ${code}）` }
  try {
    return { md5: createHash('md5').update(M.FS.readFile('/t.raw')).digest('hex') }
  } catch { return { err: '★絵が出なかった' } }
}

const only = process.argv.slice(2)
const names = (only.length ? only : readdirSync(NATIVE).filter((f) => f.endsWith('.script')))
  .sort((a, b) => (a < b ? -1 : a > b ? 1 : 0))
let ng = 0
for (const f of names) {
  const r = await run(f, readFileSync(path.join(NATIVE, f), 'utf8'))
  if (r.err) { console.log(`${f.padEnd(22)} ${r.err}`); ng = 1; continue }
  console.log(`${f.padEnd(22)} ${r.md5}`)
}
process.exit(ng)
