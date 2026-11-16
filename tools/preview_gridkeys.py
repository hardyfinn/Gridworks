#!/usr/bin/env python3
# Mock-up picture of the GridKeys panel: C major, In Key, width 8, rows a 4th apart,
# PLAYS on TRIAD, one key held on D (D F A) (every copy of D, F and A lit).
import os, sys, math, subprocess
HERE = os.path.dirname(os.path.abspath(__file__))
W = 162.56
svg = open(os.path.join(HERE, "..", "res", "GridKeys.svg")).read()
L = {}
src = open(os.path.join(HERE, "..", "res", "make_panel_gridkeys.py")).read()
exec(src.split("# ---- LAYOUT")[1].split("CONTROL =")[0].split("\n", 1)[1], L)
g = []
def jack(x, y):
    g.append(f'<circle cx="{x}" cy="{y}" r="4.1" fill="#C9CCD1"/><circle cx="{x}" cy="{y}" r="3.1" fill="#7C8088"/><circle cx="{x}" cy="{y}" r="1.9" fill="#0B0B0C"/>')
def knob(x, y, a, r=5.0):
    g.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="#1E1F22" stroke="#3A3C42" stroke-width="0.5"/>')
    ang = math.radians(-135 + 270 * a)
    g.append(f'<line x1="{x}" y1="{y}" x2="{x + (r-0.8)*math.sin(ang):.2f}" y2="{y - (r-0.8)*math.cos(ang):.2f}" stroke="#EEE" stroke-width="0.7" stroke-linecap="round"/>')
NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
MAJOR = [0, 2, 4, 5, 7, 9, 11]
held = {2, 5, 9}
C, GX, GY = L["CELL"], L["GX0"], L["GY0"]
for rb in range(8):          # row from bottom
    r = 7 - rb
    for k in range(16):
        cx = GX + k*C + C/2; cy = GY + r*C + C/2; h = C/2 - 0.45
        g.append(f'<rect x="{cx-h:.2f}" y="{cy-h:.2f}" width="{2*h:.2f}" height="{2*h:.2f}" rx="0.7" fill="#221F25"/>')
        if k < 8:
            d = k + rb * 3
            semi = 12 * (d // 7) + MAJOR[d % 7]
            pc = semi % 12
            if pc in held: col, op = "#FFFFFF", 0.95
            elif pc == 0: col, op = "#E6B450", 0.7
            else: col, op = "#B9A9C9", 0.28
            g.append(f'<rect x="{cx-h:.2f}" y="{cy-h:.2f}" width="{2*h:.2f}" height="{2*h:.2f}" rx="0.7" fill="{col}" fill-opacity="{op}"/>')
            tc = "#121014" if pc in held else "#EEE8EF"
            g.append(f'<text x="{cx:.2f}" y="{cy+0.8:.2f}" font-family="Inter, sans-serif" font-weight="700" font-size="2.2" text-anchor="middle" fill="{tc}">{NAMES[pc]}{"" if pc else str(3 + semi // 12)}</text>')
        if k == 15 and rb in (7, 6, 5, 4, 3, 2, 0):
            lv = {7: 0.3, 6: 0.3, 5: 0.3, 4: 0.3, 3: 0.3, 2: 0.6, 0: 0.3}[rb]
            g.append(f'<rect x="{cx-h:.2f}" y="{cy-h:.2f}" width="{2*h:.2f}" height="{2*h:.2f}" rx="0.7" fill="#EEE8EF" fill-opacity="{lv}"/>')
g.append(f'<text x="6" y="{L["STATUS_Y"]+1.2}" font-family="monospace" font-size="3.3" fill="#EEE8EF">C MAJOR  IN KEY  4TH  TRIAD  W8  C3</text>')
g.append(f'<text x="{W-6}" y="{L["STATUS_Y"]+1.2}" font-family="monospace" font-size="3.3" fill="#9A919E" text-anchor="end">GRID m1000123</text>')
ky = L["KNOB_Y"]
for x, a in [(L["X_ROOT"], 0), (L["X_SCALE"], 0), (L["X_OCT"], 3/7), (L["X_WIDTH"], 4/12), (L["X_ROW"], 0), (L["X_GLIDE"], 0.0), (L["X_STRUM"], 0.2)]:
    knob(x, ky, a)
x = L["X_MODE"]
g.append(f'<rect x="{x-2.4}" y="{ky-3.5}" width="4.8" height="7.0" rx="0.5" fill="#202020"/><rect x="{x-1.9}" y="{ky}" width="3.8" height="3.1" fill="#BBBBBB"/>')
x = L["X_PLAYS"]
g.append(f'<rect x="{x-2.3}" y="{ky-4.8}" width="4.6" height="9.6" rx="0.5" fill="#202020"/><rect x="{x-1.85}" y="{ky-1.5}" width="3.7" height="3.0" fill="#BBBBBB"/>')
jy = L["JACK_Y"]
x = L["X_LATCH"]
g.append(f'<circle cx="{x}" cy="{jy}" r="3.6" fill="#2B2D33"/><circle cx="{x}" cy="{jy}" r="2.6" fill="#E6B450" fill-opacity="0.25"/>')
g.append(f'<circle cx="{L["X_GRIDLIGHT"]}" cy="{jy}" r="1.5" fill="#3BE86A"/>')
for x in (L["X_CHORD"], L["X_SHOW"], L["X_PVOCT"], L["X_PGATE"], L["X_MVOCT"], L["X_MGATE"], L["X_TRIG"]):
    jack(x, L["JACK_Y"])
for x in (7.6, W - 15.2):
    for y in (2.5, 126.0): g.append(f'<circle cx="{x}" cy="{y}" r="1.6" fill="#8A8D93"/>')
svg = svg.replace("</svg>", "\n".join(g) + "\n</svg>")
out = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "preview.png")
tmp = out + ".svg"; open(tmp, "w").write(svg)
pw, ph = 813, 643
open(tmp + ".html", "w").write(f'<html><body style="margin:0;background:#111"><img src="{os.path.basename(tmp)}" style="width:{pw}px;height:{ph}px"></body></html>')
js = f"""const {{chromium}} = require('playwright');
(async () => {{ const b = await chromium.launch(); const p = await b.newPage({{viewport: {{width: {pw}, height: {ph}}}, deviceScaleFactor: 2}});
await p.goto('file://{tmp}.html'); await p.waitForTimeout(400);
await p.screenshot({{path: '{out}'}}); await b.close(); }})();"""
open(out + ".js", "w").write(js)
subprocess.run(["node", out + ".js"], check=True, cwd="/tmp")
os.remove(out + ".js"); os.remove(tmp); os.remove(tmp + ".html")
print("wrote", out)
