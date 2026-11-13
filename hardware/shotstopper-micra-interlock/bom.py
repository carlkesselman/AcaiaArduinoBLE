"""Write a PCBWay-style BOM (CSV) from design.py.   python3 bom.py OUT.csv"""
import csv, os, sys
from collections import OrderedDict
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import design as D

THT = {"J1", "J2", "J3", "J4", "J5", "PS1"}
NOTES = {
    "J1": "Female socket for Arduino Nano ESP32 (Nano not supplied)",
    "J3": "Mount on TOP side",
    "PS1": "Through-hole SIP-4; Murata MEE1S0305SC is a pin-compatible alternative",
    "K1": "Normally-CLOSED type; do not substitute a normally-open relay",
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
        w.writerow([i, ",".join(refs), len(refs), gr["mfr"], mpn, f"{value} - {gr['desc']}",
                    fp.split(":")[1], typ, note])
print("BOM:", sys.argv[1])
