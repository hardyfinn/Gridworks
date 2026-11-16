#!/usr/bin/env python3
# Mock-up picture of the panel with jacks, light and a lit grid drawn in,
# so the layout can be checked without Rack.
#   python3 tools/preview.py out.png
import os, sys, subprocess
HERE = os.path.dirname(os.path.abspath(__file__))
svg = open(os.path.join(HERE, "..", "res", "GridDrums.svg")).read()
L = {}
src = open(os.path.join(HERE, "..", "res", "make_panel.py")).read()
exec(src.split("# ---- LAYOUT")[1].split("CONTROL =")[0].split("\n", 1)[1], L)
src_rowy = "def row_y" + src.split("def row_y")[1].split("fonts = {}")[0]
exec(src_rowy, L)
TC = ["#F2A93B", "#E8684A", "#E9E1CF", "#6CC3B5", "#8FA8E8", "#C98BD9"]
g = []
def jack(x, y):
    g.append(f'<circle cx="{x}" cy="{y}" r="4.1" fill="#C9CCD1"/><circle cx="{x}" cy="{y}" r="3.1" fill="#7C8088"/><circle cx="{x}" cy="{y}" r="1.9" fill="#0B0B0C"/>')
pat = [0x1111, 0x1010, 0x5555, 0x4444, 0x0820, 0x0000]
step = 6
for r in range(8):
    for k in range(16):
        cx = L["GX0"] + k * L["CELL"] + L["CELL"] / 2; cy = L["row_y"](r); h = L["CELL"] / 2 - 0.45
        lv = 0
        col = TC[r] if r < 6 else "#ECE6D8"
        if r < 6:
            on = (pat[r] >> k) & 1
            lv = 10 if on else 0
            if k == step: lv = 15 if on else 3
        elif r == 6:
            lv = {0: 8, 1: 8, 2: 8, 3: 8, 4: 2, 5: 8, 12: 4, 13: 4, 14: 4, 15: 4}.get(k, 0)
        else:
            lv = 15 if k == 0 else (15 if k == 3 else (6 if k in (1, 2) else 2))
        base = "#2C2C31" if k % 4 == 0 else "#222226"
        g.append(f'<rect x="{cx-h:.2f}" y="{cy-h:.2f}" width="{2*h:.2f}" height="{2*h:.2f}" rx="0.7" fill="{base}"/>')
        if lv:
            g.append(f'<rect x="{cx-h:.2f}" y="{cy-h:.2f}" width="{2*h:.2f}" height="{2*h:.2f}" rx="0.7" fill="{col}" fill-opacity="{0.12+0.88*lv/15:.2f}"/>')
g.append(f'<text x="{L["GX0"]-14}" y="{L["STATUS_Y"]+1.2}" font-family="monospace" font-size="3.6" fill="#ECE6D8">SLOT 1  &gt; 4 AT BAR</text>')
g.append(f'<text x="{L["GX0"]+16*L["CELL"]}" y="{L["STATUS_Y"]+1.2}" font-family="monospace" font-size="3.6" fill="#9A9488" text-anchor="end">GRID m1000123</text>')
for x in (L["X_CLOCK"], L["X_RESET"], L["X_SLOT"]): jack(x, L["IN_Y"])
for x in L["X_OUT"] + [L["X_BAR"]]: jack(x, L["OUT_Y"])
g.append(f'<circle cx="{L["X_GRIDLIGHT"]}" cy="{L["IN_Y"]}" r="1.5" fill="#3BE86A"/>')
W = 121.92
for x in (7.6, W - 15.2):
    for y in (2.5, 126.0): g.append(f'<circle cx="{x}" cy="{y}" r="1.6" fill="#8A8D93"/>')
svg = svg.replace("</svg>", "\n".join(g) + "\n</svg>")
out = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "preview.png")
tmp = out + ".svg"; open(tmp, "w").write(svg)
pw, ph = 610, 643
open(tmp + ".html", "w").write(f'<html><body style="margin:0;background:#111"><img src="{os.path.basename(tmp)}" style="width:{pw}px;height:{ph}px"></body></html>')
js = f"""const {{chromium}} = require('playwright');
(async () => {{ const b = await chromium.launch(); const p = await b.newPage({{viewport: {{width: {pw}, height: {ph}}}, deviceScaleFactor: 2}});
await p.goto('file://{tmp}.html'); await p.waitForTimeout(300);
await p.screenshot({{path: '{out}'}}); await b.close(); }})();"""
open(out + ".js", "w").write(js)
subprocess.run(["node", out + ".js"], check=True, cwd="/tmp")
os.remove(out + ".js"); os.remove(tmp); os.remove(tmp + ".html")
print("wrote", out)
