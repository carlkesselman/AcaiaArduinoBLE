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
text("J3 Buttons", 24.8, 0.9, 0.8)
text("J4 Brain", 39.4, 0.9, 0.8)
text("-", 30.2, 36.73, 1.0)
text("+", 30.2, 39.27, 1.0)
text("JP1", 27.6, 15.11, 0.8, F, 90)
text("JP2", 36.65, 15.11, 0.8, F, 90)
text("JP3", 32.15, 12.25, 0.8)
text("JP4", 32.15, 18.1, 0.8)
for jx in (29.0, 35.3):
    text("8", jx, 12.6, 0.8); text("12", jx, 17.65, 0.8)
text("K1", 24.5, 26.25, 0.8)
text("U2", 24.3, 37.3, 0.8)
text("PS1", 40.4, 27.6, 0.8)
text("USB", 9.06, 1.2, 0.8)
# ---- bottom (mirrored), in the free area between the Nano header rows ----
lines = ["ShotStopper Micra Interlock v1.0",
         "after ShotStopper V2.1 (MIT)",
         "Paddle pin 8: JP1+JP2 to 8, JP4",
         "Paddle pin 12: JP1+JP2 to 12, JP3",
         "D5 HI = stop  D10 LO = paddle on",
         "Low-voltage paddle line only"]
for k, line in enumerate(lines):
    text(line, 4.0 + k * 1.45, 22.5, 0.8, B, 90)

pcbnew.SaveBoard(OUT_PCB, board)
print("saved", OUT_PCB, "tracks", len(r["tracks"]), "vias", len(r["vias"]))
