# Ljomi Tracker

A six-lane stem player for VCV Rack 2 with Traktor-style Flux effects. Each lane has loop roll, reverse,
tape stop and stutter; when an effect is released the lane resumes exactly where it would have been
had the effect never happened.

## Building (Windows)

1. Install [MSYS2](https://www.msys2.org/), then in an MSYS2 shell:
   `pacman -S --needed make zip unzip jq python mingw-w64-x86_64-gcc`
2. Download the Rack 2 SDK and unzip it to `dep/Rack-SDK`:
   https://vcvrack.com/downloads/Rack-SDK-latest-win-x64.zip
3. From the repo root (PowerShell or Git Bash):
   - `sh build.sh`: build `plugin.dll`
   - `sh build.sh devinstall`: build and copy into your Rack plugins folder (close Rack first)
   - `sh build.sh test`: offline engine tests; writes demo WAVs to `tests/out/`

## Layout

- `src/FluxEngine.hpp`: the playback/effects engine (no Rack dependency, unit-tested)
- `src/TapeJam.cpp`: the Rack module and its widgets
- `tools/layout.json`: panel geometry, the single source of truth. `node tools/gen_panel.mjs` regenerates
  `src/Layout.hpp` and the design template.
- `design/`: panel design template and re-skinning guide
- `src/dr_wav.h`: [dr_wav](https://github.com/mackron/dr_libs) (public domain / MIT-0)

## License

Copyright (C) 2026 Andy Deighton

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public
License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later
version. See [LICENSE.txt](LICENSE.txt).

The fonts in `res/fonts/` (Unbounded, Hanken Grotesk) are under the SIL Open Font License; their licence files sit
beside them.
