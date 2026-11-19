#!/usr/bin/env python3
# Draws res/GridKeys.svg. VCV Rack can't draw SVG <text>, so every label is
# turned into outline paths with fontTools. Positions are in mm and must match
# src/GridKeys.cpp (the numbers in the LAYOUT block are the same in both).
#   pip install fonttools
#   python3 res/make_panel_gridkeys.py
import os
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

HERE = os.path.dirname(os.path.abspath(__file__))
FONT_BOLD = os.environ.get("PANEL_FONT_BOLD", "/usr/share/fonts/opentype/inter/Inter-Bold.otf")
FONT_MED = os.environ.get("PANEL_FONT_MED", "/usr/share/fonts/opentype/inter/Inter-Medium.otf")

W, H = 182.88, 128.5  # 36 HP

BG = "#262329"
SCREEN = "#121014"
INK = "#EEE8EF"
SOFT = "#9A919E"
LINE = "#3E3842"
OUT_BG = "#1A171C"
ROOT_COL = "#E6B450"

# ---- LAYOUT (same numbers in src/GridKeys.cpp) ----
CELL = 6.0
GX0, GY0 = 31.0, 18.0
STATUS_Y = 71.0
KNOB_Y = 88.0
X_ROOT, X_SCALE, X_OCT, X_WIDTH, X_ROW, X_GLIDE, X_STRUM = 12.0, 30.0, 48.0, 66.0, 84.0, 102.0, 120.0
X_MODE, X_PLAYS = 140.0, 158.0
JACK_Y = 112.0
X_CHORD, X_SHOW, X_SLOT, X_PLAY, X_LATCH, X_GRIDLIGHT = 11.0, 24.0, 41.0, 54.0, 70.0, 82.0
X_PVOCT, X_PGATE, X_MVOCT, X_MGATE, X_TRIG = 98.0, 112.0, 130.0, 144.0, 165.0

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
add(text("GridKeys", 6.0, 9.0, 5.2, FONT_BOLD, INK, anchor="start", spacing=0.02))
add(text("Push-style note grid", 33.0, 8.6, 2.4, FONT_MED, SOFT, anchor="start"))
add(text("GRIDWORKS", W - 6.0, 8.6, 2.6, FONT_MED, SOFT, anchor="end", spacing=0.12))

add(f'<rect x="3.0" y="{GY0 - 7.0}" width="{W - 6.0}" height="{STATUS_Y + 3.5 - (GY0 - 7.0)}" rx="1.6" fill="{SCREEN}"/>')
add(text("NOTES (WIDTH 4-10)", GX0 + 5 * CELL, GY0 - 1.6, JACK, FONT_BOLD, SOFT))
add(text("CHORDS", GX0 + 12.5 * CELL, GY0 - 1.6, JACK, FONT_BOLD, ROOT_COL))
add(f'<line x1="{GX0 + 10 * CELL}" y1="{GY0}" x2="{GX0 + 10 * CELL}" y2="{GY0 + 8 * CELL}" stroke="{LINE}" stroke-width="0.3"/>')
add(text("CTRL", GX0 + 15.5 * CELL, GY0 - 1.6, JACK, FONT_BOLD, SOFT))
for r, s in enumerate(["OCT +", "OCT -", "LATCH", "KEY/CHR", "ROW", "PLAYS", "", "SUST"]):
    if s:
        add(text(s, GX0 + 16 * CELL + 1.0, row_y(r) + 0.9, 2.2, FONT_BOLD, SOFT, anchor="start"))
add(text("high", GX0 - 1.8, row_y(0) + 0.9, 2.6, FONT_MED, SOFT, anchor="end"))
add(text("low", GX0 - 1.8, row_y(7) + 0.9, 2.6, FONT_MED, SOFT, anchor="end"))

add(f'<line x1="3.0" y1="{KNOB_Y - 11.0}" x2="{W - 3.0}" y2="{KNOB_Y - 11.0}" stroke="{LINE}" stroke-width="0.35"/>')
KL = KNOB_Y - 7.0
for x, s in [(X_ROOT, "ROOT"), (X_SCALE, "SCALE"), (X_OCT, "OCTAVE"), (X_WIDTH, "WIDTH"), (X_ROW, "ROW"), (X_GLIDE, "GLIDE"), (X_STRUM, "STRUM")]:
    add(text(s, x, KL, CONTROL))
add(text("CHROM", X_MODE, KL, 3.0))
add(text("IN KEY", X_MODE, KNOB_Y + 8.4, 3.0))
add(text("PLAYS", X_PLAYS + 2.0, KL - 0.4, 3.0))
for dy, s_ in [(-3.4, "7TH"), (0.0, "TRIAD"), (3.4, "NOTE")]:
    add(text(s_, X_PLAYS + 3.4, KNOB_Y + dy + 0.8, 2.3, FONT_BOLD, SOFT, anchor="start"))
add(text("0", X_STRUM - 5.4, KNOB_Y + 7.4, 2.4, FONT_MED, SOFT))
add(text("100", X_STRUM + 5.4, KNOB_Y + 7.4, 2.4, FONT_MED, SOFT))
add(text("4", X_WIDTH - 5.6, KNOB_Y + 7.4, 2.4, FONT_MED, SOFT))
add(text("10", X_WIDTH + 5.6, KNOB_Y + 7.4, 2.4, FONT_MED, SOFT))
add(text("C", X_ROOT - 5.4, KNOB_Y + 7.4, 2.4, FONT_MED, SOFT))
add(text("B", X_ROOT + 5.4, KNOB_Y + 7.4, 2.4, FONT_MED, SOFT))
add(text("0", X_OCT - 5.4, KNOB_Y + 7.4, 2.4, FONT_MED, SOFT))
add(text("7", X_OCT + 5.4, KNOB_Y + 7.4, 2.4, FONT_MED, SOFT))

add(f'<line x1="3.0" y1="{JACK_Y - 14.0}" x2="{W - 3.0}" y2="{JACK_Y - 14.0}" stroke="{LINE}" stroke-width="0.35"/>')
JL = JACK_Y - 6.4
for x, s in [(X_CHORD, "CHORD"), (X_SHOW, "SHOW")]:
    add(text(s, x, JL, JACK))
add(text("LATCH", X_LATCH, JL, CONTROL))
add(f'<rect x="{X_SLOT - 7.0}" y="{JACK_Y - 13.0}" width="{X_PLAY - X_SLOT + 14.0}" height="19.0" rx="1.2" fill="none" stroke="{ROOT_COL}" stroke-width="0.35"/>')
add(text("CHORD MEMORY", (X_SLOT + X_PLAY) / 2, JACK_Y - 10.2, 2.4, FONT_MED, ROOT_COL))
for x, s in [(X_SLOT, "SLOT"), (X_PLAY, "PLAY")]:
    add(text(s, x, JL, JACK))
add(text("GRID", X_GRIDLIGHT, JL, 2.6, FONT_BOLD, SOFT))
bx = X_PVOCT - 8.0
add(f'<rect x="{bx}" y="{JACK_Y - 13.0}" width="{W - 3.0 - bx}" height="19.0" rx="1.2" fill="{OUT_BG}"/>')
add(text("POLY", (X_PVOCT + X_PGATE) / 2, JACK_Y - 10.2, 2.4, FONT_MED, SOFT))
add(text("MONO", (X_MVOCT + X_MGATE) / 2, JACK_Y - 10.2, 2.4, FONT_MED, SOFT))
for x, s in [(X_PVOCT, "V/OCT"), (X_PGATE, "GATE"), (X_MVOCT, "V/OCT"), (X_MGATE, "GATE"), (X_TRIG, "TRIG")]:
    add(text(s, x, JL, JACK))

svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}mm" height="{H}mm" viewBox="0 0 {W} {H}">\n'
       + "\n".join(parts) + "\n</svg>\n")
open(os.path.join(HERE, "GridKeys.svg"), "w").write(svg)
print("ok")

import sys as _sys
_sys.path.insert(0, HERE)
import haer_logo
haer_logo.add_to_file(os.path.join(HERE, "GridKeys.svg"), x=W / 2, y=126.0, cap=3.0, bg=BG)
