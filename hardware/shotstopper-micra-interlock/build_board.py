"""Build the unrouted KiCad board from design.py and dump pad geometry for route.py.

Run with KiCad's Python:
  /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3 build_board.py OUTDIR
"""
import json, os, sys
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

# ---- outline (from design.outline()) ------------------------------------------------
def seg(a, b):
    s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_SEGMENT)
    s.SetStart(V(*a)); s.SetEnd(V(*b)); s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(MM(0.1))
    board.Add(s)


def arc(start, mid, end):
    s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_ARC)
    s.SetArcGeometry(V(*start), V(*mid), V(*end)); s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(MM(0.1))
    board.Add(s)


for item in D.outline():
    (seg if item[0] == "seg" else arc)(*item[1:])

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


def silk_rect(fp, x0, y0, x1, y1, layer=pcbnew.F_SilkS, w=0.15):
    for a, b in [((x0, y0), (x1, y0)), ((x1, y0), (x1, y1)), ((x1, y1), (x0, y1)), ((x0, y1), (x0, y0))]:
        s = pcbnew.PCB_SHAPE(fp, pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(V(*a)); s.SetEnd(V(*b)); s.SetLayer(layer); s.SetWidth(MM(w))
        fp.Add(s)


def header_fp(n, name):
    """1xN 2.54 mm latching header, pins along +y from pin 1 at the origin.
    Body outline: 5.8 mm wide (measured about 5 mm on V3.1, plus margin), 2.54 mm x N + 2.6 mm long."""
    fp = pcbnew.FOOTPRINT(board)
    fp.SetFPID(pcbnew.LIB_ID("custom", name))
    for i in range(n):
        pad = pcbnew.PAD(fp)
        pad.SetNumber(str(i + 1))
        pad.SetAttribute(pcbnew.PAD_ATTRIB_PTH)
        pad.SetShape(pcbnew.PAD_SHAPE_RECT if i == 0 else pcbnew.PAD_SHAPE_CIRCLE)
        pad.SetSize(V(1.8, 1.8)); pad.SetDrillSize(V(1.1, 1.1))   # 0.63 mm square pin, 0.96 mm max diagonal
        pad.SetLayerSet(pcbnew.PAD.PTHMask())
        fp.Add(pad); pad.SetPosition(V(0, 2.54 * i))
    half = (2.54 * (n - 1) + 2.6 + 2.54) / 2
    cy = 2.54 * (n - 1) / 2
    silk_rect(fp, -2.9, cy - half, 2.9, cy + half)
    silk_rect(fp, -2.95, cy - half - 0.05, 2.95, cy + half + 0.05, pcbnew.F_CrtYd, 0.05)
    # Latch (solid) wall is on the +x side on V3.1, for all three headers: thick bar.
    silk_rect(fp, 2.5, cy - half, 2.9, cy + half, pcbnew.F_SilkS, 0.3)
    if n > 2:                                              # pin 1 dot (J7 is labelled "+" instead)
        m = pcbnew.PCB_SHAPE(fp, pcbnew.SHAPE_T_CIRCLE)
        m.SetCenter(V(-3.6, 0)); m.SetEnd(V(-3.3, 0)); m.SetLayer(pcbnew.F_SilkS); m.SetWidth(MM(0.3))
        fp.Add(m)
    return fp


def led_fp():
    """MEIHUA MHP5050RGBDT on V3.1's land pattern and in V3.1's orientation: 1.3 mm pads,
    1.6 mm pitch, rows 4.5 mm apart. Datasheet pins: anodes 1 (B), 3 (G), 5 (R) on one
    side, cathodes 2 (B), 4 (G), 6 (R) on the other, cathode mark at the pin 2 corner.
    V3.1 has the anode side on +y (3V3) and blue at +x."""
    fp = pcbnew.FOOTPRINT(board)
    fp.SetFPID(pcbnew.LIB_ID("custom", "LED_RGB_5050_MHP"))
    pos = {1: (1.6, 2.25), 3: (0, 2.25), 5: (-1.6, 2.25), 2: (1.6, -2.25), 4: (0, -2.25), 6: (-1.6, -2.25)}
    for num, (x, y) in pos.items():
        pad = pcbnew.PAD(fp)
        pad.SetNumber(str(num)); pad.SetAttribute(pcbnew.PAD_ATTRIB_SMD)
        pad.SetShape(pcbnew.PAD_SHAPE_RECT); pad.SetSize(V(1.3, 1.3))
        pad.SetLayerSet(pcbnew.PAD.SMDMask())
        fp.Add(pad); pad.SetPosition(V(x, y))
    silk_rect(fp, -2.6, -2.6, 2.6, 2.6, pcbnew.F_Fab, 0.1)
    silk_rect(fp, -2.75, -3.15, 2.75, 3.15, pcbnew.F_CrtYd, 0.05)
    m = pcbnew.PCB_SHAPE(fp, pcbnew.SHAPE_T_CIRCLE)        # cathode mark (pin 2 corner)
    m.SetCenter(V(3.1, -3.3)); m.SetEnd(V(2.9, -3.3)); m.SetLayer(pcbnew.F_SilkS); m.SetWidth(MM(0.3))
    fp.Add(m)
    return fp


CUSTOM = {"custom:Header_1x04_Latching": lambda: header_fp(4, "Header_1x04_Latching"),
          "custom:Header_1x02_Latching": lambda: header_fp(2, "Header_1x02_Latching"),
          "custom:LED_RGB_5050_MHP": led_fp}


for ref, (value, fpid, mpn, mfr, desc, place) in D.PARTS.items():
    fp = CUSTOM[fpid]() if fpid in CUSTOM else load(fpid)
    for z in list(fp.Zones()):          # e.g. the module's generous antenna keep-out:
        fp.Remove(z)                    # the board edge does that job here
    if ref == "U3":                     # 0.2 mm thermal vias in the module's ground pad:
        for pad in list(fp.Pads()):     # not needed at 0.3 W, and below PCBWay's 0.3 mm drill
            if pad.GetDrillSize().x > 0:
                fp.Remove(pad)
    if ref == "USB1":                   # front shell slots: 1.4 mm long as on HRO's drawing and V3.1
        for pad in fp.Pads():           # (the library's 1.2 mm is at PCBWay's slot length/width limit)
            d = pad.GetDrillSize()
            if abs(pcbnew.ToMM(d.y) - 1.2) < 0.01 and abs(pcbnew.ToMM(d.x) - 0.6) < 0.01:
                pad.SetDrillSize(V(0.6, 1.4)); pad.SetSize(V(1.0, 1.8))
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
    for layer, r, w in ((pcbnew.F_CrtYd, D.MOUNT_KEEPOUT_R[i], 0.05), (pcbnew.F_SilkS, 1.95, 0.15)):
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
