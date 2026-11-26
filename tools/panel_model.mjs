// Shared panel model: control positions, screens and runtime labels, built from tools/layout.json.
// Used by gen_panel.mjs (template + Layout.hpp) and preview_panel.mjs (browser preview).
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

export const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
export const L = JSON.parse(fs.readFileSync(path.join(root, 'tools/layout.json'), 'utf8'));
export const NUM_LANES = 6;
export const FX = [['ROLL', '#34d2ff'], ['REV', '#ff52d9'], ['STOP', '#ff9a2e'], ['STUT', '#f5e14a']];

// Rack draws at 75 px per inch; NanoVG sizes in src/TapeJam.cpp are in these px.
export const PX = 25.4 / 75;

// Colours of what src/TapeJam.cpp draws in code. Keep in sync with LaneDisplay, TransportDisplay and PanelLabels.
// Fonts the plugin ships (res/fonts), as loaded by displayFont() / textFont() in src/TapeJam.cpp.
export const FONTS = { display: 'res/fonts/Unbounded-Light.ttf', text: 'res/fonts/HankenGrotesk-Medium.ttf' };

export const CODE = {
  screenFill: '#080f1e', screenStroke: '#24445a',
  title: '#9ee4aa', dim: '#8b9db3',
  laneName: '#648da6', laneLoaded: '#dceBf2'.toLowerCase(), laneTime: '#648da6', barBg: '#14233a', barFill: '#2a6e82', playhead: '#9ee4aa',
  trTime: '#9ee4aa', trInfo: '#8b9db3',
};

// Real footprints of Rack's stock components (mm). svg: ComponentLibrary files the preview draws (bg first).
export const FOOT = {
  jack: { d: 8.03, color: '#3d8bff', label: 'jack (PJ301M) 8.0 mm', svg: ['PJ301M'] },
  jackOut: { d: 8.03, color: '#3d8bff', label: 'jack (PJ301M) 8.0 mm', svg: ['PJ301M-dark'] },
  knob: { d: 7.68, color: '#3ecf6a', label: 'knob (RoundSmallBlack) 7.7 mm', svg: ['RoundSmallBlackKnob_bg', 'RoundSmallBlackKnob'] },
  trim: { d: 6.05, color: '#a6e35b', label: 'trimpot 6.0 mm', svg: ['Trimpot_bg', 'Trimpot'] },
  button: { d: 6.10, color: '#ff8a3d', label: 'lit button (VCVButton) 6.1 mm', svg: ['VCVButton_0'] },
};

export const laneY = i => L.LANE_Y0 + i * L.LANE_DY;
export const fxJackX = f => L.FX_JACK_X0 + f * L.FX_DX;
export const fxBtnX = f => fxJackX(f) + L.FX_BTN_DX;

// ---- controls (mirrors TapeJamWidget) -------------------------------------
export const controls = [];
const c = (type, x, y, name) => controls.push({ type, x, y, name });
for (let i = 0; i < NUM_LANES; i++) {
  const y = laneY(i), n = i + 1;
  FX.forEach(([fx], f) => {
    c('jack', fxJackX(f), y, n + ' ' + fx + ' in');
    c('button', fxBtnX(f), y, n + ' ' + fx);
  });
  c('knob', L.LEN_X, y, n + ' LEN');
  c('jack', L.LEN_CV_X, y, n + ' LEN cv');
  c('knob', L.LVL_X, y, n + ' LVL');
  c('jack', L.LVL_CV_X, y, n + ' LVL cv');
  c('button', L.MUTE_X, y, n + ' MUTE');
  c('jack', L.MUTE_CV_X, y, n + ' MUTE cv');
  c('jackOut', L.OUTL_X, y, n + ' L out');
  c('jackOut', L.OUTR_X, y, n + ' R out');
}
const ty = L.TR_Y;
c('jack', L.CLOCK_X, ty, 'CLOCK in');
c('knob', L.BPM_X, ty, 'BPM');
c('trim', L.OFFSET_X, ty, 'OFFSET');
c('button', L.SYNC_X, ty, 'SYNC');
c('button', L.PLAY_BTN_X, ty, 'PLAY');
c('jack', L.PLAY_IN_X, ty, 'PLAY in');
c('button', L.RESET_BTN_X, ty, 'RESET');
c('jack', L.RESET_IN_X, ty, 'RESET in');
c('jack', L.SEEK_CV_X, ty, 'SEEK cv');
c('jack', L.SEEK_TRIG_X, ty, 'SEEK trig');
c('jackOut', L.POS_X, ty, 'POS out');

// Opaque screens drawn by code on top of the panel.
export const displays = [];
for (let i = 0; i < NUM_LANES; i++)
  displays.push({ kind: 'lane', lane: i, x: L.DISP_X0, y: laneY(i) - L.DISP_H / 2, w: L.DISP_W, h: L.DISP_H, name: 'lane ' + (i + 1) + ' display' });
displays.push({ kind: 'transport', x: L.TRD_X0, y: L.TR_LABEL_Y - 3, w: L.TRD_X1 - L.TRD_X0, h: 14, name: 'transport display' });

// Text drawn at runtime by PanelLabels when runtimeLabels is true. px = NanoVG font size; y is the text middle.
export const labels = [
  { x: 5, y: 7.5, t: 'TAPE JAM', px: 16, col: CODE.title, a: 'start', font: 'display' },
  { x: 41, y: 7.5, t: 'flux stem player', px: 9, col: CODE.dim, a: 'start' },
  ...FX.map(([t, col], f) => ({ x: fxJackX(f) + L.FX_BTN_DX / 2, y: 12.5, t, px: 9, col })),
  ...[['LEN', L.LEN_X], ['CV', L.LEN_CV_X], ['LVL', L.LVL_X], ['CV', L.LVL_CV_X], ['MUTE', L.MUTE_X], ['CV', L.MUTE_CV_X], ['L', L.OUTL_X], ['R', L.OUTR_X]]
    .map(([t, x]) => ({ x, y: 12.5, t, px: 9, col: CODE.dim })),
  ...Array.from({ length: NUM_LANES }, (_, i) => ({ x: 4, y: laneY(i), t: String(i + 1), px: 11, col: CODE.dim })),
  ...[['CLOCK', L.CLOCK_X], ['BPM', L.BPM_X], ['OFFS', L.OFFSET_X], ['SYNC', L.SYNC_X],
      ['PLAY', (L.PLAY_BTN_X + L.PLAY_IN_X) / 2], ['RESET', (L.RESET_BTN_X + L.RESET_IN_X) / 2],
      ['SEEK', L.SEEK_CV_X], ['TRIG', L.SEEK_TRIG_X], ['POS', L.POS_X]].map(([t, x]) => ({ x, y: L.TR_LABEL_Y, t, px: 8, col: CODE.dim })),
];

export const r = v => Math.round(v * 1000) / 1000;
export const esc = s => s.replace(/&/g, '&amp;').replace(/</g, '&lt;');
