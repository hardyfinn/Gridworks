#!/usr/bin/env python3
# Draws res/LessConcepts.svg. VCV Rack can't draw SVG <text>, so every label is
# turned into outline paths with fontTools. Positions are in mm and must match
# src/LessConcepts.cpp (the numbers in the LAYOUT block are the same in both).
#   pip install fonttools
#   python3 res/make_panel_lessconcepts.py
import os
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

HERE = os.path.dirname(os.path.abspath(__file__))
FONT_BOLD = os.environ.get("PANEL_FONT_BOLD", "/usr/share/fonts/opentype/inter/Inter-Bold.otf")
FONT_MED = os.environ.get("PANEL_FONT_MED", "/usr/share/fonts/opentype/inter/Inter-Medium.otf")

W, H = 142.24, 128.5  # 28 HP

BG = "#23262B"
SCREEN = "#111316"
INK = "#E8ECEF"
SOFT = "#8E979F"
LINE = "#3B4048"
OUT_BG = "#181B1F"
V1_COL = "#7FD1C1"
V2_COL = "#F0A868"

# ---- LAYOUT (same numbers in src/LessConcepts.cpp) ----
CELL = 6.0
GX0, GY0 = 23.0, 18.0
STATUS_Y = 71.0
KNOB_Y = 88.0
X_SEED, X_RULE, X_SCALE, X_TRANSP, X_PROB1, X_PROB2 = 12.0, 30.0, 52.0, 72.0, 94.0, 114.0
X_GRIDLIGHT = 132.0
JACK_Y = 111.0
X_CLOCK, X_RESET, X_SEEDCV, X_RULECV = 42.0, 54.0, 12.0, 30.0
X_VOCT1, X_GATE1, X_VOCT2, X_GATE2, X_SEEDOUT, X_STEP = 68.5, 81.5, 97.0, 110.0, 122.5, 134.0

CONTROL = 3.2   # control label size (project standard)
JACK = 2.9      # jack label size (project standard)


def row_y(r):
    return GY0 + r * CELL + CELL / 2


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

add(text("LessConcepts", 6.0, 9.0, 5.2, FONT_BOLD, INK, anchor="start", spacing=0.02))
add(text("after dan derks + linus schrab", 52.0, 8.6, 2.4, FONT_MED, SOFT, anchor="start"))
add(text("GRIDWORKS", W - 6.0, 8.6, 2.6, FONT_MED, SOFT, anchor="end", spacing=0.12))

# screen with the grid replica
add(f'<rect x="3.0" y="{GY0 - 7.0}" width="{W - 6.0}" height="{STATUS_Y + 3.5 - (GY0 - 7.0)}" rx="1.6" fill="{SCREEN}"/>')
hy = GY0 - 1.6
add(text("BITS 8 ... 1", GX0 + 4 * CELL, hy, JACK, FONT_BOLD, SOFT))
add(text("OFF", GX0 + 8.5 * CELL, hy, 2.4, FONT_BOLD, SOFT))
add(text("OCTAVE -3 ... +3", GX0 + 12.5 * CELL, hy, JACK, FONT_BOLD, SOFT))
labels = [("V1", V1_COL), ("V2", V2_COL), ("RAND", INK), ("1-16", INK), ("17-32", INK), ("KEYS", INK), ("SNAP", INK), ("TIME", INK)]
for r, (s, c) in enumerate(labels):
    add(text(s, GX0 - 1.8, row_y(r) + 1.05, JACK, FONT_BOLD, c, anchor="end"))

# knobs
add(f'<line x1="3.0" y1="{KNOB_Y - 11.0}" x2="{W - 3.0}" y2="{KNOB_Y - 11.0}" stroke="{LINE}" stroke-width="0.35"/>')
KL = KNOB_Y - 7.0
for x, s, c in [(X_SEED, "SEED", INK), (X_RULE, "RULE", INK), (X_SCALE, "SCALE", INK), (X_TRANSP, "TRANSP", INK),
                (X_PROB1, "PROB 1", V1_COL), (X_PROB2, "PROB 2", V2_COL)]:
    add(text(s, x, KL, CONTROL, FONT_BOLD, c))
add(text("GRID", X_GRIDLIGHT, KL, JACK))

# jacks: inputs on the panel, outputs on a dark band
add(f'<line x1="3.0" y1="{JACK_Y - 12.0}" x2="{W - 3.0}" y2="{JACK_Y - 12.0}" stroke="{LINE}" stroke-width="0.35"/>')
JL = JACK_Y - 6.4
for x, s_ in [(X_CLOCK, "CLOCK"), (X_RESET, "RESET"), (X_SEEDCV, "CV"), (X_RULECV, "CV")]:
    add(text(s_, x, JL, JACK))
bx = X_VOCT1 - 7.5
add(f'<rect x="{bx}" y="{JACK_Y - 10.5}" width="{W - 3.0 - bx}" height="16.5" rx="1.2" fill="{OUT_BG}"/>')
for x, s_, c in [(X_VOCT1, "V/OCT 1", V1_COL), (X_GATE1, "GATE 1", V1_COL), (X_VOCT2, "V/OCT 2", V2_COL),
                 (X_GATE2, "GATE 2", V2_COL), (X_SEEDOUT, "SEED", INK), (X_STEP, "STEP", INK)]:
    add(text(s_, x, JL, JACK, FONT_BOLD, c))

svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}mm" height="{H}mm" viewBox="0 0 {W} {H}">\n'
       + "\n".join(parts) + "\n</svg>\n")
open(os.path.join(HERE, "LessConcepts.svg"), "w").write(svg)
print("ok")

# --- Haer maker logo (res/haer_logo.py) on the bottom rail
import sys as _sys
_sys.path.insert(0, HERE)
import haer_logo
haer_logo.add_to_file(os.path.join(HERE, "LessConcepts.svg"), x=W / 2, y=126.0, cap=3.0, bg=BG)
