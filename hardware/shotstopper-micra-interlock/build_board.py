"""Build the unrouted KiCad board from design.py and dump pad geometry for route.py.

Run with KiCad's Python:
  /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3 build_board.py OUTDIR
"""
import json, math, os, sys
import pcbnew
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import design as D

OUT = sys.argv[1]
os.makedirs(OUT, exist_ok=True)
MM = pcbnew.FromMM


def V(x, y):
    return pcbnew.VECTOR2I(MM(x), MM(y))


board = pcbnew.BOARD()
ds = board.GetDesignSettings()
ds.SetCopperLayerCount(2)

# ---- outline: TL, TR, BR rounded, BL square --------------------------------------
W, H, R = D.BOARD_W, D.BOARD_H, D.CORNER_R


def seg(a, b):
    s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_SEGMENT)
    s.SetStart(V(*a)); s.SetEnd(V(*b)); s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(MM(0.1))
    board.Add(s)


def arc(c, start, mid, end):
    s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_ARC)
    s.SetArcGeometry(V(*start), V(*mid), V(*end)); s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(MM(0.1))
    board.Add(s)


k = R * (1 - math.sqrt(0.5))
seg((R, 0), (W - R, 0))
arc(None, (W - R, 0), (W - k, k), (W, R))
seg((W, R), (W, H - R))
arc(None, (W, H - R), (W - k, H - k), (W - R, H))
seg((W - R, H), (0, H))
seg((0, H), (0, R))
arc(None, (0, R), (k, k), (R, 0))

# ---- nets ----------------------------------------------------------------------------
nets = {}
for name in D.NETS:
    n = pcbnew.NETINFO_ITEM(board, name)
    board.Add(n)
    nets[name] = n
pad_net = {}
for name, pins in D.NETS.items():
    for ref, pin in pins:
        pad_net[(ref, pin)] = name


def load(fpid):
    lib, name = fpid.split(":")
    return pcbnew.FootprintLoad(os.path.join(D.LIB, lib + ".pretty"), name)


def ribbon_fp(ref, value):
    fp = pcbnew.FOOTPRINT(board)
    fp.SetFPID(pcbnew.LIB_ID("custom", "Ribbon_2x08_P2.54mm_V2.1"))
    p1 = D.ribbon_pin_pos(ref, 1)
    fp.SetPosition(V(*p1))
    for pin in range(1, 17):
        x, y = D.ribbon_pin_pos(ref, pin)
        pad = pcbnew.PAD(fp)
        pad.SetNumber(str(pin))
        pad.SetAttribute(pcbnew.PAD_ATTRIB_PTH)
        pad.SetShape(pcbnew.PAD_SHAPE_RECT if pin == 1 else pcbnew.PAD_SHAPE_CIRCLE)
        pad.SetSize(V(1.7, 1.7))
        pad.SetDrillSize(V(1.0, 1.0))
        pad.SetLayerSet(pcbnew.PAD.PTHMask())
        fp.Add(pad)
        pad.SetPosition(V(x, y))
    # mating IDC plug outline as on V2.1 (key notch toward the outside)
    xo, xe = p1[0], p1[0] - 2.54
    left, right = xe - 3.4, xo + 3.2
    top, bot = p1[1] - 3.45, p1[1] + 7 * 2.54 + 3.3
    for a, b in [((left, top), (right, top)), ((right, top), (right, bot)),
                 ((right, bot), (left, bot)), ((left, bot), (left, top))]:
        s = pcbnew.PCB_SHAPE(fp, pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(V(*a)); s.SetEnd(V(*b)); s.SetLayer(pcbnew.F_SilkS); s.SetWidth(MM(0.15))
        fp.Add(s)
    cy = (top + bot) / 2
    s = pcbnew.PCB_SHAPE(fp, pcbnew.SHAPE_T_RECT)
    s.SetStart(V(right - 0.6, cy - 2.5)); s.SetEnd(V(right, cy + 2.5))
    s.SetLayer(pcbnew.F_SilkS); s.SetWidth(MM(0.15)); fp.Add(s)
    return fp


for ref, (value, fpid, mpn, mfr, desc, place) in D.PARTS.items():
    if fpid.startswith("custom:Ribbon"):
        fp = ribbon_fp(ref, value)
    else:
        fp = load(fpid)
        x, y, rot = place
        fp.SetPosition(V(x, y))
        fp.SetOrientationDegrees(rot)
    fp.SetReference(ref)
    fp.SetValue(value)
    fp.Value().SetVisible(False)
    fp.Reference().SetTextSize(V(0.8, 0.8))
    fp.Reference().SetTextThickness(MM(0.12))
    if ref in D.NOT_ASSEMBLED:
        fp.SetAttributes(fp.GetAttributes() | pcbnew.FP_EXCLUDE_FROM_BOM | pcbnew.FP_EXCLUDE_FROM_POS_FILES)
    fp.SetField("MPN", mpn)            # hidden on F.Fab by finish_board.py
    fp.SetField("Manufacturer", mfr)
    board.Add(fp)
    for pad in fp.Pads():
        key = (ref, pad.GetNumber())
        if key in pad_net:
            pad.SetNet(nets[pad_net[key]])

for i, (x, y) in enumerate(D.MOUNTING_HOLES):
    # M3 clearance hole; courtyard = copper keep-out for a 5 mm hex standoff
    fp = pcbnew.FOOTPRINT(board)
    fp.SetFPID(pcbnew.LIB_ID("custom", "MountingHole_3.2mm_M3_Standoff"))
    fp.SetAttributes(pcbnew.FP_EXCLUDE_FROM_BOM | pcbnew.FP_EXCLUDE_FROM_POS_FILES | pcbnew.FP_BOARD_ONLY)
    fp.SetPosition(V(x, y))
    pad = pcbnew.PAD(fp)
    pad.SetAttribute(pcbnew.PAD_ATTRIB_NPTH)
    pad.SetShape(pcbnew.PAD_SHAPE_CIRCLE)
    pad.SetSize(V(3.2, 3.2)); pad.SetDrillSize(V(3.2, 3.2))
    pad.SetLayerSet(pcbnew.PAD.UnplatedHoleMask())
    fp.Add(pad); pad.SetPosition(V(x, y))
    for layer, r, w in ((pcbnew.F_CrtYd, D.MOUNT_KEEPOUT_R, 0.05), (pcbnew.F_SilkS, 1.95, 0.15)):
        c = pcbnew.PCB_SHAPE(fp, pcbnew.SHAPE_T_CIRCLE)
        c.SetCenter(V(x, y)); c.SetEnd(V(x + r, y)); c.SetLayer(layer); c.SetWidth(MM(w))
        fp.Add(c)
    fp.SetReference(f"H{i+1}")
    fp.Reference().SetVisible(False)
    board.Add(fp)

pcbnew.SaveBoard(os.path.join(OUT, "unrouted.kicad_pcb"), board)

# ---- dump pad geometry -------------------------------------------------------------
pads = []
for fp in board.GetFootprints():
    for pad in fp.Pads():
        layers = []
        for L, nm in ((pcbnew.F_Cu, "F"), (pcbnew.B_Cu, "B")):
            if pad.IsOnLayer(L):
                poly = pad.GetEffectivePolygon(L, pcbnew.ERROR_INSIDE)
                pts = []
                ol = poly.Outline(0)
                for j in range(ol.PointCount()):
                    p = ol.CPoint(j)
                    pts.append((pcbnew.ToMM(p.x), pcbnew.ToMM(p.y)))
                layers.append((nm, pts))
        c = pad.GetPosition()
        drill = pcbnew.ToMM(pad.GetDrillSize().x) if pad.GetDrillSize().x else 0
        pads.append({"ref": fp.GetReference(), "pin": pad.GetNumber(), "net": pad.GetNetname(),
                     "x": pcbnew.ToMM(c.x), "y": pcbnew.ToMM(c.y), "layers": layers, "drill": drill})
json.dump(pads, open(os.path.join(OUT, "pads.json"), "w"), indent=0)
print("pads:", len(pads))
