# Tape Jam: notes for Claude

VCV Rack 2 plugin (slug `TapeJam`, one module). A 6-lane stem player for multi-minute WAVs. All lanes play
from one master clock, and each lane has DJ effects in Traktor **Flux mode**: when an effect is released, the
lane resumes exactly where it would have been had the effect never been engaged.

## Core model (src/FluxEngine.hpp, no Rack dependency)
- One master position `t` (seconds of *stem time*) always advances. Each lane renders from a `Voice`, a
  **pure function of t**. Releasing an effect = crossfade (8 ms, equal-power) back to the plain voice (pos = t).
  Keep effects stateless functions of t. That is what makes Flux return exact and seek/crossfade trivial.
- Effects: roll (repeats the grid slice containing the engage point, so engage is seamless; 4 ms pre-roll
  fade at wraps), reverse, tape stop (linear speed ramp over LEN), stutter (grid gate, open first half).
  Effect length is latched on the rising edge.
- Multiple gates on a lane: the **most recently engaged still-held** effect wins; releasing it falls back to
  the older held one. Seek/reset cancels all effects until their gates re-trigger.
- Samples are stored as int16 interleaved stereo at file rate, read with 4-point Hermite (handles SR mismatch
  and varispeed). 24-bit/float files are reduced to 16-bit on load (deliberate, for memory).

## Decisions made with the user
- 6 lanes; a **dedicated gate jack + lit button per effect**; LEN knob per lane (1/64 to 2 bars), level, mute.
- Stereo out per lane; if only L is patched, L carries the mono sum (L+R)/2.
- WAV only. Transport: play/stop, reset, seek CV + trigger, position out; stops at end of the longest stem, or
  with **Loop** on (context menu, saved in the patch) wraps to 0 via `seekTo` (so held effects cancel like any seek).
- Tempo: BPM knob = stems' tempo (free running). Clock patched -> sets the grid. SYNC on -> knob = native BPM
  and the clock **varispeeds** the stems (pitch changes; time-stretch deliberately not done).
- Loading: click a lane display, or the context menu (per lane, or a folder -> first 6 WAVs alphabetically).

## Threading rule
Sample hand-off is loader thread -> `pending` -> audio thread swaps into `current` -> `garbage` -> UI thread
deletes in `ModuleWidget::step()`. **The audio thread never allocates or frees sample memory.** Keep it so.

## Build / test (Windows, MSYS2 MinGW64 at C:\msys64, SDK in dep/Rack-SDK)
- `sh build.sh` / `sh build.sh devinstall` (Rack must be closed) / `sh build.sh test`.
- build.sh passes RACK_USER_DIR explicitly. LOCALAPPDATA is not visible inside the MSYS login shell, and
  without it make installs to C:\msys64\Rack2.
- Engine changes: extend `tests/flux_test.cpp`. The key invariant is **bit-identical output vs an untouched
  reference lane after release**, with no sample step above the click threshold. Demo WAVs go to tests/out/.
- Smoke-test in Rack without touching the user's setup: copy the plugin into a scratch
  `<dir>/plugins-win-x64/TapeJam` and run `Rack.exe -h -u <dir> patch.vcv` (plain-JSON patch works), then
  read `<dir>/log.txt`.

## Panel
- `tools/layout.json` is the **single source of truth** for geometry (mm). `node tools/gen_panel.mjs`
  regenerates `src/Layout.hpp` (do not hand-edit) and `design/TapeJam-template.svg`. Pass
  `--default-panel` only to restore the stock `res/TapeJam.svg`; otherwise the user's artwork is never touched.
- Labels are drawn at runtime (`PanelLabels`) while `runtimeLabels` is true. Screens and effect colours
  (`fxColor()`) are drawn in code with NanoVG.
- `node tools/check_panel.mjs` lints res/*.svg for things nanosvg can't render.
- `node tools/preview_panel.mjs [svg] [--guides] [--empty]` -> `build/preview/panel.png` via headless Edge, using
  Rack's own component SVGs and font from the Rack install. Control/screen/label data lives in
  `tools/panel_model.mjs` (shared with gen_panel). Its `CODE` colours mirror `src/TapeJam.cpp`: change both together.
- Rack component footprints: jack 8.0 mm, small knob 7.7, trimpot 6.0, VCVButton 6.1. The effect jack/button
  pairs are tight (0.45 mm gaps).

## Conventions
- LF line endings everywhere (.gitattributes enforces it; the user's global autocrlf is on).
- Rack code style: tabs, Rack 2 API (configSwitch/configButton, createLightParamCentered...).

## Open items
- `plugin.json` license is a placeholder ("proprietary"); author fields are empty. The slug `TapeJam` is
  permanent once published.
- SYNC follows clock tempo but not phase (no beat-position lock). OFFS nudges the grid manually.
- Re-skin pending: the user may supply new panel art via the design template.
