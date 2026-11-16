"""Write a PCBWay-style BOM (CSV) from design.py.   python3 bom.py OUT.csv"""
import csv, os, sys
from collections import OrderedDict
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import design as D

THT = {"J3", "J4", "J7", "PS1"}
NOTES = {
    "J3": ("Same 4-pin latching header as ShotStopper V3.1 (mates the Micra paddle plug); pin 1 = square pad; "
           "latch wall on the side marked with the thick silkscreen bar"),
    "J4": "As J3 (mates the CN9 cable); latch wall on the thick silkscreen bar",
    "J7": "Same 2-pin latching header as ShotStopper V3.1 (5 V from CN4); latch wall on the thick silkscreen bar (board edge side), or + and - swap",
    "PS1": "Through-hole SIP-4, 5 V in / 5 V out",
    "K1": "Normally-CLOSED type; do not substitute a normally-open relay",
    "D2": "LCSC C409780 (as ShotStopper V3.1); cathode mark (pin 2 corner) at the silkscreen dot",
}

groups = OrderedDict()
for ref, (value, fp, mpn, mfr, desc, _) in D.PARTS.items():
    if ref in D.NOT_ASSEMBLED:
        continue
    key = (mpn, value, fp)
    groups.setdefault(key, {"refs": [], "mfr": mfr, "desc": desc, "fp": fp})["refs"].append(ref)

with open(sys.argv[1], "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["Item", "Designator", "Qty", "Manufacturer", "Mfg Part #", "Description / Value",
                "Package/Footprint", "Type", "Notes"])
    for i, ((mpn, value, fp), gr) in enumerate(groups.items(), 1):
        refs = gr["refs"]
        typ = "Thru-hole" if refs[0] in THT else "SMD"
        note = "; ".join(NOTES[r] for r in refs if r in NOTES)
        desc = f"{value} - {gr['desc']}" if len(refs) == 1 else value
        w.writerow([i, ",".join(refs), len(refs), gr["mfr"], mpn, desc,
                    fp.split(":")[1], typ, note])
print("BOM:", sys.argv[1])
