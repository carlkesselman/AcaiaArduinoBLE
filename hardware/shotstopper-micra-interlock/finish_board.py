"""Add routes.json tracks/vias and silkscreen to the board; save the final PCB.

Run with KiCad's Python:  python3 finish_board.py BUILD_DIR OUT_PCB
"""
import json, os, sys
import pcbnew
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import design as D

BUILD, OUT_PCB = sys.argv[1], sys.argv[2]
MM = pcbnew.FromMM


def V(x, y):
    return pcbnew.VECTOR2I(MM(x), MM(y))


board = pcbnew.LoadBoard(os.path.join(BUILD, "unrouted.kicad_pcb"))
nets = {n.GetNetname(): n for n in board.GetNetsByName().values()}
r = json.load(open(os.path.join(BUILD, "routes.json")))
assert not r["failed"], r["failed"]
LAY = {"F": pcbnew.F_Cu, "B": pcbnew.B_Cu}
for n, L, x1, y1, x2, y2, w in r["tracks"]:
    t = pcbnew.PCB_TRACK(board)
    t.SetStart(V(x1, y1)); t.SetEnd(V(x2, y2)); t.SetWidth(MM(w)); t.SetLayer(LAY[L])
    t.SetNet(nets[n]); board.Add(t)
for n, x, y in r["vias"]:
    v = pcbnew.PCB_VIA(board)
    v.SetPosition(V(x, y)); v.SetWidth(MM(D.VIA_D)); v.SetDrill(MM(D.VIA_DRILL))
    v.SetViaType(pcbnew.VIATYPE_THROUGH); v.SetNet(nets[n]); board.Add(v)

# references go to the fab layer (assembly drawing); the silkscreen gets
# hand-placed labels instead, which keeps it readable on this dense board
for fp in board.GetFootprints():
    fp.Reference().SetLayer(pcbnew.F_Fab)
    fp.Reference().SetVisible(True)
    for f in fp.GetFields():
        if f.GetName() in ("MPN", "Manufacturer", "Datasheet", "Description"):
            f.SetLayer(pcbnew.F_Fab)
            f.SetVisible(False)


# Ground pour on the bottom layer under the ESP32 section (logic side only)
x0, y0, x1, y1 = D.GND_POUR
zone = pcbnew.ZONE(board)
zone.SetLayer(pcbnew.B_Cu)
zone.SetNet(nets["GND"])
ol = zone.Outline()
ol.NewOutline()
for x, y in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
    ol.Append(MM(x), MM(y))
zone.SetLocalClearance(MM(0.3))
zone.SetMinThickness(MM(0.25))
zone.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
board.Add(zone)
pcbnew.ZONE_FILLER(board).Fill(board.Zones())


def text(s, x, y, size=0.9, layer=pcbnew.F_SilkS, angle=0, just=0):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(s); t.SetPosition(V(x, y)); t.SetLayer(layer)
    t.SetTextSize(V(size, size)); t.SetTextThickness(MM(size * 0.15))
    t.SetTextAngleDegrees(angle)
    if layer == pcbnew.B_SilkS:
        t.SetMirrored(True)
    if just:
        t.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_LEFT if just < 0 else pcbnew.GR_TEXT_H_ALIGN_RIGHT)
    board.Add(t)


F, B = pcbnew.F_SilkS, pcbnew.B_SilkS
# ---- top ----
jx3, jy = D.J3_PIN1
jx4 = D.J4_PIN1[0]
text("J3 BUTTONS", jx3, 0.9, 0.8)
text("J4 BRAIN", jx4, 0.9, 0.8)
text("(paddle)", jx3, 2.2, 0.8)
text("(to CN9)", jx4, 2.2, 0.8)
# Pin labels are the wire colours of the plugs, as fitted on V3.1: pin 1 black
# (common) on both; pin 4 white on the paddle's plug, yellow on the CN9 cable.
for jx, pin4 in ((jx3, "WHT"), (jx4, "YEL")):
    text("BLK", jx, jy - 3.45, 0.8)
    text(pin4, jx, jy + 3 * 2.54 + 3.4, 0.8)
px, py = D.PWR_PIN1
text("+", px - 2.54 - 3.5, py - 0.5, 1.0)
text("K1", 32.12, 26.0, 0.8)
text("U1", 31.92, 32.05, 0.7)
text("PS1", 47.0, 26.6, 0.8)
text("BOOT", 24.64, 16.6, 0.8)
text("RESET", 24.64, 40.2, 0.8)
# ---- bottom (mirrored), between J3 and J4 ----
lines = ["ShotStopper Micra Interlock v2.0",
         "after ShotStopper V3.1 (MIT)",
         "GPIO5 HI = stop  GPIO8 LO = paddle on",
         "Low-voltage paddle line only"]
for k, line in enumerate(lines):
    text(line, 35.0 + k * 1.5, 12.8, 0.8, B, 90)

pcbnew.SaveBoard(OUT_PCB, board)
print("saved", OUT_PCB, "tracks", len(r["tracks"]), "vias", len(r["vias"]))
