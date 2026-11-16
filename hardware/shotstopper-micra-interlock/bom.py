"""Write a PCBWay-style BOM (CSV) from design.py.   python3 bom.py OUT.csv"""
import csv, os, sys
from collections import OrderedDict
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import design as D

THT = {"J3", "J4", "J7", "PS1"}
LATCH = "latch (solid) wall on the side marked LATCH / thick silkscreen bar"
NOTES = {
    "J3": f"Through-hole, hand solder. Pin 1 = square pad; {LATCH}",
    "J7": f"Through-hole, hand solder. {LATCH} (toward the board edge); reversed = + and - swapped",
    "PS1": "Through-hole SIP-4, hand solder (no reflow). Pin 1 = square pad",
    "K1": "Normally-CLOSED (1-Form-B) type: NO SUBSTITUTION. Pin 1 at the silkscreen dot",
    "U1": "Pin 1 at the silkscreen dot (part is turned 180 degrees)",
    "D2": "Source by LCSC C409780. Cathode mark (pin 2 corner) at the silkscreen dot",
    "SW1": "Source by LCSC C318884",
    "USB1": "SMD pads reflow; the four shell tabs are plated slots without paste: hand solder them",
    "U3": "ESP32-C3-WROOM-02-H4 is an acceptable alternate",
    "F1": "Any 1206 PPTC with 0.5 A hold, 1 A trip, >= 6 V is acceptable",
}
# Full description for parts that share a line (the per-part purpose is dropped there).
SPEC = {
    "CL10A106KP8NNNC": "10uF 10V X5R 20% 0603 ceramic capacitor",
    "CL10B104KB8NNNC": "0.1uF 50V X7R 10% 0603 ceramic capacitor",
    "CL10A475KO8NNNC": "4.7uF 16V X5R 10% 0603 ceramic capacitor",
    "CL10A105KB8NNNC": "1uF 50V X5R 10% 0603 ceramic capacitor",
    "CL10B472KB8NNNC": "4.7nF 50V X7R 10% 0603 ceramic capacitor",
    "CL21A475KAQNNNE": "4.7uF 25V X5R 10% 0805 ceramic capacitor",
    "CL21A106KAYNNNE": "10uF 25V X5R 10% 0805 ceramic capacitor",
}
ALT = "any equivalent value/size/rating from a major maker is acceptable"

groups = OrderedDict()
for ref, (value, fp, mpn, mfr, desc, _) in D.PARTS.items():
    if ref in D.NOT_ASSEMBLED:
        continue
    g = groups.setdefault(mpn, {"refs": [], "mfr": mfr, "desc": [], "fp": fp, "value": value})
    assert g["fp"] == fp, f"{mpn}: used with two footprints"
    if value not in g["value"].split(" / "):
        g["value"] += " / " + value      # e.g. the two buttons: BOOT / RESET
    g["refs"].append(ref)
    g["desc"].append(desc)

with open(sys.argv[1], "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["Item", "Designator", "Qty", "Manufacturer", "Mfg Part #", "Description / Value",
                "Package/Footprint", "Type", "Notes"])
    for i, (mpn, gr) in enumerate(groups.items(), 1):
        refs = gr["refs"]
        typ = "Thru-hole" if refs[0] in THT else "SMD"
        notes = [NOTES[r] for r in refs if r in NOTES]
        pkg = gr["fp"].split(":")[1]
        if mpn in SPEC:
            desc = SPEC[mpn]
            notes.append(ALT)
        elif refs[0].startswith("R"):
            size = "1206 0.25W" if "1206" in pkg else "0603"
            desc = f"{gr['value']} ohm 1% {size} thick-film resistor"
            notes.append(ALT)
        else:
            desc = f"{gr['value']} - {gr['desc'][0]}"
        w.writerow([i, ",".join(refs), len(refs), gr["mfr"], mpn, desc, pkg, typ, "; ".join(notes)])
print("BOM:", sys.argv[1], len(groups), "lines")
