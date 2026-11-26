#!/usr/bin/env node
// Browser preview of the panel as Rack will show it: artwork + real Rack components + code-drawn
// screens and labels. No build, no Rack restart.
//   node tools/preview_panel.mjs [panel.svg] [--guides] [--empty] [--no-png]
// Writes build/preview/panel.html and (via headless Edge/Chrome) build/preview/panel.png.
//   --guides  overlay control footprints and screen outlines
//   --empty   screens show the empty state instead of two demo stems
// Set RACK_DIR if Rack is not in C:/Program Files/VCV/Rack2Free (used for component SVGs and the font).
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { pathToFileURL } from 'node:url';
import { root, L, FOOT, PX, CODE, controls, displays, labels, r, esc } from './panel_model.mjs';

const args = process.argv.slice(2);
const flag = f => args.includes(f);
const svgPath = path.resolve(root, args.find(a => !a.startsWith('--')) || 'res/TapeJam.svg');
const outDir = path.join(root, 'build/preview');
const SCALE = 10; // screen px per mm

const rackDir = [process.env.RACK_DIR, 'C:/Program Files/VCV/Rack2Free', 'C:/Program Files/VCV/Rack2Pro']
  .find(d => d && fs.existsSync(path.join(d, 'res/ComponentLibrary')));
if (!rackDir) console.warn('Rack install not found (set RACK_DIR): drawing plain circles and a fallback font.');

// ---- panel artwork ---------------------------------------------------------
const art = fs.readFileSync(svgPath, 'utf8').replace(/^\s*<\?xml[^>]*>\s*/, '');
const artInner = art.replace(/^[\s\S]*?<svg[^>]*>/, '').replace(/<\/svg>\s*$/, '');

// ---- components ------------------------------------------------------------
const compCache = {};
function component(name) {
  if (compCache[name] !== undefined) return compCache[name];
  const f = rackDir && path.join(rackDir, 'res/ComponentLibrary', name + '.svg');
  if (!f || !fs.existsSync(f)) return (compCache[name] = null);
  const s = fs.readFileSync(f, 'utf8');
  const tag = s.match(/<svg[\s\S]*?>/)[0];
  const dim = a => { const m = tag.match(new RegExp('\\s' + a + '="([\\d.]+)(mm|px)?"')); return m[2] === 'mm' ? +m[1] : +m[1] * PX; };
  return (compCache[name] = { href: 'data:image/svg+xml;base64,' + Buffer.from(s).toString('base64'), w: dim('width'), h: dim('height') });
}

let comps = '';
for (const k of controls) {
  const f = FOOT[k.type];
  const imgs = f.svg.map(component);
  if (imgs.every(Boolean)) {
    for (const im of imgs)
      comps += '<image href="' + im.href + '" x="' + r(k.x - im.w / 2) + '" y="' + r(k.y - im.h / 2) + '" width="' + r(im.w) + '" height="' + r(im.h) + '"/>\n';
  }
  else {
    comps += '<circle cx="' + k.x + '" cy="' + k.y + '" r="' + f.d / 2 + '" fill="#2a2a2a" stroke="#888" stroke-width="0.3"/>\n';
  }
}

// ---- code-drawn screens (mirror LaneDisplay / TransportDisplay, in Rack px) --
const fmtTime = s => { const m = Math.floor(s / 60); return m + ':' + (s - m * 60).toFixed(1).padStart(4, '0'); };
const DEMO = flag('--empty') ? [] : [{ name: 'drums.wav', dur: 192.4, pos: 0.35 }, { name: 'bass.wav', dur: 192.4, pos: 0.35 }];
const text = (x, y, s, px, col, anchor = 'start', base = 'text-before-edge') =>
  '<text x="' + r(x) + '" y="' + r(y) + '" font-size="' + px + '" fill="' + col + '" text-anchor="' + anchor + '" dominant-baseline="' + base + '">' + esc(s) + '</text>\n';

let screens = '';
for (const d of displays) {
  const w = d.w / PX, h = d.h / PX;
  let s = '<rect x="0" y="0" width="' + r(w) + '" height="' + r(h) + '" rx="2" fill="' + CODE.screenFill + '" stroke="' + CODE.screenStroke + '" stroke-width="0.7"/>\n';
  if (d.kind === 'lane') {
    const pad = 4, barY = h - 9, barH = 5, barW = w - 2 * pad;
    const demo = DEMO[d.lane];
    s += text(pad, 3, demo ? demo.name : 'click to load WAV', 10.5, demo ? CODE.laneLoaded : CODE.laneName);
    if (demo) s += text(w - pad, 3, fmtTime(demo.dur), 10.5, CODE.laneTime, 'end');
    s += '<rect x="' + pad + '" y="' + r(barY) + '" width="' + r(barW) + '" height="' + barH + '" fill="' + CODE.barBg + '"/>\n';
    if (demo) {
      const mx = pad + barW * demo.pos;
      s += '<rect x="' + pad + '" y="' + r(barY) + '" width="' + r(mx - pad) + '" height="' + barH + '" fill="' + CODE.barFill + '"/>\n';
      s += '<rect x="' + r(mx - 0.75) + '" y="' + r(barY - 1.5) + '" width="1.5" height="' + (barH + 3) + '" fill="' + CODE.playhead + '"/>\n';
    }
  }
  else {
    const t = DEMO.length ? DEMO[0].dur * DEMO[0].pos : 0, mx = DEMO.length ? DEMO[0].dur : 0;
    s += text(5, 4, fmtTime(t) + ' / ' + fmtTime(mx), 15, CODE.trTime);
    s += text(5, 21, '120.0 BPM INT', 10.5, CODE.trInfo);
  }
  screens += '<g transform="translate(' + r(d.x) + ',' + r(d.y) + ') scale(' + PX + ')">\n' + s + '</g>\n';
}

// ---- runtime labels ------------------------------------------------------
let lbls = '';
if (L.runtimeLabels)
  for (const lb of labels)
    lbls += '<g transform="translate(' + r(lb.x) + ',' + r(lb.y) + ') scale(' + PX + ')">' + text(0, 0, lb.t, lb.px, lb.col, lb.a || 'middle', 'middle') + '</g>\n';

// ---- optional guides -----------------------------------------------------
let guides = '';
if (flag('--guides')) {
  for (const k of controls) {
    const f = FOOT[k.type];
    guides += '<circle cx="' + k.x + '" cy="' + k.y + '" r="' + r(f.d / 2 + 1) + '" fill="none" stroke="' + f.color + '" stroke-width="0.15" stroke-dasharray="0.6,0.6"/>\n';
    guides += '<circle cx="' + k.x + '" cy="' + k.y + '" r="' + r(f.d / 2) + '" fill="none" stroke="' + f.color + '" stroke-width="0.2"/>\n';
  }
  for (const d of displays)
    guides += '<rect x="' + d.x + '" y="' + d.y + '" width="' + d.w + '" height="' + d.h + '" fill="none" stroke="#ff3030" stroke-width="0.25"/>\n';
}

// ---- page ----------------------------------------------------------------
const font = rackDir && path.join(rackDir, 'res/fonts/ShareTechMono-Regular.ttf');
const fontFace = font && fs.existsSync(font)
  ? '@font-face{font-family:RackMono;src:url(data:font/ttf;base64,' + fs.readFileSync(font).toString('base64') + ');}' : '';
const W = Math.round(L.W * SCALE), H = Math.round(L.H * SCALE);
const html = '<!doctype html><html><head><meta charset="utf-8"><title>Tape Jam panel preview</title><style>' + fontFace +
  'html,body{margin:0;background:#202020}svg{display:block}text{font-family:RackMono,monospace}</style></head><body>\n' +
  '<svg xmlns="http://www.w3.org/2000/svg" width="' + W + '" height="' + H + '" viewBox="0 0 ' + L.W + ' ' + L.H + '">\n' +
  '<g id="art">' + artInner + '</g>\n<g id="components">\n' + comps + '</g>\n<g id="screens">\n' + screens +
  '</g>\n<g id="labels">\n' + lbls + '</g>\n<g id="guides">\n' + guides + '</g>\n</svg>\n</body></html>\n';

fs.mkdirSync(outDir, { recursive: true });
const htmlPath = path.join(outDir, 'panel.html');
fs.writeFileSync(htmlPath, html);
console.log('wrote ' + path.relative(root, htmlPath) + ' (' + path.relative(root, svgPath) + ')');

if (!flag('--no-png')) {
  const browser = [process.env.BROWSER,
    'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe', 'C:/Program Files/Microsoft/Edge/Application/msedge.exe',
    'C:/Program Files/Google/Chrome/Application/chrome.exe'].find(b => b && fs.existsSync(b));
  if (!browser) {
    console.warn('No Edge/Chrome found (set BROWSER): open the HTML instead.');
  }
  else {
    const png = path.join(outDir, 'panel.png');
    const res = spawnSync(browser, ['--headless=new', '--disable-gpu', '--hide-scrollbars', '--window-size=' + W + ',' + H,
      '--screenshot=' + png, pathToFileURL(htmlPath).href], { stdio: 'ignore', timeout: 60000 });
    if (res.status === 0 && fs.existsSync(png)) console.log('wrote ' + path.relative(root, png));
    else console.warn('screenshot failed: open the HTML instead.');
  }
}
