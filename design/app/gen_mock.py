# Generates the Fieldscape app mock canvas: one .dc.html per screen plus canvas.json.
import json, math, os, datetime

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "build", "app-mock")   # published to https://claude.ai/artifact/H4SXurQP2fMVy48LLuuNc5
P = os.path.join(ROOT, "project")
os.makedirs(P, exist_ok=True)

W, H = 390, 844
C = dict(sunk="#0d1310", ground="#19211d", panel="#222b27", raised="#2e3833", hairline="#36403b",
         ink="#e3e7e4", dim="#b3b9b4", faint="#9ca49d", accent="#bbceb5", lamp="#bae6b1")

FONTS = '<link rel="preconnect" href="https://fonts.googleapis.com"><link href="https://fonts.googleapis.com/css2?family=Cormorant+Garamond:wght@500;600&family=Courier+Prime&family=Newsreader:opsz,wght@6..72,400;6..72,500&display=swap" rel="stylesheet">'

STYLE = f"""
body{{margin:0;background:{C['sunk']};font-family:'Newsreader',Georgia,serif;color:{C['ink']}}}
a{{color:{C['accent']}}}a:hover{{color:{C['lamp']}}}
button{{font:inherit;color:inherit}}
input,textarea{{font:inherit;color:inherit}}
"""

def page(title, body, interactive=False):
    return f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>{title}</title>
<script src="./support.js"></script>
</head>
<body>
<x-dc>
<helmet>
{FONTS}
<style>{STYLE}</style>
</helmet>
<div style="width: {W}px; height: {H}px; position: relative; overflow: hidden; background: {C['ground']}; font-family: 'Newsreader', Georgia, serif; color: {C['ink']}">
{body}
</div>
</x-dc>
<script type="text/x-dc" data-dc-script data-props='{{"$preview":{{"width":{W},"height":{H}}}}}'>
class Component extends DCLogic {{
  renderVals() {{ return {{}}; }}
}}
</script>
</body>
</html>
"""

# ---------- pieces ----------
def mono(t, size=12.48, color=None, extra=""):
    return f'<span style="font-family: \'Courier Prime\', monospace; font-size: {size}px; color: {color or C["dim"]}; {extra}">{t}</span>'

def eyebrow(t):
    return f'<div style="font-family: \'Courier Prime\', monospace; font-size: 10.88px; letter-spacing: 0.28em; text-transform: uppercase; color: {C["faint"]}">{t}</div>'

def chip(t):
    return f'<span style="font-family: \'Courier Prime\', monospace; font-size: 10.88px; letter-spacing: 0.18em; text-transform: uppercase; color: {C["faint"]}; border: 1px solid {C["hairline"]}; border-radius: 5px; padding: 5px 6px">{t}</span>'

def btn(t, grow=False, w=None, primary=False, label=None):
    style = f"min-height: 44px; padding: 0 14px; border-radius: 10px; border: 1px solid {C['hairline']}; background: {C['accent'] if primary else C['raised']}; color: {C['sunk'] if primary else C['ink']}; font-size: 15px; font-weight: 500; cursor: pointer"
    if grow: style += "; flex-grow: 1"
    if w: style += f"; width: {w}px"
    al = f' aria-label="{label}"' if label else ""
    return f'<button type="button"{al} style="{style}">{t}</button>'

def seg(items, on=0, grow=True):
    out = []
    for i, t in enumerate(items):
        sel = i == on
        out.append(f'<button type="button" aria-pressed="{str(sel).lower()}" style="min-height: 40px; {"flex-grow: 1;" if grow else ""} padding: 0 12px; border: 0; border-radius: 8px; background: {C["raised"] if sel else "transparent"}; color: {C["ink"] if sel else C["dim"]}; font-size: 14px; cursor: pointer">{t}</button>')
    return f'<div role="group" style="display: flex; gap: 2px; padding: 3px; background: {C["sunk"]}; border: 1px solid {C["hairline"]}; border-radius: 11px">{"".join(out)}</div>'

def icon(name, size=20, color=None):
    col = color or C["ink"]
    paths = {
        "layers": '<path d="M12 3 2 8l10 5 10-5-10-5Z"/><path d="m2 13 10 5 10-5"/>',
        "locate": '<circle cx="12" cy="12" r="4"/><path d="M12 2v3M12 19v3M2 12h3M19 12h3"/>',
        "user": '<circle cx="12" cy="8" r="4"/><path d="M4 21c1.5-4 4.5-6 8-6s6.5 2 8 6"/>',
        "back": '<path d="M15 5 8 12l7 7"/>',
        "close": '<path d="M6 6l12 12M18 6 6 18"/>',
        "search": '<circle cx="11" cy="11" r="6"/><path d="m20 20-4.5-4.5"/>',
        "play": '<path d="M8 5v14l11-7Z"/>',
        "plus": '<path d="M12 5v14M5 12h14"/>',
        "tree": '<path d="M12 3 6 12h3l-3 5h12l-3-5h3Z"/><path d="M12 17v4"/>',
        "animal": '<circle cx="8" cy="9" r="2"/><circle cx="16" cy="9" r="2"/><circle cx="5" cy="14" r="1.6"/><circle cx="19" cy="14" r="1.6"/><path d="M8.5 18c1-2.5 6-2.5 7 0 .6 1.6-7.6 1.6-7 0Z"/>',
        "flower": '<circle cx="12" cy="9" r="2.4"/><path d="M12 6.6c0-3 3-3 3 0M14.4 9c3 0 3 3 0 3M12 11.4c0 3-3 3-3 0M9.6 9c-3 0-3-3 0-3M12 12v9"/>',
        "insect": '<ellipse cx="12" cy="13" rx="3.2" ry="5"/><path d="M12 8V5M9 10 5 8M15 10l4-2M9 14l-4 1M15 14l4 1"/>',
        "fish": '<path d="M3 12c3-4 9-5 13 0-4 5-10 4-13 0Z"/><path d="m16 12 5-4v8Z"/>',
        "soil": '<path d="M3 16h18M5 19h14M7 13c2-3 8-3 10 0"/>',
        "water": '<path d="M12 3c3 5 6 8 6 11a6 6 0 0 1-12 0c0-3 3-6 6-11Z"/>',
        "route": '<path d="M5 19c3-8 11-2 14-12"/><circle cx="5" cy="19" r="2"/><circle cx="19" cy="7" r="2"/>',
        "point": '<circle cx="12" cy="11" r="3"/><path d="M12 21c-5-6-7-9-7-11a7 7 0 0 1 14 0c0 2-2 5-7 11Z"/>',
        "select": '<path d="M5 3l14 8-6 2-2 6Z"/>',
        "mic": '<rect x="9" y="3" width="6" height="11" rx="3"/><path d="M5 11a7 7 0 0 0 14 0M12 18v3"/>',
        "photo": '<rect x="3" y="6" width="18" height="14" rx="2"/><circle cx="12" cy="13" r="3.5"/><path d="M9 6l1.5-2h3L15 6"/>',
        "upload": '<path d="M12 16V4M7 9l5-5 5 5M4 20h16"/>',
    }[name]
    return f'<svg width="{size}" height="{size}" viewBox="0 0 24 24" fill="none" stroke="{col}" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">{paths}</svg>'

def roundbtn(name, label):
    return f'<button type="button" aria-label="{label}" style="width: 44px; height: 44px; border-radius: 22px; border: 1px solid {C["hairline"]}; background: {C["panel"]}; display: flex; align-items: center; justify-content: center; cursor: pointer">{icon(name)}</button>'

def knob(label, value, frac, size=68):
    r = size / 2 - 6
    cx = cy = size / 2
    def pt(a):
        rad = math.radians(a)
        return cx + r * math.cos(rad), cy + r * math.sin(rad)
    a0, a1 = 135, 135 + 270 * frac
    x0, y0 = pt(a0); xe, ye = pt(405); x1, y1 = pt(a1)
    large_all = 1
    large_v = 1 if (a1 - a0) > 180 else 0
    track = f'<path d="M{x0:.1f} {y0:.1f} A{r} {r} 0 {large_all} 1 {xe:.1f} {ye:.1f}" stroke="{C["hairline"]}" stroke-width="4" fill="none" stroke-linecap="round"/>'
    val = f'<path d="M{x0:.1f} {y0:.1f} A{r} {r} 0 {large_v} 1 {x1:.1f} {y1:.1f}" stroke="{C["accent"]}" stroke-width="4" fill="none" stroke-linecap="round"/>' if frac > 0.01 else ""
    px, py = cx + (r - 11) * math.cos(math.radians(a1)), cy + (r - 11) * math.sin(math.radians(a1))
    body = f'<circle cx="{cx}" cy="{cy}" r="{r - 9}" fill="{C["raised"]}" stroke="{C["hairline"]}"/><circle cx="{px:.1f}" cy="{py:.1f}" r="2.6" fill="{C["ink"]}"/>'
    return f'''<div style="display: flex; flex-direction: column; align-items: center; gap: 4px; min-width: 0">
<svg width="{size}" height="{size}" viewBox="0 0 {size} {size}" role="img" aria-label="{label} {value}">{track}{val}{body}</svg>
<div style="font-size: 13px; color: {C["dim"]}; text-align: center; line-height: 1.2">{label}</div>
{mono(value, 12, C["ink"])}
</div>'''

def level_row(name, meta, frac, playing=True):
    return f'''<div style="display: flex; flex-direction: column; gap: 6px; padding: 10px 0; min-height: 44px; justify-content: center">
<div style="display: flex; align-items: center; gap: 8px">
<span style="width: 9px; height: 9px; border-radius: 5px; background: {C["lamp"]}; flex: none"></span>
<span style="font-size: 17px; flex-grow: 1">{name}</span>
{mono(meta)}
</div>
<div style="height: 5px; border-radius: 3px; background: {C["raised"]}"><div style="width: {frac*100:.0f}%; height: 5px; border-radius: 3px; background: {C["accent"] if playing else C["faint"]}"></div></div>
</div>'''

def hr():
    return f'<div style="height: 1px; background: {C["hairline"]}"></div>'

def note(t):
    return f'<p style="margin: 0; font-size: 13px; line-height: 1.45; color: {C["dim"]}">{t}</p>'

# the map: an abstract satellite-dark ground with the park, a route, points and the walker
def mapart(h=H, walker=True, zones=False, sections=False, drawing=False):
    pts = [(206, 316, True), (246, 280, True), (228, 300, True), (178, 356, False), (300, 214, False), (140, 250, True), (262, 402, False)]
    park = "M70 150 L330 120 L352 330 L300 470 L118 450 L58 300 Z"
    streets = "".join(f'<path d="M{-20 + i * 70} 0 L{120 + i * 70} {h}" stroke="{C["raised"]}" stroke-width="1" opacity="0.55"/>' for i in range(8))
    streets += "".join(f'<path d="M0 {60 + i * 90} L{W} {20 + i * 90}" stroke="{C["raised"]}" stroke-width="1" opacity="0.4"/>' for i in range(10))
    route = "M150 430 L186 372 L214 324 L240 290 L276 250 L320 196"
    sec = ""
    if sections:
        sec = f'<path d="M70 150 L200 138 L230 300 L118 450 L58 300 Z" fill="{C["accent"]}" fill-opacity="0.07" stroke="{C["accent"]}" stroke-opacity="0.35" stroke-dasharray="4 4"/>'
    z = ""
    if zones:
        z = "".join(f'<circle cx="{x}" cy="{y}" r="26" fill="none" stroke="{C["dim"]}" stroke-opacity="0.45" stroke-dasharray="3 4"/>' for x, y, _ in pts[:4])
    ptsvg = "".join(f'<circle cx="{x}" cy="{y}" r="9" fill="{C["lamp"] if a else C["sunk"]}" stroke="{C["sunk"] if a else C["ink"]}" stroke-width="2"/>' for x, y, a in pts)
    wk = f'<circle cx="214" cy="324" r="18" fill="{C["lamp"]}" fill-opacity="0.18"/><circle cx="214" cy="324" r="8" fill="{C["lamp"]}" stroke="{C["sunk"]}" stroke-width="3"/>' if walker else ""
    draw = ""
    if drawing:
        draw = f'<path d="M96 210 L150 236 L170 290" stroke="{C["lamp"]}" stroke-width="3" stroke-dasharray="6 5" fill="none"/><circle cx="96" cy="210" r="5" fill="{C["lamp"]}"/><circle cx="150" cy="236" r="5" fill="{C["lamp"]}"/><circle cx="170" cy="290" r="6" fill="{C["ground"]}" stroke="{C["lamp"]}" stroke-width="2"/>'
    return f'''<svg width="{W}" height="{h}" viewBox="0 0 {W} {h}" style="position: absolute; left: 0; top: 0" role="img" aria-label="Map of Koşuyolu Parkı with its route and recordings">
<rect width="{W}" height="{h}" fill="#1b2420"/>
{streets}
<path d="{park}" fill="#23321f" stroke="{C["ink"]}" stroke-opacity="0.8" stroke-width="2"/>
{sec}{z}
<path d="{route}" stroke="{C["sunk"]}" stroke-opacity="0.85" stroke-width="9" fill="none" stroke-linecap="round" stroke-linejoin="round"/>
<path d="{route}" stroke="#ffffff" stroke-width="3" fill="none" stroke-linecap="round" stroke-linejoin="round"/>
{ptsvg}{wk}{draw}
</svg>'''

def topbar(place="Koşuyolu Parkı", setter=False):
    s = f'<div style="margin-left: auto; display: flex; gap: 8px">{roundbtn("layers", "Map layers")}{roundbtn("user", "Account")}</div>'
    tag = f'<span style="margin-left: 6px">{chip("setter")}</span>' if setter else ""
    return f'''<div style="position: absolute; left: 16px; right: 16px; top: 56px; display: flex; align-items: center; gap: 8px">
<button type="button" aria-haspopup="listbox" style="min-height: 44px; padding: 0 16px; border-radius: 22px; border: 1px solid {C["hairline"]}; background: {C["panel"]}; display: flex; align-items: center; gap: 8px; cursor: pointer">
<span style="font-family: 'Cormorant Garamond', serif; font-size: 20px; font-weight: 600">{place}</span><span style="color: {C["faint"]}">▾</span></button>{tag}
{s}
</div>'''

def sheet(top, inner, handle=True):
    hd = f'<div style="width: 38px; height: 4px; border-radius: 2px; background: {C["hairline"]}; align-self: center"></div>' if handle else ""
    return f'''<div style="position: absolute; left: 0; right: 0; bottom: 0; top: {top}px; background: {C["panel"]}; border-radius: 20px 20px 0 0; border-top: 1px solid {C["hairline"]}; padding: 8px 16px 34px; box-sizing: border-box; display: flex; flex-direction: column; gap: 12px; overflow: hidden">
{hd}
{inner}
</div>'''

def title_row(t, right=""):
    return f'<div style="display: flex; align-items: center; gap: 12px"><span style="font-family: \'Cormorant Garamond\', serif; font-size: 24px; font-weight: 600; flex-grow: 1; line-height: 1.1">{t}</span>{right}</div>'

def field(label, value, multiline=False):
    tag = "textarea" if multiline else "input"
    inner = f'<textarea rows="2" style="width: 100%; box-sizing: border-box; min-height: 64px; padding: 10px 12px; border-radius: 10px; border: 1px solid {C["hairline"]}; background: {C["sunk"]}; font-size: 15px; resize: none">{value}</textarea>' if multiline else f'<input type="text" value="{value}" style="width: 100%; box-sizing: border-box; min-height: 44px; padding: 0 12px; border-radius: 10px; border: 1px solid {C["hairline"]}; background: {C["sunk"]}; font-size: 15px">'
    return f'<label style="display: flex; flex-direction: column; gap: 6px">{eyebrow(label)}{inner}</label>'

boards = {}

# 1 Walk ---------------------------------------------------------------
body = mapart() + topbar() + f'<div style="position: absolute; right: 16px; top: 398px">{roundbtn("locate", "Centre on me")}</div>' + sheet(452, f'''
{title_row("Koşuyolu Parkı", btn("Stop", w=76) + chip("±5 m"))}
{note('<span style="color:#e3e7e4">Route</span> Koşuyolu Parkı · chord 7 of 16, E♭maj9 · <span style="color:#e3e7e4">rhythm</span> Grains')}
<div style="display: flex; flex-direction: column">
{level_row("Stretch 2", "24 m", 0.82)}{hr()}{level_row("Stretch", "61 m", 0.38)}{hr()}{level_row("Grains", "78 m", 0.22)}
</div>
''')
boards["Walk.dc.html"] = ("Walk — listening", page("Walk", body))

# 2 Places -------------------------------------------------------------
places = [("Koşuyolu Parkı", "Kadıköy · 1 route · 6 recordings", True), ("Validebağ Korusu", "Üsküdar · 1 route · 3 recordings", False),
          ("Belgrad Ormanı", "Sarıyer · nothing yet", False), ("Emirgan Korusu", "Sarıyer · nothing yet", False), ("Yıldız Parkı", "Beşiktaş · nothing yet", False)]
rows = "".join(f'''<button type="button" role="option" aria-selected="{str(sel).lower()}" style="min-height: 56px; display: flex; flex-direction: column; justify-content: center; align-items: flex-start; gap: 2px; padding: 8px 12px; border: 0; border-radius: 10px; background: {C["raised"] if sel else "transparent"}; text-align: left; cursor: pointer">
<span style="font-size: 17px">{n}</span>{mono(m, 11.5, C["faint"])}</button>''' for n, m, sel in places)
routes = "".join(f'''<button type="button" role="option" style="min-height: 52px; display: flex; align-items: center; gap: 10px; padding: 8px 12px; border: 0; border-radius: 10px; background: transparent; text-align: left; cursor: pointer">{icon("route", 18, C["dim"])}<span style="font-size: 16px; flex-grow: 1">{n}</span>{mono(m, 11.5, C["faint"])}</button>''' for n, m in [("Koşuyolu Parkı", "1.2 km"), ("Validebağ Korusu", "0.9 km")])
body = mapart() + f'<div style="position: absolute; inset: 0; background: {C["sunk"]}; opacity: 0.55"></div>' + sheet(96, f'''
{title_row("Places", roundbtn("close", "Close"))}
<div style="display: flex; align-items: center; gap: 12px; min-height: 44px">
<span style="flex-grow: 1"><span style="font-size: 16px">Open world</span><br>{mono("every route at once, by where you are", 11.5, C["faint"])}</span>
<button type="button" role="switch" aria-checked="true" aria-label="Open world" style="width: 52px; height: 32px; border-radius: 16px; border: 0; background: {C["accent"]}; position: relative; cursor: pointer"><span style="position: absolute; right: 3px; top: 3px; width: 26px; height: 26px; border-radius: 13px; background: {C["sunk"]}"></span></button>
</div>
<label style="display: flex; align-items: center; gap: 8px; min-height: 44px; padding: 0 12px; border-radius: 10px; border: 1px solid {C["hairline"]}; background: {C["sunk"]}">{icon("search", 18, C["faint"])}<input type="search" aria-label="Search places and routes" placeholder="Search forests, korular and routes" style="flex-grow: 1; border: 0; background: transparent; font-size: 15px; outline: none"></label>
{eyebrow("Places")}
<div role="listbox" style="display: flex; flex-direction: column; gap: 2px">{rows}</div>
{eyebrow("Routes")}
<div role="listbox" style="display: flex; flex-direction: column">{routes}</div>
''')
boards["Places.dc.html"] = ("Places and routes", page("Places", body))

# 3 Layers -------------------------------------------------------------
body = mapart() + topbar() + sheet(470, f'''
{title_row("Map", roundbtn("close", "Close"))}
{eyebrow("Base map")}
{seg(["Map", "Topo", "Satellite", "Virtual"], 2)}
{eyebrow("Park boundary")}
<div style="display: flex; gap: 8px">{seg(["Off", "Frame"], 1)}{btn("Fit all", w=96)}</div>
{eyebrow("Show")}
<div style="display: flex; gap: 8px; flex-wrap: wrap">{seg(["Zones"], 0, False)}{seg(["Sections"], -1, False)}{seg(["Morph cells"], -1, False)}</div>
''')
boards["Layers.dc.html"] = ("Map layers", page("Layers", body))

# 4 Point card, listener ----------------------------------------------
wave = "".join(f'<rect x="{i*4}" y="{22 - h}" width="2" height="{2*h}" rx="1" fill="{C["accent"] if i < 30 else C["faint"]}"/>' for i, h in enumerate([abs(math.sin(i*0.7)*9 + math.sin(i*0.23)*6) + 2 for i in range(80)]))
body = mapart() + topbar() + sheet(236, f'''
<div style="display: flex; align-items: center; gap: 8px">{roundbtn("back", "Back to the walk")}{icon("flower", 22, C["lamp"])}{chip("stretch")}<span style="margin-left: auto">{chip("61 m away")}</span></div>
{title_row("Stretch")}
{note("Morning chorus under the plane trees, recorded from the bench by the east gate.")}
<div style="display: flex; gap: 8px">
<div style="width: 112px; height: 72px; border-radius: 10px; background: #2c3a2a; border: 1px solid {C["hairline"]}; display: flex; align-items: center; justify-content: center">{mono("photo 1", 11, C["faint"])}</div>
<div style="width: 112px; height: 72px; border-radius: 10px; background: #26332c; border: 1px solid {C["hairline"]}; display: flex; align-items: center; justify-content: center">{mono("photo 2", 11, C["faint"])}</div>
<div style="width: 112px; height: 72px; border-radius: 10px; background: #30392f; border: 1px solid {C["hairline"]}; display: flex; align-items: center; justify-content: center">{mono("+2", 11, C["faint"])}</div>
</div>
{eyebrow("The recording")}
<div style="display: flex; align-items: center; gap: 10px">
{roundbtn("play", "Play the recording as it was made")}
<svg width="236" height="44" viewBox="0 0 320 44" role="img" aria-label="The recording as it was made, 38 percent played">{wave}</svg>
{mono("2:31 / 6:40", 11.5)}
</div>
{eyebrow("Where and when")}
{note("41.0081 N, 29.0390 E · 2026-09-12, 07:18 · 6 min 40 s · tags: birds, dawn")}
{note("Listen: this point alone, stretched, from wherever you are.")}
<div style="display: flex; gap: 8px; margin-top: auto">{btn(icon("play", 16, C["sunk"]) + " Listen", grow=True, primary=True)}{btn("Zoom to", w=110)}</div>
''')
boards["PointCard.dc.html"] = ("Point card — listener", page("Point card", body))

# 5 Route card, listener ----------------------------------------------
chords = ["Cm9", "Cm11", "Fmaj7♯11", "F6/9", "B♭maj9", "B♭maj7", "E♭maj7♯11", "E♭6/9", "A♭maj9", "A♭13", "D♭maj7", "D♭maj9", "Dm7♭5", "G7alt", "Cm9", "Cm6"]
cells = "".join(f'<div style="flex: 1; height: 28px; border-radius: 4px; background: {C["accent"] if i == 6 else C["raised"]}; border: 1px solid {C["hairline"]}"></div>' for i in range(16))
body = mapart() + topbar() + sheet(360, f'''
<div style="display: flex; align-items: center; gap: 8px">{roundbtn("back", "Back to the walk")}{icon("route", 22, C["ink"])}{chip("route")}<span style="margin-left: auto">{chip("1.2 km")}</span></div>
{title_row("Koşuyolu Parkı")}
{note("Sixteen chords from the south gate to the north lawn; the second voice changes mode with each section of the park.")}
{eyebrow("The progression — you are at 7 of 16")}
<div style="display: flex; gap: 3px">{cells}</div>
<div style="display: flex; justify-content: space-between">{mono("Cm9", 11.5)}{mono("E♭maj7♯11 — now", 11.5, C["ink"])}{mono("Cm6", 11.5)}</div>
<div style="display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 8px">
<div style="padding: 10px; border-radius: 10px; background: {C["sunk"]}; border: 1px solid {C["hairline"]}">{eyebrow("Key")}<div style="font-size: 18px">D · G</div></div>
<div style="padding: 10px; border-radius: 10px; background: {C["sunk"]}; border: 1px solid {C["hairline"]}">{eyebrow("Tempo")}<div style="font-size: 18px">72 bpm</div></div>
<div style="padding: 10px; border-radius: 10px; background: {C["sunk"]}; border: 1px solid {C["hairline"]}">{eyebrow("Voices")}<div style="font-size: 18px">FM · formant · AM</div></div>
</div>
<div style="display: flex; gap: 8px; margin-top: auto">{btn("Show whole route", grow=True)}</div>
''')
boards["RouteCard.dc.html"] = ("Route card — listener", page("Route card", body))

# 6 Account ---------------------------------------------------------------
body = f'''<div style="padding: 56px 16px 34px; box-sizing: border-box; height: {H}px; display: flex; flex-direction: column; gap: 16px">
<div style="display: flex; align-items: center; gap: 8px">{roundbtn("back", "Back to the map")}<span style="font-family: 'Cormorant Garamond', serif; font-size: 28px; font-weight: 600">Account</span></div>
<div style="padding: 16px; border-radius: 14px; background: {C["panel"]}; border: 1px solid {C["hairline"]}; display: flex; flex-direction: column; gap: 12px">
{eyebrow("Setter")}
{note("Setters place points, record, draw routes and shape the sound. Everyone else listens. Sign in with the email your setter account uses; we send a link, no password.")}
{field("Email", "you@example.com")}
{btn("Send sign-in link", primary=True)}
</div>
<div style="padding: 16px; border-radius: 14px; background: {C["panel"]}; border: 1px solid {C["hairline"]}; display: flex; flex-direction: column; gap: 12px">
{eyebrow("Offline")}
<div style="display: flex; align-items: center; gap: 12px"><span style="flex-grow: 1; font-size: 16px">Koşuyolu Parkı</span>{mono("map + 6 recordings · 98 MB", 11.5, C["faint"])}</div>
{btn("Download for offline")}
{note("Recordings you have heard are kept already; this saves the map tiles too, for walks without signal.")}
</div>
<div style="padding: 16px; border-radius: 14px; background: {C["panel"]}; border: 1px solid {C["hairline"]}; display: flex; flex-direction: column; gap: 8px">
{eyebrow("About")}
{note("Fieldscape · test 5 · recordings © their authors · map imagery © Esri, © OpenStreetMap contributors")}
</div>
</div>'''
boards["Account.dc.html"] = ("Account — signed out", page("Account", body))

# 7 Setter: draw ----------------------------------------------------------
types = "".join(f'<button type="button" aria-label="{n}" aria-pressed="{str(i==2).lower()}" style="width: 44px; height: 44px; border-radius: 10px; border: 1px solid {C["hairline"]}; background: {C["raised"] if i==2 else "transparent"}; display: flex; align-items: center; justify-content: center; cursor: pointer">{icon(n, 20, C["ink"] if i==2 else C["dim"])}</button>' for i, n in enumerate(["tree", "animal", "flower", "insect", "fish", "soil", "water"]))
body = mapart(zones=True, drawing=True) + topbar(setter=True) + f'''
<div style="position: absolute; left: 16px; right: 16px; top: 112px; padding: 8px; border-radius: 14px; background: {C["panel"]}; border: 1px solid {C["hairline"]}; display: flex; flex-direction: column; gap: 8px">
{seg([icon("select", 18) + " Select", icon("point", 18) + " Point", icon("route", 18) + " Route"], 2)}
</div>''' + sheet(602, f'''
{title_row("Drawing a route", btn("Done", w=80, primary=True))}
{note("Tap the map to add a corner, tap the last corner again to finish. 3 corners · 162 m.")}
<div style="display: flex; align-items: center; gap: 12px; padding-top: 4px">
{btn(icon("upload", 18) + " Publish 2", w=150)}{mono("2 changes waiting · last saved 12:41", 11.5, C["faint"])}
</div>
''')
boards["SetterDraw.dc.html"] = ("Setter — drawing", page("Setter drawing", body))

# 8 Setter point card ----------------------------------------------------
body = f'''<div style="padding: 56px 16px 34px; box-sizing: border-box; height: {H}px; display: flex; flex-direction: column; gap: 12px">
<div style="display: flex; align-items: center; gap: 8px">{roundbtn("back", "Back")}{chip("point")}<span style="margin-left: auto; display: flex; gap: 8px">{seg(["Published"], 0, False)}{seg(["Sensitive"], -1, False)}</span></div>
{field("Name", "Stretch")}
{field("What is here", "Morning chorus under the plane trees, recorded from the bench by the east gate.", True)}
{eyebrow("Type")}
<div style="display: flex; gap: 4px; flex-wrap: wrap">{types}</div>
{field("Tags", "birds, dawn")}
{eyebrow("Sound device")}
{seg(["Stretch", "Rhythm", "Grains"], 0)}
<div style="display: flex; align-items: center; gap: 10px; padding: 10px 12px; border-radius: 10px; background: {C["sunk"]}; border: 1px solid {C["hairline"]}">{icon("mic", 20, C["lamp"])}<span style="flex-grow: 1; font-size: 15px">take.webm</span>{mono("6:40", 11.5)}{btn("Replace", w=96)}</div>
<div style="display: flex; gap: 8px">{btn("Shape the sound", grow=True, primary=True)}{btn(icon("photo", 18) + " Photos 4", w=132)}</div>
<div style="display: flex; gap: 8px; margin-top: auto">{btn("Mute", w=80)}{btn("Solo", w=80)}<span style="flex-grow: 1"></span>{btn("Delete", w=96)}</div>
</div>'''
boards["SetterPoint.dc.html"] = ("Setter — point", page("Setter point", body))

# 9 Stretch device -------------------------------------------------------
knobs1 = "".join(knob(*k) for k in [("Radius", "140 m", 0.28), ("Level", "0.90", 0.9), ("Zone", "25 m", 0.25)])
knobs2 = "".join(knob(*k) for k in [("Stretch", "×8.0", 0.3), ("Window", "0.34 s", 0.35), ("Width", "1.00", 1.0)])
knobs3 = "".join(knob(*k) for k in [("Onset", "0.20", 0.2), ("Grit", "0.00", 0.0), ("Start", "0 %", 0.0)])
body = f'''<div style="padding: 56px 16px 34px; box-sizing: border-box; height: {H}px; display: flex; flex-direction: column; gap: 14px">
<div style="display: flex; align-items: center; gap: 8px">{roundbtn("back", "Back to the point")}<span style="font-family: 'Cormorant Garamond', serif; font-size: 26px; font-weight: 600; flex-grow: 1">Stretch</span>{btn("Reset", w=76)}</div>
{seg(["Place", "Stretch", "Colour"], 1)}
<div style="display: flex; align-items: center; gap: 10px; padding: 10px 12px; border-radius: 12px; background: {C["sunk"]}; border: 1px solid {C["hairline"]}">
<svg width="258" height="48" viewBox="0 0 320 48" role="img" aria-label="The recording, with the part being stretched marked">{wave.replace('22 -', '24 -')}<rect x="112" y="0" width="4" height="48" fill="{C["lamp"]}"/></svg>
{mono("at 2:31", 11.5)}
</div>
<div style="display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 18px 8px; padding: 8px 0">{knobs2}{knobs3}</div>
<div style="display: flex; align-items: center; gap: 12px; min-height: 44px"><span style="flex-grow: 1; font-size: 16px">Freeze</span><button type="button" role="switch" aria-checked="false" aria-label="Freeze" style="width: 52px; height: 32px; border-radius: 16px; border: 1px solid {C["hairline"]}; background: {C["raised"]}; position: relative; cursor: pointer"><span style="position: absolute; left: 3px; top: 3px; width: 24px; height: 24px; border-radius: 12px; background: {C["dim"]}"></span></button></div>
<div style="display: flex; align-items: center; gap: 12px; min-height: 44px"><span style="flex-grow: 1; font-size: 16px">Window shape</span>{seg(["Hann", "Tukey"], 0, False)}</div>
<div style="margin-top: auto; display: flex; flex-direction: column; gap: 8px">
{note("Drag a knob up or down; hold it still for fine steps. You hear every change as you walk, and it is saved for this point only.")}
{btn("Listen from this point", primary=True)}
</div>
</div>'''
boards["StretchDevice.dc.html"] = ("Device — stretch", page("Stretch device", body))

# 10 Rhythm device -------------------------------------------------------
def ring(pulses, steps, rot, label):
    size = 78; cx = cy = size / 2; r = 30
    dots = []
    acc = 0
    pat = []
    for i in range(steps):
        acc += pulses
        if acc >= steps: acc -= steps; pat.append(True)
        else: pat.append(False)
    for i in range(steps):
        on = pat[(i + rot) % steps]
        a = -math.pi / 2 + 2 * math.pi * i / steps
        x, y = cx + r * math.cos(a), cy + r * math.sin(a)
        dots.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{2.6 if on else 1.3}" fill="{C["lamp"] if on else C["hairline"]}"/>')
    return f'''<div style="display: flex; flex-direction: column; align-items: center; gap: 4px; padding: 8px 4px; border-radius: 12px; background: {C["sunk"]}; border: 1px solid {C["hairline"]}">
<svg width="{size}" height="{size}" viewBox="0 0 {size} {size}" role="img" aria-label="{label}: {pulses} hits in {steps}">{''.join(dots)}</svg>
<div style="font-size: 14px">{label}</div>{mono(f"{pulses} / {steps}", 11)}</div>'''
rings = ring(10, 32, 4, "Low") + ring(12, 32, 0, "Mid") + ring(9, 32, 16, "High") + ring(5, 32, 0, "Random")
fxk = "".join(knob(*k, size=60) for k in [("Crush", "0.03", 0.03), ("Drive", "0.23", 0.23), ("Stretch", "0.10", 0.1), ("Delay", "1/4", 0.5), ("Feedback", "0.14", 0.14), ("Wet", "0.29", 0.29)])
body = f'''<div style="padding: 56px 16px 34px; box-sizing: border-box; height: {H}px; display: flex; flex-direction: column; gap: 14px">
<div style="display: flex; align-items: center; gap: 8px">{roundbtn("back", "Back to the point")}<span style="font-family: 'Cormorant Garamond', serif; font-size: 26px; font-weight: 600; flex-grow: 1">Rhythm</span>{btn("New sentence", w=132)}</div>
{seg(["Pattern", "Hits", "Effects"], 0)}
<div style="display: grid; grid-template-columns: repeat(4, minmax(0, 1fr)); gap: 6px">{rings}</div>
<div style="display: flex; align-items: center; gap: 12px">{eyebrow("Sentence 2 of 4 · bar 3 of 8")}<span style="flex-grow: 1; height: 4px; border-radius: 2px; background: {C["raised"]}"><span style="display: block; width: 35%; height: 4px; border-radius: 2px; background: {C["accent"]}"></span></span></div>
{eyebrow("Low · effects")}
<div style="display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 16px 8px">{fxk}</div>
<div style="display: flex; gap: 8px; margin-top: auto">{seg(["1/16", "1/8", "1/4"], 1)}{btn("Swing 0", w=96)}</div>
</div>'''
boards["RhythmDevice.dc.html"] = ("Device — rhythm", page("Rhythm device", body))

# 11 Patch editor --------------------------------------------------------
chordbtns = "".join(f'<button type="button" style="min-height: 44px; border-radius: 8px; border: 1px solid {C["hairline"]}; background: {C["accent"] if i == 6 else C["raised"]}; color: {C["sunk"] if i == 6 else C["ink"]}; font-family: \'Courier Prime\', monospace; font-size: 12px; cursor: pointer">{c}</button>' for i, c in enumerate(chords))
tk = "".join(knob(*k, size=62) for k in [("Tempo", "72 bpm", 0.32), ("Key", "D", 0.5), ("Second key", "G", 0.7)])
body = f'''<div style="padding: 56px 16px 34px; box-sizing: border-box; height: {H}px; display: flex; flex-direction: column; gap: 14px">
<div style="display: flex; align-items: center; gap: 8px">{roundbtn("back", "Back to the route")}<span style="font-family: 'Cormorant Garamond', serif; font-size: 26px; font-weight: 600; flex-grow: 1">Koşuyolu Parkı</span>{btn("Reset", w=76)}</div>
<div role="tablist" style="display: flex; gap: 2px; padding: 3px; background: {C["sunk"]}; border: 1px solid {C["hairline"]}; border-radius: 11px">
{"".join(f'<button type="button" role="tab" aria-selected="{str(i==0).lower()}" style="min-height: 40px; flex-grow: 1; border: 0; border-radius: 8px; background: {C["raised"] if i==0 else "transparent"}; color: {C["ink"] if i==0 else C["dim"]}; font-size: 13px; cursor: pointer">{t}</button>' for i, t in enumerate(["Chords", "Voices", "Effects", "Morphs", "Points"]))}
</div>
<div style="display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 8px">{tk}</div>
{eyebrow("Progression — along the route, start to end")}
<div style="display: grid; grid-template-columns: repeat(4, minmax(0, 1fr)); gap: 6px">{chordbtns}</div>
{note("Tap a chord to change its root or quality; hold to make it lean into the next one.")}
<div style="display: flex; gap: 8px">{btn("New progression", grow=True)}{btn("Sections 7", w=120)}</div>
<div style="margin-top: auto; display: flex; align-items: center; gap: 12px">{btn("Sound", w=90)}{mono("−17.2 dB · chord 7 · E♭maj7♯11", 11.5, C["faint"])}</div>
</div>'''
boards["PatchEditor.dc.html"] = ("Route patch — chords", page("Patch editor", body))

# 12 Archive list --------------------------------------------------------
items = [("flower", "Stretch", "stretch · 6:40 · Koşuyolu", "published"), ("flower", "Stretch 2", "stretch · 2:32 · Koşuyolu", "published"),
         ("insect", "Grains", "grains · 1:10 · Koşuyolu", "published"), ("tree", "Rhythm", "rhythm · 4 hits · Koşuyolu", "published"),
         ("route", "Koşuyolu Parkı", "route · 1.2 km", "published"), ("animal", "Rhythm 2", "rhythm · 4 hits · Validebağ", "draft"),
         ("water", "Fountain", "zone · no recording", "draft")]
lst = "".join(f'''<button type="button" style="min-height: 60px; display: flex; align-items: center; gap: 12px; padding: 8px 4px; border: 0; border-bottom: 1px solid {C["hairline"]}; background: transparent; text-align: left; cursor: pointer">
{icon(ic, 22, C["lamp"] if st == "published" else C["dim"])}<span style="flex-grow: 1; display: flex; flex-direction: column; gap: 2px"><span style="font-size: 16px">{n}</span>{mono(m, 11.5, C["faint"])}</span>{chip(st)}</button>''' for ic, n, m, st in items)
body = f'''<div style="padding: 56px 16px 34px; box-sizing: border-box; height: {H}px; display: flex; flex-direction: column; gap: 12px">
<div style="display: flex; align-items: center; gap: 8px">{roundbtn("back", "Back to the map")}<span style="font-family: 'Cormorant Garamond', serif; font-size: 28px; font-weight: 600; flex-grow: 1">Archive</span>{chip("28")}</div>
<div style="display: flex; gap: 8px">
<label style="flex-grow: 1; display: flex; align-items: center; gap: 8px; min-height: 44px; padding: 0 12px; border-radius: 10px; border: 1px solid {C["hairline"]}; background: {C["sunk"]}">{icon("search", 18, C["faint"])}<input type="search" aria-label="Filter by name, tag or type" placeholder="name, tag or type" style="flex-grow: 1; border: 0; background: transparent; font-size: 15px; outline: none; min-width: 0"></label>
{btn("Newest ▾", w=110)}</div>
<div style="display: flex; flex-direction: column">{lst}</div>
<div style="margin-top: auto; display: flex; align-items: center; gap: 12px">{btn(icon("upload", 18) + " Publish 2", w=150)}{mono("2 drafts on this phone", 11.5, C["faint"])}</div>
</div>'''
boards["Archive.dc.html"] = ("Archive — setter", page("Archive", body))

# ---------- write + index ----------
order = ["Walk.dc.html", "Places.dc.html", "Layers.dc.html", "PointCard.dc.html", "RouteCard.dc.html", "Account.dc.html",
         "SetterDraw.dc.html", "SetterPoint.dc.html", "StretchDevice.dc.html", "RhythmDevice.dc.html", "PatchEditor.dc.html", "Archive.dc.html"]
# rename Walk to Main (the entry)
index_boards = {}
x0, gap = 0, 80
for i, name in enumerate(order):
    title, html = boards[name]
    fname = "Main.dc.html" if name == "Walk.dc.html" else name
    open(os.path.join(P, fname), "w", encoding="utf-8", newline="\n").write(html)
    row = 0 if i < 6 else 1
    col = i if i < 6 else i - 6
    index_boards[fname] = {"x": col * (W + gap), "y": row * (H + 120) + (0 if row == 0 else 300), "w": W, "h": H, "title": title}
order_files = ["Main.dc.html" if n == "Walk.dc.html" else n for n in order]
now = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
canvas = {"v": 3, "createdOnFiles": {"v": 1, "at": now}, "title": "Fieldscape App Screens", "launch": {"view": "canvas"}, "pages": [],
          "boards": index_boards, "order": order_files,
          "notes": {"listen": {"x": 0, "y": -300, "text": "Listening", "kind": "title1", "maxW": 2800},
                    "set": {"x": 0, "y": H + 120 + 300 - 260, "text": "Setting", "kind": "title1", "maxW": 2800}},
          "designSystems": []}
open(os.path.join(P, "canvas.json"), "w", encoding="utf-8", newline="\n").write(json.dumps(canvas, ensure_ascii=False, indent=1))
print("written", len(order_files))
