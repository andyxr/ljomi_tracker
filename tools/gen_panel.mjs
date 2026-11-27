#!/usr/bin/env node
// Generates panel geometry outputs from tools/layout.json.
//   node tools/gen_panel.mjs                  -> src/Layout.hpp + design/TapeJam-template.svg
//   node tools/gen_panel.mjs --default-panel  -> also overwrites res/TapeJam.svg with the stock artwork
import fs from 'node:fs';
import path from 'node:path';
import { root, L, NUM_LANES, FOOT, PX, controls, displays, labels, laneY, r, esc } from './panel_model.mjs';

// ---- stock artwork (also the starting point inside the template) --------
function stockArtwork() {
  let s = '<rect x="0" y="0" width="' + L.W + '" height="' + L.H + '" fill="#1b1d22"/>\n';
  s += '<rect x="0" y="0" width="' + L.W + '" height="15" fill="#14161a"/>\n';
  s += '<rect x="0" y="0" width="' + L.W + '" height="1.2" fill="#e0703a"/>\n';
  for (let i = 0; i < NUM_LANES; i++) {
    const y = laneY(i) - 7;
    s += '<rect x="0" y="' + y + '" width="' + L.W + '" height="14" fill="' + (i % 2 ? '#1f2127' : '#23262d') + '"/>\n';
    s += '<rect x="' + (L.FX_JACK_X0 - 5) + '" y="' + (y + 1) + '" width="' + 4 * L.FX_DX + '" height="12" rx="1.5" fill="#2b2f37"/>\n';
    s += '<rect x="' + (L.OUTL_X - 5.5) + '" y="' + (y + 1) + '" width="' + (L.OUTR_X - L.OUTL_X + 11) + '" height="12" rx="1.5" fill="#33302b"/>\n';
  }
  s += '<rect x="0" y="' + (L.TR_LABEL_Y - 4.5) + '" width="' + L.W + '" height="' + r(L.H - L.TR_LABEL_Y + 4.5) + '" fill="#14161a"/>\n';
  s += '<rect x="0" y="' + (L.TR_LABEL_Y - 4.5) + '" width="' + L.W + '" height="0.4" fill="#e0703a"/>\n';
  s += '<rect x="' + (L.POS_X - 5.5) + '" y="' + (L.TR_Y - 5.5) + '" width="11" height="11" rx="1.5" fill="#33302b"/>\n';
  return s;
}

// ---- guide layer -------------------------------------------------------
function guides() {
  let g = '';
  // HP grid
  for (let x = 0; x <= L.W + 0.01; x += 5.08)
    g += '<line x1="' + r(x) + '" y1="0" x2="' + r(x) + '" y2="' + L.H + '" stroke="#ffffff" stroke-opacity="0.08" stroke-width="0.15"/>\n';
  // Lane bands
  for (let i = 0; i < NUM_LANES; i++)
    g += '<rect x="0.3" y="' + (laneY(i) - 7) + '" width="' + r(L.W - 0.6) + '" height="14" fill="none" stroke="#ffffff" stroke-opacity="0.25" stroke-width="0.2" stroke-dasharray="1,1"/>\n';
  // Displays
  for (const d of displays) {
    g += '<rect x="' + r(d.x) + '" y="' + r(d.y) + '" width="' + r(d.w) + '" height="' + r(d.h) + '" fill="#ff3030" fill-opacity="0.18" stroke="#ff3030" stroke-width="0.25"/>\n';
    g += '<text x="' + r(d.x + d.w / 2) + '" y="' + r(d.y + d.h / 2 + 0.6) + '" font-family="monospace" font-size="1.8" fill="#ff8080" text-anchor="middle">' + esc(d.name) + ' (opaque, drawn by code)</text>\n';
  }
  // Controls: footprint, 1 mm keep-clear ring, centre cross, name
  for (const k of controls) {
    const f = FOOT[k.type], rad = f.d / 2;
    g += '<circle cx="' + r(k.x) + '" cy="' + r(k.y) + '" r="' + r(rad + 1) + '" fill="none" stroke="' + f.color + '" stroke-opacity="0.5" stroke-width="0.15" stroke-dasharray="0.6,0.6"/>\n';
    g += '<circle cx="' + r(k.x) + '" cy="' + r(k.y) + '" r="' + r(rad) + '" fill="' + f.color + '" fill-opacity="0.25" stroke="' + f.color + '" stroke-width="0.25"/>\n';
    g += '<path d="M' + r(k.x - 1) + ',' + r(k.y) + 'H' + r(k.x + 1) + 'M' + r(k.x) + ',' + r(k.y - 1) + 'V' + r(k.y + 1) + '" stroke="' + f.color + '" stroke-width="0.15"/>\n';
    g += '<text x="' + r(k.x) + '" y="' + r(k.y + rad + 2.2) + '" font-family="monospace" font-size="1.4" fill="' + f.color + '" text-anchor="middle">' + esc(k.name) + '</text>\n';
  }
  // Runtime label positions
  for (const lb of labels) {
    const s = r(lb.px * PX);
    g += '<text x="' + r(lb.x) + '" y="' + r(lb.y + s * 0.35) + '" font-family="monospace" font-size="' + s + '" fill="#ffffff" fill-opacity="0.45" text-anchor="' + (lb.a || 'middle') + '">' + esc(lb.t) + '</text>\n';
  }
  // Legend, outside the page so it never exports
  const lx = L.W + 6;
  let ly = 6;
  const line = (txt, col = '#cccccc', size = 2.6) => { g += '<text x="' + lx + '" y="' + ly + '" font-family="sans-serif" font-size="' + size + '" fill="' + col + '">' + esc(txt) + '</text>\n'; ly += size * 1.5; };
  line('LJOMI TRACKER panel template', '#e0703a', 4);
  line('Page: ' + L.W + ' x ' + L.H + ' mm (' + Math.round(L.W / 5.08) + ' HP). Draw in the "Artwork" layer.');
  line('This GUIDES layer is locked; hide or delete it before saving to res/TapeJam.svg.');
  ly += 2;
  for (const k of Object.values(FOOT).filter((k, i, a) => a.findIndex(o => o.label === k.label) === i)) {
    g += '<circle cx="' + (lx + 1.5) + '" cy="' + (ly - 0.9) + '" r="1.4" fill="' + k.color + '" fill-opacity="0.4" stroke="' + k.color + '" stroke-width="0.25"/>\n';
    g += '<text x="' + (lx + 4) + '" y="' + ly + '" font-family="sans-serif" font-size="2.6" fill="#cccccc">' + esc(k.label + ' (dashed ring = 1 mm clearance)') + '</text>\n';
    ly += 4;
  }
  g += '<rect x="' + lx + '" y="' + (ly - 2.6) + '" width="3" height="2.6" fill="#ff3030" fill-opacity="0.3" stroke="#ff3030" stroke-width="0.25"/>\n';
  g += '<text x="' + (lx + 4) + '" y="' + ly + '" font-family="sans-serif" font-size="2.6" fill="#cccccc">screen drawn by code: artwork under it is hidden</text>\n';
  ly += 4;
  line('Faint white text = labels currently drawn by code (runtimeLabels: ' + L.runtimeLabels + ').', '#aaaaaa');
  line('Put your own lettering in the artwork as PATHS, then set runtimeLabels false.', '#aaaaaa');
  ly += 2;
  line('Rack cannot render: <text>, embedded images, CSS <style>, filters/blur,', '#ff8080');
  line('masks, clip paths, <use>/symbols, patterns. Check with: node tools/check_panel.mjs', '#ff8080');
  line('Moving a control? Edit tools/layout.json and re-run tools/gen_panel.mjs.', '#aaaaaa');
  return g;
}

// ---- writers -------------------------------------------------------------
const numKeys = Object.entries(L).filter(([k, v]) => typeof v === 'number');
const hpp = '// GENERATED by tools/gen_panel.mjs from tools/layout.json - do not edit by hand. Units: mm.\n' +
  '#pragma once\nnamespace layout {\n' +
  numKeys.map(([k, v]) => 'static const float ' + k + ' = ' + (Number.isInteger(v) ? v + '.0' : v) + 'f;').join('\n') +
  '\nstatic const bool RUNTIME_LABELS = ' + (L.runtimeLabels ? 'true' : 'false') + ';\n' +
  'inline float laneY(int i) { return LANE_Y0 + i * LANE_DY; }\n' +
  'inline float fxJackX(int f) { return FX_JACK_X0 + f * FX_DX; }\n' +
  'inline float fxBtnX(int f) { return FX_JACK_X0 + f * FX_DX + FX_BTN_DX; }\n' +
  '} // namespace layout\n';

const NS = 'xmlns="http://www.w3.org/2000/svg" xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape" xmlns:sodipodi="http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd"';
const head = '<?xml version="1.0" encoding="UTF-8"?>\n<svg ' + NS + ' width="' + L.W + 'mm" height="' + L.H + 'mm" viewBox="0 0 ' + L.W + ' ' + L.H + '">\n' +
  '<sodipodi:namedview id="namedview" inkscape:document-units="mm" pagecolor="#505050" inkscape:pagecheckerboard="false"/>\n';

const template = head +
  '<g id="artwork" inkscape:groupmode="layer" inkscape:label="Artwork">\n' + stockArtwork() + '</g>\n' +
  '<g id="guides" inkscape:groupmode="layer" inkscape:label="GUIDES - hide before saving to res" sodipodi:insensitive="true">\n' + guides() + '</g>\n</svg>\n';

const panel = '<?xml version="1.0" encoding="UTF-8"?>\n<svg xmlns="http://www.w3.org/2000/svg" width="' + L.W + 'mm" height="' + L.H + 'mm" viewBox="0 0 ' + L.W + ' ' + L.H + '">\n' + stockArtwork() + '</svg>\n';

fs.mkdirSync(path.join(root, 'design'), { recursive: true });
fs.writeFileSync(path.join(root, 'src/Layout.hpp'), hpp);
fs.writeFileSync(path.join(root, 'design/TapeJam-template.svg'), template);
console.log('wrote src/Layout.hpp, design/TapeJam-template.svg (' + controls.length + ' controls, ' + displays.length + ' screens)');
if (process.argv.includes('--default-panel')) {
  fs.writeFileSync(path.join(root, 'res/TapeJam.svg'), panel);
  console.log('wrote res/TapeJam.svg (stock artwork)');
}
