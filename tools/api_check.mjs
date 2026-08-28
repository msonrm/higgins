// index.html が呼ぶ API 名が実在するかを静的に確かめる（沈黙する誤りを防ぐ）
import fs from 'node:fs'; import path from 'node:path'; import vm from 'node:vm';
const root = path.join(import.meta.dirname, '..');
const ctx = { console, setTimeout, clearTimeout, TextEncoder, TextDecoder, URL, fetch,
              self: {}, window: {}, navigator: { userAgent: 'node' } };
ctx.globalThis = ctx; vm.createContext(ctx);
for (const f of ['web/vendor/keymap-engine/keymap-engine.js', 'web/vendor/hechima/hechima.js'])
  vm.runInContext(fs.readFileSync(path.join(root, f), 'utf8'), ctx, { filename: f });

const want = [
  ['Hechima.connectWorker', () => typeof ctx.Hechima?.connectWorker],
  ['Hechima.createFep',     () => typeof ctx.Hechima?.createFep],
  ['KeymapEngine.InputEngine', () => typeof ctx.KeymapEngine?.InputEngine],
  ['KeymapEngine.decodeKeymap', () => typeof ctx.KeymapEngine?.decodeKeymap],
  ['KeymapEngine.keyEventFromBrowser', () => typeof ctx.KeymapEngine?.keyEventFromBrowser],
];
let bad = 0;
for (const [name, get] of want) {
  const t = get();
  if (t !== 'function') { console.log(`✗ ${name} = ${t}`); bad++; } else console.log(`✓ ${name}`);
}
// createFep が返すセッションのメソッドも見る
const fep = ctx.Hechima.createFep({ show(){}, hide(){}, commit(){} });
for (const m of ['setActive', 'feed', 'feedUp', 'setEngine', 'pumpEngine']) {
  if (typeof fep[m] !== 'function') { console.log(`✗ fep.${m}`); bad++; } else console.log(`✓ fep.${m}`);
}
// 配列 JSON が decode できるか
const raw = JSON.parse(fs.readFileSync(path.join(root, 'web/vendor/keymaps/romaji.json'), 'utf8'));
try { ctx.KeymapEngine.decodeKeymap(raw); console.log('✓ decodeKeymap(romaji.json)'); }
catch (e) { console.log('✗ decodeKeymap: ' + e.message); bad++; }
process.exit(bad ? 1 : 0);
