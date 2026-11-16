#!/usr/bin/env python3
# Draws res/GridDrums.svg. VCV Rack can't draw SVG <text>, so every label is
# turned into outline paths with fontTools. Positions are in mm and must match
# src/GridDrums.cpp (the numbers in the LAYOUT block are the same in both).
#   pip install fonttools
#   python3 res/make_panel.py
import os
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

HERE = os.path.dirname(os.path.abspath(__file__))
FONT_BOLD = os.environ.get("PANEL_FONT_BOLD", "/usr/share/fonts/opentype/inter/Inter-Bold.otf")
FONT_MED = os.environ.get("PANEL_FONT_MED", "/usr/share/fonts/opentype/inter/Inter-Medium.otf")

W, H = 121.92, 128.5  # 24 HP

BG = "#2A2A2D"
SCREEN = "#121214"
INK = "#ECE6D8"
SOFT = "#9A9488"
LINE = "#46464B"
OUT_BG = "#1C1C1F"
TRACK_COL = ["#F2A93B", "#E8684A", "#E9E1CF", "#6CC3B5", "#8FA8E8", "#C98BD9"]

# ---- LAYOUT (same numbers in src/GridDrums.cpp) ----
CELL = 5.8
GX0, GY0 = 20.0, 17.0
GAP_MUTE, GAP_SLOT = 5.0, 2.0
STATUS_Y = 75.5
IN_Y = 95.0
X_CLOCK, X_RESET, X_SLOT, X_GRIDLIGHT = 14.0, 30.0, 46.0, 104.0
OUT_Y = 114.0
X_OUT = [12.0, 26.0, 40.0, 54.0, 68.0, 82.0]
X_BAR = 106.0

CONTROL = 3.2   # control label size (project standard)
JACK = 2.9      # jack label size (project standard)


def row_y(r):
    y = GY0 + r * CELL + CELL / 2
    if r >= 6:
        y += GAP_MUTE
    if r >= 7:
        y += GAP_SLOT
    return y


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

# title
add(text("GridDrums", 6.0, 9.0, 5.2, FONT_BOLD, INK, anchor="start", spacing=0.03))
add(text("GRIDWORKS", W - 6.0, 8.6, 2.6, FONT_MED, SOFT, anchor="end", spacing=0.12))

# screen: the grid replica sits on a dark panel
add(f'<rect x="3.0" y="{GY0 - 6.0}" width="{W - 6.0}" height="{STATUS_Y + 3.5 - (GY0 - 6.0)}" rx="1.6" fill="{SCREEN}"/>')
# step numbers over the columns (1, 5, 9, 13 brighter = beats)
for c in range(16):
    x = GX0 + c * CELL + CELL / 2
    add(text(str(c + 1), x, GY0 - 1.4, 2.2, FONT_BOLD if c % 4 == 0 else FONT_MED, INK if c % 4 == 0 else SOFT))
# track numbers, in the track colours
for t in range(6):
    add(text(f"TR {t + 1}", GX0 - 2.0, row_y(t) + 1.15, CONTROL, FONT_BOLD, TRACK_COL[t], anchor="end"))
add(text("MUTE", GX0 - 2.0, row_y(6) + 1.1, JACK, FONT_BOLD, INK, anchor="end"))
add(text("SLOT", GX0 - 2.0, row_y(7) + 1.1, JACK, FONT_BOLD, INK, anchor="end"))
# CLEAR / COPY labels in the gap above the mute row
gy = row_y(6) - CELL / 2 - 0.9
add(text("CLEAR", GX0 + 13 * CELL, gy, JACK, FONT_BOLD, INK))
add(text("COPY", GX0 + 15 * CELL, gy, JACK, FONT_BOLD, INK))
add(text("1-6", GX0 + 3 * CELL, gy, JACK, FONT_BOLD, SOFT))

# inputs
add(f'<line x1="3.0" y1="{IN_Y - 11.5}" x2="{W - 3.0}" y2="{IN_Y - 11.5}" stroke="{LINE}" stroke-width="0.35"/>')
LY = IN_Y - 6.4
for x, s in [(X_CLOCK, "CLOCK"), (X_RESET, "RESET"), (X_SLOT, "SLOT")]:
    add(text(s, x, LY, JACK))
add(text("1 semitone = 1 slot", X_SLOT + 6.5, IN_Y + 1.0, 2.4, FONT_MED, SOFT, anchor="start"))
add(text("GRID", X_GRIDLIGHT, LY, JACK))

# outputs on a darker band
add(f'<rect x="3.0" y="{OUT_Y - 13.0}" width="{W - 6.0}" height="19.0" rx="1.2" fill="{OUT_BG}"/>')
OLY = OUT_Y - 6.4
add(text("TRIGGER OUTS", (X_OUT[0] + X_OUT[-1]) / 2, OUT_Y - 10.0, 2.4, FONT_MED, SOFT))
for t, x in enumerate(X_OUT):
    add(text(f"TR {t + 1}", x, OLY, JACK, FONT_BOLD, TRACK_COL[t]))
add(f'<line x1="{(X_OUT[-1] + X_BAR) / 2 + 2}" y1="{OUT_Y - 11.5}" x2="{(X_OUT[-1] + X_BAR) / 2 + 2}" y2="{OUT_Y + 4.5}" stroke="{LINE}" stroke-width="0.35"/>')
add(text("BAR", X_BAR, OLY, JACK))

svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}mm" height="{H}mm" viewBox="0 0 {W} {H}">\n'
       + "\n".join(parts) + "\n</svg>\n")
open(os.path.join(HERE, "GridDrums.svg"), "w").write(svg)
print("ok")

# --- Haer maker logo (res/haer_logo.py) on the bottom rail
import sys as _sys
_sys.path.insert(0, HERE)
import haer_logo
haer_logo.add_to_file(os.path.join(HERE, "GridDrums.svg"), x=W / 2, y=126.0, cap=3.0, bg=BG)
