"""
ShotStopper Micra Interlock (derived from ShotStopper V3.1 by Tate Mazer, MIT)

Single source of truth for the board: outline, parts, placement, nets and
routing domains. build_board.py turns this into a KiCad PCB, route.py routes
it, finish_board.py adds the tracks and exports.

Coordinates: millimetres, KiCad convention (x right, y DOWN), origin at the
top-left corner of the board's bounding box. V3.1 Gerber coordinates (y up)
are converted with
    x_k = x_g - X0,  y_k = Y1 - y_g

The ESP32-C3 section (module, USB-C, regulator, RGB LED, buttons) sits where
it is on V3.1, at V3.1's pad positions. The interlock section (K1, U1, PS1)
replaces V3.1's Q1 optocoupler and spade terminals.
"""
import math

X0, Y1 = 67.945, 96.52             # V3.1 outline left / top edge (Gerber mm)
BOARD_W, BOARD_H = 51.943, 41.275  # V3.1 outline size
CORNER_R = 2.54
CUT_X, CUT_Y = 19.304, 34.925      # bottom-left cut-out under the module antenna


def g(x, y):
    """V3.1 Gerber coordinate -> KiCad board coordinate."""
    return round(x - X0, 3), round(Y1 - y, 3)


def outline():
    """Board edge as ('seg', a, b) / ('arc', start, mid, end), clockwise."""
    W, H, R = BOARD_W, BOARD_H, CORNER_R
    k = R * (1 - math.sqrt(0.5))
    return [
        ("seg", (R, 0), (W - R, 0)),
        ("arc", (W - R, 0), (W - k, k), (W, R)),
        ("seg", (W, R), (W, H - R)),
        ("arc", (W, H - R), (W - k, H - k), (W - R, H)),
        ("seg", (W - R, H), (CUT_X + R, H)),
        ("arc", (CUT_X + R, H), (CUT_X + k, H - k), (CUT_X, H - R)),
        ("seg", (CUT_X, H - R), (CUT_X, CUT_Y + R)),
        ("arc", (CUT_X, CUT_Y + R), (CUT_X - k, CUT_Y + k), (CUT_X - R, CUT_Y)),
        ("seg", (CUT_X - R, CUT_Y), (0, CUT_Y)),
        ("seg", (0, CUT_Y), (0, R)),
        ("arc", (0, R), (k, k), (R, 0)),
    ]


def outline_polygon(step=0.1):
    """The outline as a closed list of points (arcs split into short segments)."""
    pts = []
    for item in outline():
        if item[0] == "seg":
            pts.append(item[1])
            continue
        (x1, y1), (xm, ym), (x2, y2) = item[1:]
        d = 2 * (x1 * (ym - y2) + xm * (y2 - y1) + x2 * (y1 - ym))
        cx = ((x1**2 + y1**2) * (ym - y2) + (xm**2 + ym**2) * (y2 - y1) + (x2**2 + y2**2) * (y1 - ym)) / d
        cy = ((x1**2 + y1**2) * (x2 - xm) + (xm**2 + ym**2) * (x1 - x2) + (x2**2 + y2**2) * (xm - x1)) / d
        r = math.hypot(x1 - cx, y1 - cy)
        a1, am, a2 = (math.atan2(p[1] - cy, p[0] - cx) for p in ((x1, y1), (xm, ym), (x2, y2)))
        sweep = (a2 - a1) % (2 * math.pi)
        if not (0 < (am - a1) % (2 * math.pi) < sweep):
            sweep -= 2 * math.pi
        n = max(2, int(abs(sweep) * r / step))
        pts += [(cx + r * math.cos(a1 + sweep * i / n), cy + r * math.sin(a1 + sweep * i / n)) for i in range(n)]
    return pts


# --- mechanical: identical to V3.1 so the board drops into the same case ---------
MOUNTING_HOLES = [g(70.485, 92.202), g(107.315, 68.072)]
# copper keep-out radius around each M3 hole: H1 (logic side), H2 (among the machine-side nets)
MOUNT_KEEPOUT_R = (3.3, 3.8)

# --- Machine connectors: one 1x4 latching header each, in the middle four holes of
# the even row of V3.1's 2x8 pattern, where V3.1 fits them for the Micra. Header
# pin 1 = V3.1 ribbon pin 6 (common, black wire), pin 4 = ribbon pin 12 (paddle).
# J3 "Buttons": the paddle's own plug.  J4 "Brain": the cable to CN9.
J3_PIN1 = g(99.06, 86.487)
J4_PIN1 = g(113.665, 86.487)
# Power: 1x2, pin 1 = GND, pin 2 = +5 V from the Micra's CN4. Turned 90 degrees from
# V3.1 (pins side by side, + on the left, latch wall toward the board edge) so the
# 7.6 x 5 mm body stays inside the outline and clear of the sense circuit.
PWR_PIN1 = (41.82, 37.9)
PWR_ROT = 270

# --- Parts ---------------------------------------------------------------------------
# ref: (value, kicad footprint, MPN, manufacturer, description, (x, y, rot))
LIB = "/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints"
R0603 = "Resistor_SMD:R_0603_1608Metric"
R1206 = "Resistor_SMD:R_1206_3216Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
C0805 = "Capacitor_SMD:C_0805_2012Metric"
HDR4 = "custom:Header_1x04_Latching"
HDR2 = "custom:Header_1x02_Latching"
# Headers: TE AMPMODU MOD II, 2.54 mm, partially shrouded with a detent (latch) window.
# The V3.1 seller's cable plugs are the MOD II housings 280358 (2-pos) and 280359
# (4-pos); these are the mating headers. Listed body: 12.7 x 5 mm and 7.7 x 5 mm,
# which matches the headers measured on V3.1 (12.4 x 5 and 7.5 x 5 mm).
# AMP drawing C-280370: D max 12.7 / 7.7 mm, 5.0 mm wide, 12.6 mm tall, 0.63 mm square
# pins, pin row 2.2 mm from one face. -1 = tin plated.
LATCH4_MPN = "280371-1"
LATCH2_MPN = "280370-1"


def v31(x, y, rot):
    return g(x, y) + (rot,)


PARTS = {
    # ---- ESP32-C3 section, as on V3.1 ----
    "U3": ("ESP32-C3-WROOM-02-N4", "RF_Module:ESP32-C3-WROOM-02", "ESP32-C3-WROOM-02-N4", "Espressif",
           "ESP32-C3 module, 4 MB flash, PCB antenna", (9.805, 27.82, 180)),
    "USB1": ("USB-C", "Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12", "TYPE-C-31-M-12", "HRO",
             "USB-C receptacle (power + programming)", (16.255, 2.69, 180)),
    "U2": ("TLV75733P", "Package_TO_SOT_SMD:SOT-23-5", "TLV75733PDBVR", "Texas Instruments",
           "LDO 3.3 V 1 A", v31(91.44, 83.947, 0)),
    "D1": ("PMEG6010CEH", "Diode_SMD:D_SOD-123F", "PMEG6010CEH,115", "Nexperia",
           "Schottky 60 V 1 A: USB 5 V into VIN", (25.781, 5.969, 90)),
    "D2": ("RGB", "custom:LED_RGB_5050_MHP", "MHP5050RGBDT", "MEIHUA",
           "RGB LED 5050 (LCSC C409780, as V3.1); anodes 1/3/5, cathodes 2 B, 4 G, 6 R", v31(70.612, 84.963, 0)),
    "D3": ("PRTR5V0U2X", "Package_TO_SOT_SMD:SOT-143", "PRTR5V0U2X,215", "Nexperia",
           "USB ESD protection", v31(82.423, 86.741, 90)),
    "SW1": ("BOOT", "Button_Switch_SMD:SW_Push_1P1T_XKB_TS-1187A", "TS-1187A-B-A-B", "XKB",
            "Tactile switch (boot, GPIO9)", v31(92.583, 75.438, 90)),
    "SW2": ("RESET", "Button_Switch_SMD:SW_Push_1P1T_XKB_TS-1187A", "TS-1187A-B-A-B", "XKB",
            "Tactile switch (reset, EN)", v31(92.583, 60.833, 90)),
    "C1": ("10u", C0603, "CL10A106KP8NNNC", "Samsung", "3V3 bulk", v31(74.422, 83.185, 90)),
    "C2": ("0.1u", C0603, "CL10B104KB8NNNC", "Samsung", "3V3 decoupling", v31(76.2, 83.185, 90)),
    "C3": ("4.7u", C0603, "CL10A475KO8NNNC", "Samsung", "LDO input", v31(83.312, 83.185, 90)),
    "C4": ("4.7u", C0603, "CL10A475KO8NNNC", "Samsung", "LDO output", v31(77.978, 83.185, 90)),
    "C5": ("1u", C0603, "CL10A105KB8NNNC", "Samsung", "EN delay", v31(88.9, 62.103, 270)),
    "C6": ("0.1u", C0603, "CL10B104KB8NNNC", "Samsung", "EN filter", v31(88.9, 58.674, 270)),
    "C7": ("4.7n", C0603, "CL10B472KB8NNNC", "Samsung", "USB shield to GND", v31(79.756, 83.185, 90)),
    "R2": ("10k", R0603, "RC0603FR-0710KL", "Yageo", "EN pull-up", v31(88.9, 65.532, 270)),
    "R3": ("499", R0603, "RC0603FR-07499RL", "Yageo", "LED blue", v31(74.422, 86.741, 90)),
    "R4": ("499", R0603, "RC0603FR-07499RL", "Yageo", "LED green", v31(76.2, 86.741, 90)),
    "R5": ("499", R0603, "RC0603FR-07499RL", "Yageo", "LED red", v31(77.978, 86.741, 90)),
    "R6": ("5.1k", R0603, "RC0603FR-075K1L", "Yageo", "USB CC pull-down", v31(79.756, 86.741, 90)),
    "R7": ("5.1k", R0603, "RC0603FR-075K1L", "Yageo", "USB CC pull-down", v31(86.868, 86.741, 90)),
    "R8": ("1M", R0603, "RC0603FR-071ML", "Yageo", "USB shield to GND", v31(81.534, 83.185, 90)),
    # ---- connectors ----
    "J3": ("Buttons", HDR4, LATCH4_MPN, "TE Connectivity", "1x4 2.54 mm latching header: paddle plug", J3_PIN1 + (0,)),
    "J4": ("Brain", HDR4, LATCH4_MPN, "TE Connectivity", "1x4 2.54 mm latching header: cable to CN9", J4_PIN1 + (0,)),
    "J7": ("PWR", HDR2, LATCH2_MPN, "TE Connectivity", "1x2 2.54 mm latching header: 5 V from CN4 (1 = GND, 2 = +5 V)",
           PWR_PIN1 + (PWR_ROT,)),
    # ---- interlock section ----
    "K1": ("CPC1117N", "Package_SO:SOP-4_3.8x4.1mm_P2.54mm", "CPC1117NTR", "Littelfuse",
           "OptoMOS relay 1-Form-B (normally closed) 60V 150mA", (32.12, 29.2, 0)),
    "U1": ("EL357N", "Package_SO:SO-4_4.4x3.6mm_P2.54mm", "EL357N(C)(TA)-G", "Everlight",
           "Optocoupler, paddle sense", (31.92, 34.6, 180)),
    # PS1 runs from the machine's 5 V only (J7 through D6), never from USB: with the
    # machine off there is then no sense voltage on the control board's input. It is
    # not on the 3.3 V rail, so its ~60 mA does not heat the SOT-23-5 regulator.
    "PS1": ("B0505S-1WR3", "Converter_DCDC:Converter_DCDC_Murata_MEE1SxxxxSC_THT", "B0505S-1WR3", "Mornsun",
            "Isolated DC-DC 5V->5V 1W SIP-4", (50.02, 38.2, 180)),
    # D5: reverse-polarity protection for the hand-soldered J7, and stops USB 5 V
    # from back-feeding the machine's CN4 rail.
    "D5": ("PMEG6010CEH", "Diode_SMD:D_SOD-123F", "PMEG6010CEH,115", "Nexperia",
           "Schottky 60 V 1 A: J7 5 V into VIN", (24.6, 29.7, 0)),
    "D6": ("B0520WS", "Diode_SMD:D_SOD-323", "B0520WS-7-F", "Diodes Inc",
           "Schottky 20 V 0.5 A: J7 5 V into PS1", (24.6, 31.6, 0)),
    # F1 limits what a fault on this board (shorted C3, C8 or PS1) can draw from the
    # machine's CN4 5 V supply. Normal draw is about 0.14 A.
    "F1": ("0.5A", "Fuse:Fuse_1206_3216Metric", "MF-NSMF050-2", "Bourns",
           "Resettable fuse 0.5 A hold / 1 A trip: J7 input", (30.3, 38.7, 0)),
    # D7 clamps the isolated rail if PS1 ever runs unloaded (R11 open), so the sense
    # circuit can never put more than about 6.5 V behind R10 on the paddle line.
    "D7": ("6.2V", "Diode_SMD:D_SOD-123", "MMSZ5234BT1G", "onsemi",
           "Zener 6.2 V 500 mW: isolated rail clamp", (40.0, 23.4, 0)),
    "D4": ("1N4148WS", "Diode_SMD:D_SOD-323", "1N4148WS-7-F", "Diodes Inc", "Signal diode", (39.02, 32.7, 180)),
    "R1": ("390", R0603, "RC0603FR-07390RL", "Yageo", "K1 LED current", (26.87, 27.4, 90)),
    "R9": ("10k", R0603, "RC0603FR-0710KL", "Yageo", "K1 pull-down (closed in reset)", (22.6, 27.6, 0)),
    # R10: about 1.4 mA through U1's LED with the paddle on. 2.2 k (not less) also
    # limits what the sense branch can push into the control board's input in a
    # fault (R11 open, a reversed J4, or the control board unpowered).
    "R10": ("2.2k", R0603, "RC0603FR-072K2L", "Yageo", "Paddle sense current", (41.22, 34.2, 0)),
    # R11: about 21 mA at 4.6 V, PS1's 10 % minimum load (unloaded, its output is
    # unspecified and can rise well above 5 V). About 0.1 W, hence 1206.
    "R11": ("220", R1206, "RC1206FR-07220RL", "Yageo", "DC-DC minimum load (0.25 W)", (37.2, 20.9, 0)),
    # R12 must stay high: a low value here bypasses U1's LED, and then a shorted D4
    # with PS1 off pulls the paddle line low through R12, R10 and R11 (the machine
    # would brew with the paddle off). At 100 k the LED blocks that path.
    "R12": ("100k", R0603, "RC0603FR-07100KL", "Yageo", "U1 LED leakage shunt", (38.22, 34.2, 0)),
    "R13": ("47k", R0603, "RC0603FR-0747KL", "Yageo", "Paddle sense pull-up", (21.6, 25.9, 0)),
    "C8": ("4.7u", C0805, "CL21A475KAQNNNE", "Samsung", "DC-DC input", (34.7, 39.4, 0)),
    "C9": ("10u", C0805, "CL21A106KAYNNNE", "Samsung", "DC-DC output", (43.72, 27.15, 90)),
}
NOT_ASSEMBLED = set()

# --- Nets ---------------------------------------------------------------------------
USB_GND = [("USB1", p) for p in ("A1", "A12", "B1", "B12")]
USB_VBUS = [("USB1", p) for p in ("A4", "A9", "B4", "B9")]
NETS = {
    # ---- logic side (ESP32 ground) ----
    "GND": USB_GND + [("U3", "9"), ("U3", "19"), ("U2", "2"), ("D3", "1"),
                      ("SW1", "1"), ("SW2", "1"), ("C1", "2"), ("C2", "2"), ("C3", "2"), ("C4", "2"),
                      ("C5", "2"), ("C6", "2"), ("C7", "2"), ("R6", "2"), ("R7", "2"), ("R8", "2"),
                      ("J7", "1"), ("K1", "2"), ("U1", "3"), ("R9", "1"), ("PS1", "1"), ("C8", "2")],
    "VIN_J": [("J7", "2"), ("F1", "2")],
    "VIN_F": [("F1", "1"), ("D5", "2"), ("D6", "2")],
    "VPS": [("D6", "1"), ("PS1", "2"), ("C8", "1")],
    "+3V3": [("U3", "1"), ("U2", "5"), ("C1", "1"), ("C2", "1"), ("C4", "1"), ("R2", "1"),
             ("D2", "1"), ("D2", "3"), ("D2", "5"), ("R13", "1")],
    "VIN": [("D5", "1"), ("D1", "1"), ("U2", "1"), ("U2", "3"), ("C3", "1")],
    "VUSB": USB_VBUS + [("D1", "2"), ("D3", "4")],
    "USB_DP": [("USB1", "A6"), ("USB1", "B6"), ("D3", "3"), ("U3", "14")],   # GPIO19
    "USB_DN": [("USB1", "A7"), ("USB1", "B7"), ("D3", "2"), ("U3", "13")],   # GPIO18
    "CC1": [("USB1", "A5"), ("R7", "1")],
    "CC2": [("USB1", "B5"), ("R6", "1")],
    "SHIELD": [("USB1", "SH"), ("R8", "1"), ("C7", "1")],
    "EN": [("U3", "2"), ("R2", "2"), ("C5", "1"), ("C6", "1"), ("SW2", "2")],
    "BOOT": [("U3", "8"), ("SW1", "2")],                                     # GPIO9
    "LED_B": [("U3", "10"), ("R3", "1")],                                    # GPIO10
    "LED_G": [("U3", "11"), ("R4", "1")],                                    # GPIO20
    "LED_R": [("U3", "12"), ("R5", "1")],                                    # GPIO21
    "LEDK_B": [("R3", "2"), ("D2", "2")],
    "LEDK_G": [("R4", "2"), ("D2", "4")],
    "LEDK_R": [("R5", "2"), ("D2", "6")],
    "STOP": [("U3", "4"), ("R1", "1"), ("R9", "2")],                         # GPIO5: HIGH opens K1
    "K1_A": [("R1", "2"), ("K1", "1")],
    "SENSE": [("U3", "7"), ("R13", "2"), ("U1", "4")],                       # GPIO8: LOW = paddle on
    # ---- machine side (isolated, referenced to COMMON) ----
    "ISO5V": [("PS1", "4"), ("R10", "2"), ("R11", "1"), ("C9", "1"), ("D7", "1")],
    "COMMON": [("PS1", "3"), ("R11", "2"), ("C9", "2"), ("D7", "2"), ("J3", "1"), ("J4", "1")],
    "U1_A": [("R10", "1"), ("R12", "1"), ("U1", "1")],
    "U1_K": [("U1", "2"), ("R12", "2"), ("D4", "2")],
    "PADDLE": [("D4", "1"), ("K1", "4"), ("J3", "4")],
    "BRAIN": [("K1", "3"), ("J4", "4")],
    "KEY2": [("J3", "2"), ("J4", "2")],     # CN9 pins the Micra doesn't use: straight through
    "KEY3": [("J3", "3"), ("J4", "3")],
}
MACHINE_NETS = {"ISO5V", "COMMON", "U1_A", "U1_K", "PADDLE", "BRAIN", "KEY2", "KEY3"}
LOGIC_NETS = set(NETS) - MACHINE_NETS

TRACK_W = {"VIN": 0.4, "VIN_J": 0.4, "VIN_F": 0.4, "VPS": 0.3, "VUSB": 0.3, "+3V3": 0.3, "GND": 0.3,
           # USB-C signal pads are 0.5 mm apart
           "USB_DP": 0.15, "USB_DN": 0.15, "CC1": 0.15, "CC2": 0.15}
DEFAULT_TRACK_W = 0.25
CLEARANCE = 0.2
VIA_D, VIA_DRILL = 0.6, 0.3
EDGE_CLEARANCE = 0.5

# Nothing on either layer under the antenna end of the module.
ANTENNA_KEEPOUT = (0.0, 31.0, 18.2, 41.3)
# Ground pour on the bottom layer under the ESP32 section only (x0, y0, x1, y1).
GND_POUR = (0.3, 0.3, 27.6, 30.6)


def domain(x, y, layer):
    """'L' logic, 'M' machine, None = isolation gap / keep-out. layer 'F' or 'B'."""
    ax0, ay0, ax1, ay1 = ANTENNA_KEEPOUT
    if ax0 <= x <= ax1 and ay0 <= y <= ay1:
        return None
    if (y < 25.6 and x < 28.02) or (y >= 25.6 and x < 30.92) \
            or (x > 37.0 and y > 35.4) or (x > 30.92 and y > 37.2) or (layer == "B" and y > 39.0):
        return "L"
    if (y < 24.4 and x > 29.22) or (x > 34.22 and 24.4 <= y < 34.6) \
            or (34.22 < x < 36.8 and 34.6 <= y < 36.6):
        return "M"
    return None
