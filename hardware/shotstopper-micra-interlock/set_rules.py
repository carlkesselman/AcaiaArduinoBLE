"""Set the board's design rules in the KiCad project file (run after finish_board.py,
which rewrites it).   python3 set_rules.py PROJECT.kicad_pro"""
import json, sys

p = json.load(open(sys.argv[1]))
ds = p["board"]["design_settings"]
ds["rules"].update({
    "min_copper_edge_clearance": 0.25,   # the ESP32 module's edge pads sit 0.3 mm in, as on V3.1
    "min_track_width": 0.15,             # USB-C signal pads are 0.5 mm apart
})
# The module's courtyard includes its antenna clearance area, where V3.1's EN and
# reset parts sit; courtyard overlaps are reviewed by hand (README) instead.
ds["rule_severities"]["courtyards_overlap"] = "warning"
json.dump(p, open(sys.argv[1], "w"), indent=2)
