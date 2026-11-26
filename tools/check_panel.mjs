#!/usr/bin/env node
// Checks panel SVGs for things Rack's renderer (nanosvg) can't draw.
//   node tools/check_panel.mjs [file.svg ...]   (default: res/*.svg)
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const L = JSON.parse(fs.readFileSync(path.join(root, 'tools/layout.json'), 'utf8'));
let files = process.argv.slice(2);
if (!files.length)
  files = fs.readdirSync(path.join(root, 'res')).filter(f => f.endsWith('.svg')).map(f => path.join(root, 'res', f));

// Remove hidden groups (display:none), which Rack skips, so their content isn't flagged.
function stripHidden(svg) {
  let out = svg, changed = true;
  while (changed) {
    changed = false;
    out = out.replace(/<g\b[^>]*(?:display\s*:\s*none|display="none")[^>]*>(?:(?!<g\b)[\s\S])*?<\/g>/, () => { changed = true; return ''; });
  }
  return out;
}

const RULES = [
  [/<text\b/, 'error', 'contains <text>: Rack ignores it. Convert text to paths (Inkscape: Path > Object to Path).'],
  [/<image\b/, 'error', 'contains an embedded image: Rack cannot draw bitmaps in panels.'],
  [/<style\b/, 'error', 'contains a CSS <style> block: Rack ignores classes. Use inline attributes (Illustrator: Styling = Presentation Attributes).'],
  [/\bfilter\s*[=:]/, 'warn', 'uses filters (blur/shadow): they will not render.'],
  [/\bmask\s*[=:]/, 'warn', 'uses masks: they will not render.'],
  [/clip-path\s*[=:]/, 'warn', 'uses clip paths: they are ignored, so clipped content shows in full.'],
  [/<use\b/, 'warn', 'uses <use>/clones: they will not render. Unlink clones first.'],
  [/<pattern\b/, 'warn', 'uses pattern fills: they will not render.'],
];

let bad = 0;
for (const f of files) {
  const raw = fs.readFileSync(f, 'utf8');
  const svg = stripHidden(raw);
  const issues = [];
  const tag = (raw.match(/<svg\b[^>]*>/) || [''])[0];
  const w = (tag.match(/\swidth="([\d.]+)mm"/) || [])[1], h = (tag.match(/\sheight="([\d.]+)mm"/) || [])[1];
  if (!w || !h) issues.push(['warn', 'width/height are not in mm; expected ' + L.W + 'mm x ' + L.H + 'mm.']);
  else if (Math.abs(w - L.W) > 0.05 || Math.abs(h - L.H) > 0.05) issues.push(['error', 'size is ' + w + ' x ' + h + ' mm; expected ' + L.W + ' x ' + L.H + ' mm.']);
  if (/id="guides"/.test(svg)) issues.push(['warn', 'the GUIDES layer is still visible; hide or delete it.']);
  for (const [re, level, msg] of RULES) if (re.test(svg)) issues.push([level, msg]);
  console.log(path.relative(root, f) + ': ' + (issues.length ? '' : 'OK'));
  for (const [level, msg] of issues) {
    console.log('  ' + level.toUpperCase() + ': ' + msg);
    if (level === 'error') bad++;
  }
}
process.exit(bad ? 1 : 0);
