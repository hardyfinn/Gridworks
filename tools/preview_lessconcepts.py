#!/usr/bin/env python3
# Mock-up picture of the LessConcepts panel with knobs, jacks and a lit grid.
#   python3 tools/preview_lessconcepts.py out.png
import os, sys, math, subprocess
HERE = os.path.dirname(os.path.abspath(__file__))
svg = open(os.path.join(HERE, "..", "res", "LessConcepts.svg")).read()
L = {}
src = open(os.path.join(HERE, "..", "res", "make_panel_lessconcepts.py")).read()
exec(src.split("# ---- LAYOUT")[1].split("CONTROL =")[0].split("\n", 1)[1], L)
g = []
def jack(x, y):
    g.append(f'<circle cx="{x}" cy="{y}" r="4.1" fill="#C9CCD1"/><circle cx="{x}" cy="{y}" r="3.1" fill="#7C8088"/><circle cx="{x}" cy="{y}" r="1.9" fill="#0B0B0C"/>')
def knob(x, y, a, r=5.0):
    g.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="#1E1F22" stroke="#3A3C42" stroke-width="0.5"/>')
    ang = math.radians(-135 + 270 * a)
    g.append(f'<line x1="{x}" y1="{y}" x2="{x + (r-0.8)*math.sin(ang):.2f}" y2="{y - (r-0.8)*math.cos(ang):.2f}" stroke="#EEE" stroke-width="0.7" stroke-linecap="round"/>')
# grid LEDs: a plausible state (seed 0b10110010, v1 bit 1, v2 bit 8, octaves 0 / +1, low 1, high 14)
V = ["#7FD1C1", "#F0A868"]
lv = [[0]*16 for _ in range(8)]
seed = 0b10110010
for i in range(1, 9):
    if (seed >> (i-1)) & 1: lv[0][8-i] = lv[1][8-i] = 2
lv[0][7] = 4; lv[1][0] = 15
lv[0][12] = 15; lv[1][13] = 15
for x in (1, 2, 4, 5, 7, 8, 10, 11, 13, 14, 16): lv[2][x-1] = 4
lv[3][6] = 3; lv[3][0] = 6; lv[3][13] = 6
lv[5][0] = lv[5][1] = 6; lv[5][8] = 4; lv[5][9] = 4; lv[5][10] = 8; lv[5][13] = 2; lv[5][14] = 4; lv[5][15] = 6
for y in (6, 7):
    for x in range(8, 16): lv[y][x] = 2
lv[6][8] = 15; lv[6][9] = 6; lv[6][10] = 6
lv[7][0] = 6; lv[7][1] = 8; lv[7][2] = 12
C, GX, GY = L["CELL"], L["GX0"], L["GY0"]
for r in range(8):
    for k in range(16):
        cx = GX + k*C + C/2; cy = GY + r*C + C/2; h = C/2 - 0.45
        base = "#262A30" if (k % 8 == 0 or r >= 3) else "#202328"
        g.append(f'<rect x="{cx-h:.2f}" y="{cy-h:.2f}" width="{2*h:.2f}" height="{2*h:.2f}" rx="0.7" fill="{base}"/>')
        if lv[r][k]:
            col = V[r] if (r < 2 and k < 9) else "#E8ECEF"
            g.append(f'<rect x="{cx-h:.2f}" y="{cy-h:.2f}" width="{2*h:.2f}" height="{2*h:.2f}" rx="0.7" fill="{col}" fill-opacity="{0.1+0.9*lv[r][k]/15:.2f}"/>')
g.append(f'<text x="6" y="{L["STATUS_Y"]+1.2}" font-family="monospace" font-size="3.3" fill="#E8ECEF">SEED  36  RULE  30  1/16  Major  SNAP 1/3 -</text>')
g.append(f'<text x="{142.24-6}" y="{L["STATUS_Y"]+1.2}" font-family="monospace" font-size="3.3" fill="#8E979F" text-anchor="end">GRID m1000123</text>')
ky = L["KNOB_Y"]
for x, a in [(L["X_SEED"], 36/255), (L["X_RULE"], 30/255), (L["X_SCALE"], 0), (L["X_TRANSP"], 0.5), (L["X_PROB1"], 1), (L["X_PROB2"], 0.8)]:
    knob(x, ky, a)
g.append(f'<circle cx="{L["X_GRIDLIGHT"]}" cy="{ky}" r="1.5" fill="#3BE86A"/>')
for x in (L["X_CLOCK"], L["X_RESET"], L["X_SEEDCV"], L["X_RULECV"], L["X_VOCT1"], L["X_GATE1"], L["X_VOCT2"], L["X_GATE2"], L["X_SEEDOUT"], L["X_STEP"]):
    jack(x, L["JACK_Y"])
W = 142.24
for x in (7.6, W - 15.2):
    for y in (2.5, 126.0): g.append(f'<circle cx="{x}" cy="{y}" r="1.6" fill="#8A8D93"/>')
svg = svg.replace("</svg>", "\n".join(g) + "\n</svg>")
out = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "preview.png")
tmp = out + ".svg"; open(tmp, "w").write(svg)
pw, ph = 711, 643
open(tmp + ".html", "w").write(f'<html><body style="margin:0;background:#111"><img src="{os.path.basename(tmp)}" style="width:{pw}px;height:{ph}px"></body></html>')
js = f"""const {{chromium}} = require('playwright');
(async () => {{ const b = await chromium.launch(); const p = await b.newPage({{viewport: {{width: {pw}, height: {ph}}}, deviceScaleFactor: 2}});
await p.goto('file://{tmp}.html'); await p.waitForTimeout(300);
await p.screenshot({{path: '{out}'}}); await b.close(); }})();"""
open(out + ".js", "w").write(js)
subprocess.run(["node", out + ".js"], check=True, cwd="/tmp")
os.remove(out + ".js"); os.remove(tmp); os.remove(tmp + ".html")
print("wrote", out)
