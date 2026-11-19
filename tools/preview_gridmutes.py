#!/usr/bin/env python3
# Mock-up picture of the GridMutes panel: a few channels muted, signal on most,
# the grid picture showing layout A with the activity mirror.
import os, sys, math, random, subprocess
HERE = os.path.dirname(os.path.abspath(__file__))
svg = open(os.path.join(HERE, "..", "res", "GridMutes.svg")).read()
L = {}
src = open(os.path.join(HERE, "..", "res", "make_panel_gridmutes.py")).read()
exec(src.split("# ---- LAYOUT")[1].split("CONTROL =")[0].split("\n", 1)[1], L)
W = 223.52
g = []
def jack(x, y):
    g.append(f'<circle cx="{x}" cy="{y}" r="4.1" fill="#C9CCD1"/><circle cx="{x}" cy="{y}" r="3.1" fill="#7C8088"/><circle cx="{x}" cy="{y}" r="1.9" fill="#0B0B0C"/>')
def knob(x, y, a, r=5.0):
    g.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="#1E1F22" stroke="#3A3C42" stroke-width="0.5"/>')
    ang = math.radians(-135 + 270 * a)
    g.append(f'<line x1="{x}" y1="{y}" x2="{x + (r-0.8)*math.sin(ang):.2f}" y2="{y - (r-0.8)*math.cos(ang):.2f}" stroke="#EEE" stroke-width="0.7" stroke-linecap="round"/>')
muted = {3, 8, 15, 20, 22}
random.seed(4)
level = {}
for ch in range(1, 25):
    level[ch] = random.choice([0, 0.2, 0.45, 0.7, 1.0, 1.0])
# channel jacks + mute lights
for ch in range(24):
    x0 = L["CH_X0"] + (ch // 6) * L["CH_COLW"]; y = L["CH_Y0"] + (ch % 6) * L["CH_DY"]
    jack(x0 + L["CH_IN"], y); jack(x0 + L["CH_OUT"], y)
    on = (ch + 1) not in muted
    g.append(f'<circle cx="{x0 + L["CH_NUM"]}" cy="{y + 3.2}" r="1.0" fill="{"#E6B450" if on else "#3A3330"}"/>')
# grid picture
C, GX, GY = L["CELL"], L["GX0"], L["GY0"]
MOD = {(5, 0): "CUE", (5, 1): "LOCK", (5, 2): "SOLO", (5, 3): "A/B", (5, 4): "PAIR", (5, 5): "INV",
       (6, 0): "ALL+", (6, 1): "ALL-", (6, 2): "CLR", (6, 3): "SAVE", (6, 4): "DEL", (6, 5): "COPY"}
for r in range(8):
    for k in range(16):
        cx = GX + k*C + C/2; cy = GY + r*C + C/2; h = C/2 - 0.45
        g.append(f'<rect x="{cx-h:.2f}" y="{cy-h:.2f}" width="{2*h:.2f}" height="{2*h:.2f}" rx="0.7" fill="#221F25"/>')
        col, op, lab, tc = None, 0, "", "#141215"
        if k < 4 and r < 6:
            ch = k * 6 + r + 1; lab = str(ch)
            if ch in muted: col, op, tc = "#E6B450", 0.25, "#EEE8EF"
            else: col, op = "#E6B450", 0.9
        elif 7 <= k <= 10 and r < 6:
            ch = (k - 7) * 6 + r + 1
            lv = level[ch]
            if ch in muted: col, op = "#9A919E", 0.1 + 0.4 * lv
            else: col, op = "#6FE08A", 0.06 + 0.9 * lv
        elif (k, r) in MOD:
            lab = MOD[(k, r)]; col = "#B9A9C9" if k == 5 else "#8FB3E8"; op = 0.35; tc = "#EEE8EF"
        elif k >= 12 and r < 4:
            n = r * 4 + (k - 12) + 1; lab = str(n)
            col = "#E08A6B"; op = 0.95 if n == 2 else (0.45 if n <= 6 else 0.1); tc = "#141215" if n == 2 else "#EEE8EF"
        elif k >= 12 and r == 5:
            lab = ["NOW", "BEAT", "BAR", "2BAR"][k - 12]; col = "#7FD1C1"; op = 0.9 if k == 13 else 0.3
            tc = "#141215" if k == 13 else "#EEE8EF"
        if col:
            g.append(f'<rect x="{cx-h:.2f}" y="{cy-h:.2f}" width="{2*h:.2f}" height="{2*h:.2f}" rx="0.7" fill="{col}" fill-opacity="{op}"/>')
        if lab:
            fs = 1.7 if len(lab) > 3 else 2.2
            g.append(f'<text x="{cx:.2f}" y="{cy+0.75:.2f}" font-family="Inter, sans-serif" font-weight="700" font-size="{fs}" text-anchor="middle" fill="{tc}">{lab}</text>')
g.append(f'<text x="{GX - 2}" y="{L["STATUS_Y"]+1.2}" font-family="monospace" font-size="3.0" fill="#EEE8EF">SCENE 2  NEXT BEAT  19 ON  5 MUTED</text>')
g.append(f'<text x="{W-6}" y="{L["STATUS_Y"]+1.2}" font-family="monospace" font-size="3.0" fill="#9A919E" text-anchor="end">GRID m1000123</text>')
jy = L["JACK_Y"]
for x in (L["X_CLOCK"], L["X_RESET"], L["X_SCENE"], L["X_TRIG"], L["X_MIX"]):
    jack(x, jy)
knob(L["X_FADE"], jy, 0.2)
g.append(f'<circle cx="{L["X_GRIDLIGHT"]}" cy="{jy}" r="1.5" fill="#3BE86A"/>')
for x in (7.6, W - 15.2):
    for y in (2.5, 126.0): g.append(f'<circle cx="{x}" cy="{y}" r="1.6" fill="#8A8D93"/>')
svg = svg.replace("</svg>", "\n".join(g) + "\n</svg>")
out = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "preview.png")
tmp = out + ".svg"; open(tmp, "w").write(svg)
pw, ph = 1118, 643
open(tmp + ".html", "w").write(f'<html><body style="margin:0;background:#111"><img src="{os.path.basename(tmp)}" style="width:{pw}px;height:{ph}px"></body></html>')
js = f"""const {{chromium}} = require('playwright');
(async () => {{ const b = await chromium.launch(); const p = await b.newPage({{viewport: {{width: {pw}, height: {ph}}}, deviceScaleFactor: 2}});
await p.goto('file://{tmp}.html'); await p.waitForTimeout(400);
await p.screenshot({{path: '{out}'}}); await b.close(); }})();"""
open(out + ".js", "w").write(js)
subprocess.run(["node", out + ".js"], check=True, cwd="/tmp")
os.remove(out + ".js"); os.remove(tmp); os.remove(tmp + ".html")
print("wrote", out)
