// web/ の JS 3 本を Node で動かし、CLI 版（Python 経路）と同じ応答が出るかを見る。
// ★ブラウザを開かずに「Web 版が壊れていない」ことを確かめるため。
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';

const root = path.join(import.meta.dirname, '..');
const ctx = { console, setTimeout, clearTimeout, TextEncoder, TextDecoder };
vm.createContext(ctx);
for (const f of ['web/eliza.js', 'web/wakachi.js', 'web/tsunagi.js'])
  vm.runInContext(fs.readFileSync(path.join(root, f), 'utf8'), ctx, { filename: f });

const scriptText = fs.readFileSync(path.join(root, 'doctor/ja.txt'), 'utf8');
ctx.scriptText = scriptText;
ctx.INPUTS = fs.readFileSync(path.join(root, 'experiments/ja_doctor.in'), 'utf8').trim().split('\n');
const out = await vm.runInContext(`(async () => {
  const [status, script] = readScript(scriptText);
  if (status !== 'success') throw new Error('script: ' + status);
  const wk = makeWakachi(scriptText);
  const eliza = new Eliza(script.rules, script.memoryRule, new nullTracer(), 500);
  const lines = [tsunagi(join(script.helloMessage))];
  for (const t of INPUTS) lines.push(tsunagi(await eliza.response(wk.split(t))));
  return lines.join('\\n');
})()`, ctx);

console.log(out);
