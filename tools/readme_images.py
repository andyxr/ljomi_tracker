# README images from the panel preview. Run from the repo root after `node tools/preview_panel.mjs`:
#   python tools/readme_images.py build/preview/panel.png docs/images res/fonts/HankenGrotesk-Medium.ttf
# Needs Pillow. Region boxes are in panel mm (see tools/layout.json); update them if the layout moves.
import sys
from PIL import Image, ImageDraw, ImageFont
src, outdir, font = sys.argv[1], sys.argv[2], sys.argv[3]
im = Image.open(src).convert('RGB')
W, H = im.size
S = W / 213.36  # px per mm
def mm(x): return round(x * S)

# 1. plain panel
im.resize((1400, round(H * 1400 / W)), Image.LANCZOS).save(outdir + '/panel.png', optimize=True)

# 2. annotated panel
regions = [
  ('1', 'Lane screens', (5.5, 15.2, 42.5, 104.0), '#9ee4aa'),
  ('2', 'Effect gates', (42.8, 9.8, 104.2, 104.0), '#34d2ff'),
  ('3', 'Lane controls', (104.6, 9.8, 184.0, 104.0), '#ff9a2e'),
  ('4', 'Outputs', (184.4, 9.8, 209.6, 104.0), '#c9a0ff'),
  ('5', 'Tempo', (3.5, 106.5, 48.5, 126.8), '#f5e14a'),
  ('6', 'Transport', (49.5, 106.5, 128.0, 126.8), '#ff52d9'),
  ('7', 'Transport screen', (129.5, 106.5, 210.5, 124.5), '#9ee4aa'),
]
M = mm(4.5)
ann = Image.new('RGB', (W, H + M), (11, 20, 38))
ann.paste(im, (0, 0))
ov = Image.new('RGBA', ann.size, (0, 0, 0, 0))
d = ImageDraw.Draw(ov)
f = ImageFont.truetype(font, mm(4.2))
for n, _, (x0, y0, x1, y1), col in regions:
    d.rounded_rectangle((mm(x0), mm(y0), mm(x1), mm(y1)), radius=mm(1.2), outline=col, width=mm(0.5))
for n, _, (x0, y0, x1, y1), col in regions:
    cx, cy, r = mm((x0 + x1) / 2), mm(y1), mm(2.6)
    d.ellipse((cx - r - mm(0.5), cy - r - mm(0.5), cx + r + mm(0.5), cy + r + mm(0.5)), fill='#0b1426')
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=col)
    d.text((cx, cy), n, font=f, fill='#0b1426', anchor='mm')
ann = Image.alpha_composite(ann.convert('RGBA'), ov).convert('RGB')
ann.resize((1400, round((H + M) * 1400 / W)), Image.LANCZOS).save(outdir + '/panel-annotated.png', optimize=True)

# 3. one lane with the header row
lane = im.crop((0, mm(9.5), W, mm(30.5)))
lane.resize((1400, round(lane.size[1] * 1400 / W)), Image.LANCZOS).save(outdir + '/lane.png', optimize=True)

# 4. transport strip
tr = im.crop((0, mm(105), W, H))
tr.resize((1400, round(tr.size[1] * 1400 / W)), Image.LANCZOS).save(outdir + '/transport.png', optimize=True)
print('ok', W, H)
