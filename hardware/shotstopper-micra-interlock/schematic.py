"""Draw the board's schematic as an SVG.   python3 schematic.py schematic.svg

The netlist lives in design.py; this drawing is laid out by hand to match it.
check() below fails if a part or value drawn here is not in design.py, or if a
part in design.py is missing from the drawing.
"""
import os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import design as D

W, H = 1720, 1080
out = []
drawn = set()


def val(ref):
    drawn.add(ref)
    return D.PARTS[ref][0]


def lab(ref):
    return f"{ref} {val(ref)}"


def wire(*pts):
    out.append('<polyline points="%s" fill="none" stroke="#111" stroke-width="1.6"/>'
               % " ".join(f"{x},{y}" for x, y in pts))


def dot(x, y):
    out.append(f'<circle cx="{x}" cy="{y}" r="3.2" fill="#111"/>')


def text(x, y, s, anchor="start", size=14, weight=400, color="#111"):
    s = s.replace("&", "&amp;").replace("<", "&lt;")
    out.append(f'<text x="{x}" y="{y}" text-anchor="{anchor}" font-size="{size}" font-weight="{weight}" '
               f'fill="{color}" font-family="Helvetica, Arial, sans-serif">{s}</text>')


def flag(x, y, s, side="left"):
    """Net label at a wire end. side = where the text sits relative to the point."""
    out.append(f'<circle cx="{x}" cy="{y}" r="4" fill="#fff" stroke="#0a58a8" stroke-width="1.6"/>')
    if side == "left":
        text(x - 9, y + 5, s, "end", color="#0a58a8", weight=600)
    elif side == "right":
        text(x + 9, y + 5, s, "start", color="#0a58a8", weight=600)
    elif side == "right-up":
        text(x + 2, y - 10, s, "start", color="#0a58a8", weight=600)
    elif side == "below":
        text(x, y + 20, s, "middle", color="#0a58a8", weight=600)
    else:
        text(x, y - 10, s, "middle", color="#0a58a8", weight=600)


def gnd(x, y):
    for i, hw in enumerate((10, 6, 2)):
        out.append(f'<line x1="{x - hw}" y1="{y + 4 * i}" x2="{x + hw}" y2="{y + 4 * i}" stroke="#111" stroke-width="1.6"/>')


def res_h(x, y, ref):
    out.append(f'<rect x="{x}" y="{y - 8}" width="44" height="16" fill="#fff" stroke="#111" stroke-width="1.6"/>')
    text(x + 22, y - 14, lab(ref), "middle")
    return x + 44


def diode_h(x, y, ref, note=""):
    """Anode at x, cathode at x + 18 (current flows left to right)."""
    out.append(f'<polygon points="{x},{y - 9} {x},{y + 9} {x + 18},{y}" fill="#fff" stroke="#111" stroke-width="1.6"/>')
    out.append(f'<line x1="{x + 18}" y1="{y - 9}" x2="{x + 18}" y2="{y + 9}" stroke="#111" stroke-width="1.6"/>')
    text(x + 9, y - 16, f"{ref} {note or val(ref)}", "middle")
    drawn.add(ref)
    return x + 18


def comp_v(x, y, kind, label, length=62, end="gnd", below=False):
    """Vertical two-pin part hanging from node (x, y)."""
    m = y + length / 2
    if kind == "c":
        wire((x, y), (x, m - 3)); wire((x, m + 3), (x, y + length))
        for yy in (m - 3, m + 3):
            out.append(f'<line x1="{x - 10}" y1="{yy}" x2="{x + 10}" y2="{yy}" stroke="#111" stroke-width="1.8"/>')
    elif kind == "r":
        wire((x, y), (x, m - 15)); wire((x, m + 15), (x, y + length))
        out.append(f'<rect x="{x - 7}" y="{m - 15}" width="14" height="30" fill="#fff" stroke="#111" stroke-width="1.6"/>')
    elif kind == "z":      # cathode (bar) at the top
        wire((x, y), (x, m - 9)); wire((x, m + 9), (x, y + length))
        out.append(f'<polygon points="{x - 9},{m + 9} {x + 9},{m + 9} {x},{m - 9}" fill="#fff" stroke="#111" stroke-width="1.6"/>')
        out.append(f'<polyline points="{x - 13},{m - 5} {x - 9},{m - 9} {x + 9},{m - 9} {x + 13},{m - 13}" fill="none" stroke="#111" stroke-width="1.6"/>')
    elif kind == "sw":
        wire((x, y), (x, m - 10)); wire((x, m + 10), (x, y + length))
        for yy in (m - 10, m + 10):
            out.append(f'<circle cx="{x}" cy="{yy}" r="2.6" fill="#fff" stroke="#111" stroke-width="1.4"/>')
        out.append(f'<line x1="{x}" y1="{m + 10}" x2="{x + 9}" y2="{m - 8}" stroke="#111" stroke-width="1.6"/>')
    if below:
        for i, part in enumerate(label.split(" ", 1)):
            text(x, y + length + 28 + 16 * i, part, "middle", size=13)
    else:
        text(x + 15, m + 5, label)
    if end == "gnd":
        gnd(x, y + length)


def box(x, y, w, h, title, sub=""):
    out.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="#fffbe8" stroke="#111" stroke-width="1.8"/>')
    text(x + w / 2, y + 20, title, "middle", weight=600)
    if sub:
        text(x + w / 2, y + 38, sub, "middle", size=12.5, color="#444")


def pin(x, y, name, side):
    """Pin name inside a box edge; side = which edge the pin is on."""
    text(x + (6 if side == "left" else -6), y + 5, name, "start" if side == "left" else "end", size=12.5)


def section(x, y, w, h, title):
    out.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="none" stroke="#999" stroke-width="1" stroke-dasharray="2 4"/>')
    text(x + 10, y + 22, title, size=16, weight=600, color="#444")


# ------------------------------------------------------------------ power
section(20, 20, 940, 350, "Power")
flag(50, 110, "J7 pin 2: +5 V from CN4", "right-up")
wire((54, 110), (330, 110)); dot(240, 110)
e = diode_h(330, 110, "D5")
wire((e, 110), (560, 110)); dot(470, 110)
flag(50, 170, "USB VBUS", "right-up")
wire((54, 170), (330, 170))
e = diode_h(330, 170, "D1")
wire((e, 170), (470, 170), (470, 110))
text(470, 96, "VIN", "middle", color="#0a58a8", weight=600)
dot(515, 110); comp_v(515, 110, "c", lab("C3"), 50, below=True)
box(560, 50, 150, 100, "U2 " + val("U2"), "3.3 V regulator")
pin(560, 110, "IN, EN", "left"); pin(710, 110, "OUT", "right")
wire((635, 150), (635, 162)); gnd(635, 162)
wire((710, 110), (916, 110)); flag(920, 110, "+3V3", "top")
for x, ref in ((745, "C4"), (805, "C1"), (865, "C2")):
    dot(x, 110); comp_v(x, 110, "c", lab(ref), 50, below=True)

wire((240, 110), (240, 255), (330, 255))
e = diode_h(330, 255, "D6")
wire((e, 255), (560, 255)); dot(455, 255)
text(410, 241, "VPS", "middle", color="#0a58a8", weight=600)
comp_v(455, 255, "c", lab("C8"), 46)
box(560, 190, 150, 145, "PS1 " + val("PS1"), "isolated 5 V to 5 V")
out.append('<line x1="635" y1="262" x2="635" y2="335" stroke="#c0392b" stroke-width="1.6" stroke-dasharray="6 4"/>')
pin(560, 255, "Vin", "left"); pin(560, 310, "GND", "left")
pin(710, 255, "+Vo", "right"); pin(710, 310, "0V", "right")
wire((560, 310), (530, 310), (530, 322)); gnd(530, 322)
wire((710, 255), (916, 255)); flag(920, 255, "ISO5V", "top")
wire((710, 310), (916, 310)); flag(920, 310, "COMMON", "below")
for x, kind, ref in ((745, "c", "C9"), (800, "r", "R11"), (855, "z", "D7")):
    dot(x, 255); dot(x, 310); comp_v(x, 255, kind, "", 55, end="")
text(730, 352, f"{lab('C9')}, {lab('R11')}, {lab('D7')} zener", size=13)
flag(50, 235, "J7 pin 1", "right-up"); wire((54, 235), (150, 235), (150, 248)); gnd(150, 248)
text(40, 340, "PS1 is fed from J7 only: on USB power alone the paddle sensing is off.", size=13, color="#444")
text(40, 358, "D5 and D1 stop either supply back-feeding the other.", size=13, color="#444")

# ------------------------------------------------------------------ ESP32 + USB
section(20, 390, 940, 670, "ESP32-C3, USB, reset and status LED")
box(400, 430, 180, 480, "U3", "ESP32-C3-WROOM-02")
drawn.add("U3")
# left pins
pin(400, 480, "3V3", "left"); wire((400, 480), (330, 480)); flag(326, 480, "+3V3", "left")
pin(400, 540, "EN", "left")
flag(64, 540, "+3V3", "top"); wire((68, 540), (100, 540)); e = res_h(100, 540, "R2"); wire((e, 540), (400, 540))
for x, kind, ref, note in ((190, "c", "C5", ""), (255, "c", "C6", ""), (325, "sw", "SW2", "")):
    dot(x, 540); comp_v(x, 540, kind, f"{ref} {val(ref)}", 50, below=True)
pin(400, 665, "IO9", "left"); wire((400, 665), (325, 665), (325, 650)); comp_v(325, 650, "sw", lab("SW1"), 50, below=True)
box(50, 735, 150, 265, "USB1", "USB-C socket")
drawn.add("USB1")
pin(200, 790, "VBUS", "right"); wire((200, 790), (236, 790)); flag(240, 790, "USB VBUS", "right")
pin(200, 825, "D+", "right"); pin(200, 860, "D-", "right")
pin(400, 825, "IO19", "left"); pin(400, 860, "IO18", "left")
wire((200, 825), (400, 825)); wire((200, 860), (400, 860))
dot(325, 825); dot(360, 860); wire((325, 825), (325, 884)); wire((360, 860), (360, 884))
out.append('<rect x="305" y="884" width="76" height="26" fill="#fffbe8" stroke="#111" stroke-width="1.6"/>')
text(343, 902, "D3 ESD", "middle", size=12.5); drawn.add("D3")
pin(200, 930, "CC1", "right"); wire((200, 930), (222, 930)); e = res_h(222, 930, "R7"); wire((e, 930), (285, 930), (285, 938)); gnd(285, 938)
pin(200, 975, "CC2", "right"); wire((200, 975), (222, 975)); e = res_h(222, 975, "R6")
wire((e, 975), (285, 975), (285, 983)); gnd(285, 983)
text(110, 992, "GND, shield", "middle", size=12.5)
wire((110, 1000), (110, 1012)); gnd(110, 1012)
text(150, 1046, f"shield to GND through {lab('R8')} and {lab('C7')}", size=13, color="#444")
pin(490, 900, "GND", "right"); wire((490, 910), (490, 925)); gnd(490, 925)
# right pins
for y, io, r, col in ((500, "IO10", "R3", "B"), (540, "IO20", "R4", "G"), (580, "IO21", "R5", "R")):
    pin(580, y, io, "right"); wire((580, y), (640, y)); e = res_h(640, y, r); wire((e, y), (760, y))
box(760, 440, 110, 165, "D2", "RGB LED")
drawn.add("D2")
for y, c in ((500, "B"), (540, "G"), (580, "R")):
    pin(760, y, c, "left")
wire((870, 540), (916, 540)); flag(920, 540, "+3V3", "top")
text(815, 624, "anodes to +3V3", "middle", size=12.5, color="#444")
pin(580, 680, "IO5", "right"); wire((580, 680), (916, 680)); flag(920, 680, "STOP", "top")
pin(580, 780, "IO8", "right"); wire((580, 780), (916, 780)); flag(920, 780, "SENSE", "top")
text(600, 850, "Other pins are not connected.", size=13, color="#444")
text(600, 870, "IO8 is also a boot-mode pin: use BOOT with the paddle off.", size=13, color="#444")

# ------------------------------------------------------------------ interlock + sense
section(980, 20, 720, 1040, "Paddle interlock (K1) and paddle sensing (U1)")
BX = 1300
for y0, y1 in ((60, 285), (385, 558)):
    out.append(f'<line x1="{BX}" y1="{y0}" x2="{BX}" y2="{y1}" stroke="#c0392b" stroke-width="1.8" stroke-dasharray="8 5"/>')
text(BX - 12, 78, "ESP32 side", "end", color="#c0392b", weight=600)
text(BX + 12, 78, "machine side (isolated)", "start", color="#c0392b", weight=600)

box(1240, 150, 120, 110, "K1", val("K1"))
text(1300, 252, "normally closed", "middle", size=12.5, color="#444")
pin(1240, 195, "1", "left"); pin(1240, 235, "2", "left"); pin(1360, 195, "4", "right"); pin(1360, 235, "3", "right")
flag(1020, 195, "STOP", "top"); wire((1024, 195), (1110, 195)); dot(1060, 195)
comp_v(1060, 195, "r", lab("R9"), 70)
e = res_h(1110, 195, "R1"); wire((e, 195), (1240, 195))
wire((1240, 235), (1205, 235), (1205, 262)); gnd(1205, 262)
wire((1360, 195), (1616, 195)); flag(1620, 195, "PADDLE", "top")
wire((1360, 235), (1616, 235)); flag(1620, 235, "BRAIN", "right")
text(1000, 320, "IO5 high opens K1: the machine sees the paddle as off.", size=13, color="#444")
text(1000, 340, "IO5 low or undriven (R9): K1 is closed, paddle works as stock.", size=13, color="#444")

box(1240, 420, 120, 120, "U1", val("U1"))
pin(1240, 465, "4 C", "left"); pin(1240, 515, "3 E", "left"); pin(1360, 465, "A 1", "right"); pin(1360, 515, "K 2", "right")
flag(1020, 465, "+3V3", "top"); wire((1024, 465), (1060, 465)); e = res_h(1060, 465, "R13"); wire((e, 465), (1240, 465))
dot(1170, 465); wire((1170, 465), (1170, 420)); flag(1170, 416, "SENSE", "top")
wire((1240, 515), (1205, 515), (1205, 540)); gnd(1205, 540)
flag(1660, 465, "ISO5V", "top"); wire((1656, 465), (1574, 465)); res_h(1530, 465, "R10"); wire((1530, 465), (1360, 465))
dot(1440, 465); dot(1440, 515); comp_v(1440, 465, "r", "", 50, end="")
text(1429, 495, lab("R12"), "end", size=13)
wire((1360, 515), (1530, 515)); e = diode_h(1530, 515, "D4"); wire((e, 515), (1656, 515)); flag(1660, 515, "PADDLE", "below")
text(1000, 580, "Paddle on: about 3 mA flows ISO5V, R10, U1, D4, paddle switch, COMMON;", size=13, color="#444")
text(1000, 600, "U1 pulls SENSE low. Paddle off: D4 blocks, nothing flows in the machine's line.", size=13, color="#444")

box(1140, 680, 150, 220, "J3 Buttons", "paddle's own plug")
box(1460, 680, 150, 220, "J4 Brain", "cable to CN9")
drawn.update(("J3", "J4", "J7"))
for i, y in enumerate((740, 780, 820, 860)):
    pin(1290, y, str(i + 1), "right"); pin(1460, y, str(i + 1), "left")
wire((1290, 740), (1460, 740)); dot(1375, 740); wire((1375, 740), (1375, 712)); flag(1375, 708, "COMMON", "top")
wire((1290, 780), (1460, 780)); wire((1290, 820), (1460, 820))
text(1375, 804, "2, 3: straight through", "middle", size=12.5, color="#444")
wire((1290, 860), (1336, 860)); flag(1340, 860, "PADDLE", "right")
wire((1460, 860), (1444, 860), (1444, 900), (1400, 900)); flag(1396, 900, "BRAIN", "left")
text(1000, 950, "J3 pin 1 black (common), pin 4 white (paddle).", size=13, color="#444")
text(1000, 970, "J4 pin 1 black (common), pin 4 yellow (paddle input on CN9).", size=13, color="#444")
text(1000, 1000, "Measured on CN9: 5 V pull-up through about 10 k; a short to common = paddle on.", size=13, color="#444")


def check():
    missing = set(D.PARTS) - drawn
    extra = drawn - set(D.PARTS)
    assert not missing and not extra, f"not drawn: {sorted(missing)}; not in design: {sorted(extra)}"


check()
svg = (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}">'
       f'<rect width="{W}" height="{H}" fill="#fff"/>'
       f'<text x="{W - 30}" y="{H - 6}" text-anchor="end" font-size="12" fill="#666" '
       f'font-family="Helvetica, Arial, sans-serif">ShotStopper Micra Interlock v2.0 - schematic (netlist: design.py)</text>'
       + "".join(out) + "</svg>")
open(sys.argv[1], "w").write(svg)
print("schematic:", sys.argv[1], len(drawn), "parts")
