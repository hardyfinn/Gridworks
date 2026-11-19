#!/usr/bin/env python3
# Draws res/GridMutes.svg. VCV Rack can't draw SVG <text>, so every label is
# turned into outline paths with fontTools. Positions are in mm and must match
# src/GridMutes.cpp (the numbers in the LAYOUT block are the same in both).
#   pip install fonttools
#   python3 res/make_panel_gridmutes.py
import os
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

HERE = os.path.dirname(os.path.abspath(__file__))
FONT_BOLD = os.environ.get("PANEL_FONT_BOLD", "/usr/share/fonts/opentype/inter/Inter-Bold.otf")
FONT_MED = os.environ.get("PANEL_FONT_MED", "/usr/share/fonts/opentype/inter/Inter-Medium.otf")

W, H = 223.52, 128.5  # 44 HP

BG = "#262329"
SCREEN = "#121014"
INK = "#EEE8EF"
SOFT = "#9A919E"
LINE = "#3E3842"
OUT_BG = "#1A171C"
GOLD = "#E6B450"

# ---- LAYOUT (same numbers in src/GridMutes.cpp) ----
IN_X = [18.65, 29.15, 39.65]            # input jacks: 3 columns of 6 (ch 1-6, 7-12, 13-18)
OUT_X = [56.85, 67.35, 77.85]           # output jacks, same order
CH_Y0, CH_DY = 26.0, 11.6
LIGHT_DX, LIGHT_DY = 4.3, -4.6        # mute light, up and right of each output
CELL = 7.5
GX0, GY0 = 98.5, 17.0               # on-screen grid
STATUS_Y = 83.0
NUM_CH = 18
JACK_Y = 111.0
X_CLOCK, X_RESET, X_SCENE, X_TRIG = 12.0, 26.0, 44.0, 58.0
X_FADE, X_GRIDLIGHT, X_MIX = 160.0, 182.0, 210.0

CONTROL = 3.2   # control label size (project standard)
JACK = 2.9      # jack label size (project standard)


def ch_y(ch):
    return CH_Y0 + (ch % 6) * CH_DY


fonts = {}


def text(s, x, y, size, font=FONT_BOLD, fill=INK, anchor="middle", spacing=0.04):
    """SVG path for string s; (x, y) is the baseline point in mm."""
    if font not in fonts:
        f = TTFont(font)
        fonts[font] = (f, f.getGlyphSet(), f.getBestCmap(), f["head"].unitsPerEm)
    f, gs, cmap, upm = fonts[font]
    scale = size / upm
    track = spacing * upm
    names = [cmap[ord(c)] for c in s]
    width = sum(gs[n].width + track for n in names) - track
    x0 = {"middle": x - width * scale / 2, "end": x - width * scale}.get(anchor, x)
    pen = SVGPathPen(gs)
    cx = 0.0
    for n in names:
        gs[n].draw(TransformPen(pen, (scale, 0, 0, -scale, x0 + cx * scale, y)))
        cx += gs[n].width + track
    return f'<path fill="{fill}" d="{pen.getCommands()}"/>'


parts = []
add = parts.append
add(f'<rect width="{W}" height="{H}" fill="{BG}"/>')
add(text("GridMutes", 6.0, 9.0, 5.2, FONT_BOLD, INK, anchor="start", spacing=0.02))
add(text("18-channel performance mutes", 35.5, 8.6, 2.4, FONT_MED, SOFT, anchor="start"))
add(text("GRIDWORKS", W - 6.0, 8.6, 2.6, FONT_MED, SOFT, anchor="end", spacing=0.12))

# inputs and outputs: one block each, channels down the columns
for xs, name, fill in [(IN_X, "INPUTS", None), (OUT_X, "OUTPUTS", OUT_BG)]:
    x1, x2 = xs[0] - 6.6, xs[2] + (7.6 if fill else 6.6)
    if fill:
        add(f'<rect x="{x1}" y="12.0" width="{x2 - x1}" height="{CH_Y0 + 5 * CH_DY + 7.0 - 12.0}" rx="1.6" fill="{fill}"/>')
    else:
        add(f'<rect x="{x1}" y="12.0" width="{x2 - x1}" height="{CH_Y0 + 5 * CH_DY + 7.0 - 12.0}" rx="1.6" fill="none" stroke="{LINE}" stroke-width="0.4"/>')
    add(text(name, (x1 + x2) / 2, 18.6, CONTROL))

# screen
add(f'<rect x="{GX0 - 4.0}" y="12.0" width="{W - 3.0 - (GX0 - 4.0)}" height="{STATUS_Y + 4.0 - 12.0}" rx="1.6" fill="{SCREEN}"/>')

# bottom row
add(f'<line x1="3.0" y1="{JACK_Y - 15.0}" x2="{W - 3.0}" y2="{JACK_Y - 15.0}" stroke="{LINE}" stroke-width="0.35"/>')
JL = JACK_Y - 6.4
for x, s in [(X_CLOCK, "CLOCK"), (X_RESET, "RESET"), (X_SCENE, "CV"), (X_TRIG, "TRIG")]:
    add(text(s, x, JL, JACK))
add(f'<rect x="{X_SCENE - 7.0}" y="{JACK_Y - 13.0}" width="{X_TRIG - X_SCENE + 14.0}" height="19.0" rx="1.2" fill="none" stroke="{GOLD}" stroke-width="0.35"/>')
add(text("SCENE", (X_SCENE + X_TRIG) / 2, JACK_Y - 10.2, 2.4, FONT_MED, GOLD))
add(text("FADE", X_FADE, JACK_Y - 7.0, CONTROL))
add(text("1", X_FADE - 5.4, JACK_Y + 7.4, 2.4, FONT_MED, SOFT))
add(text("50ms", X_FADE + 6.4, JACK_Y + 7.4, 2.4, FONT_MED, SOFT))
add(text("GRID", X_GRIDLIGHT, JL, 2.6, FONT_BOLD, SOFT))
add(f'<rect x="{X_MIX - 8.0}" y="{JACK_Y - 13.0}" width="16.0" height="19.0" rx="1.2" fill="{OUT_BG}"/>')
add(text("MIX", X_MIX, JL, JACK))

svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}mm" height="{H}mm" viewBox="0 0 {W} {H}">\n'
       + "\n".join(parts) + "\n</svg>\n")
open(os.path.join(HERE, "GridMutes.svg"), "w").write(svg)
print("ok")

import sys as _sys
_sys.path.insert(0, HERE)
import haer_logo
haer_logo.add_to_file(os.path.join(HERE, "GridMutes.svg"), x=W / 2, y=126.0, cap=3.0, bg=BG)
