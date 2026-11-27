# Re-skinning Ljomi Tracker

1. Open `design/TapeJam-template.svg` in Inkscape (recommended), Illustrator or Figma.
   Page: 213.36 x 128.5 mm (42 HP). In Inkscape the legend sits to the right of the page.
2. Draw in the **Artwork** layer. It starts with the stock artwork; delete or keep it.
   The **GUIDES** layer is locked and shows every control's footprint, a 1 mm keep-clear
   ring, the screens drawn by code (red: anything under them is hidden), and where the
   current runtime labels sit.
3. Convert all lettering to paths. Then set `"runtimeLabels": false` in
   `tools/layout.json` and run `node tools/gen_panel.mjs` so the code stops drawing labels.
4. Hide or delete GUIDES and save as **plain SVG** to `res/TapeJam.svg`.
5. `node tools/check_panel.mjs` flags anything Rack can't render.
   `node tools/preview_panel.mjs [file.svg]` renders the panel as Rack will show it (real Rack
   components, code-drawn screens and labels) to `build/preview/panel.png`, with no build or Rack
   restart. Add `--guides` for footprints, `--empty` for empty screens.
6. `sh build.sh devinstall` with Rack closed.

To move a control, edit `tools/layout.json` and run `node tools/gen_panel.mjs`. That
rewrites `src/Layout.hpp` and the template, and never touches `res/TapeJam.svg` unless you
pass `--default-panel` (which restores the stock artwork).
