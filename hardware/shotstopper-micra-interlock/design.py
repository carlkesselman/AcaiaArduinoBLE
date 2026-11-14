"""
ShotStopper Micra Interlock (derived from ShotStopper V2.1 by Tate Mazer, MIT)

Single source of truth for the board: geometry (copied from the V2.1 Gerbers),
parts, placement, nets and routing domains. build_board.py turns this into a
KiCad PCB, route.py routes it, finish_board.py adds the tracks and exports.

Coordinates: millimetres, KiCad convention (x right, y DOWN), origin at the
board's top-left corner. V2.1 Gerber coordinates (y up) are converted with
    x_k = x_g - X0,  y_k = Y1 - y_g
"""

X0, Y1 = 75.565, 96.393            # V2.1 outline left / top edge (Gerber mm)
BOARD_W, BOARD_H = 44.323, 41.275  # V2.1 outline size
CORNER_R = 2.54                    # V2.1: TL, TR, BR rounded; BL square


def g(x, y):
    """V2.1 Gerber coordinate -> KiCad board coordinate."""
    return round(x - X0, 3), round(Y1 - y, 3)


# --- mechanical: identical to V2.1 so the board drops into the same place -----
MOUNTING_HOLES = [g(84.607, 58.301), g(84.607, 89.535), g(107.315, 67.945), g(107.315, 86.741)]
MOUNT_KEEPOUT_R = 3.0              # copper keep-out radius around the M3 holes

# --- Arduino Nano ESP32 socket (two 1x15 female headers, 15.24 mm apart) -------
NANO_LEFT = ["D13", "3V3", "B0", "A0", "A1", "A2", "A3", "A4", "A5", "A6", "A7", "VBUS", "B1", "GND", "VIN"]
NANO_RIGHT = ["D12", "D11", "D10", "D9", "D8", "D7", "D6", "D5", "D4", "D3", "D2", "GND", "RST", "RX0", "TX1"]
NANO_L_PIN1 = g(77.003, 92.187)
NANO_R_PIN1 = g(92.243, 92.187)

# --- Ribbon connectors: 2x8, 2.54 mm. Pin 1 (square) at top of the odd column,
# even column to its LEFT (as on V2.1). J3 "Buttons" (paddle), J4 "Brain".
J3_ODD_X, J3_EVEN_X = 101.6, 99.06
J4_ODD_X, J4_EVEN_X = 116.205, 113.665
RIBBON_TOP_Y = 91.44


def ribbon_pin_pos(conn, pin):
    odd_x, even_x = (J3_ODD_X, J3_EVEN_X) if conn == "J3" else (J4_ODD_X, J4_EVEN_X)
    row = (pin - 1) // 2
    return g(odd_x if pin % 2 else even_x, RIBBON_TOP_Y - 2.54 * row)


RIBBON_COMMON_PIN = 6
PADDLE_PIN_OPTIONS = (8, 12)

# --- Power header (V2.1 "+/-"): pin 1 = "-" (GND), pin 2 = "+" (Nano VIN) ------
PWR_PIN1 = g(107.798, 59.665)

# --- Parts ---------------------------------------------------------------------------
# ref: (value, kicad footprint, MPN, manufacturer, description, (x, y, rot))
LIB = "/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints"
R0603 = "Resistor_SMD:R_0603_1608Metric"
R1206 = "Resistor_SMD:R_1206_3216Metric"
C0805 = "Capacitor_SMD:C_0805_2012Metric"
JUMPER = "Jumper:SolderJumper-2_P1.3mm_Open_RoundedPad1.0x1.5mm"
JUMPER3 = "Jumper:SolderJumper-3_P1.3mm_Open_RoundedPad1.0x1.5mm"

PARTS = {
    "J1": ("Nano ESP32 L", "Connector_PinSocket_2.54mm:PinSocket_1x15_P2.54mm_Vertical",
           "PPTC151LFBN-RC", "Sullins", "Female header 1x15 2.54mm (Nano left)", NANO_L_PIN1 + (0,)),
    "J2": ("Nano ESP32 R", "Connector_PinSocket_2.54mm:PinSocket_1x15_P2.54mm_Vertical",
           "PPTC151LFBN-RC", "Sullins", "Female header 1x15 2.54mm (Nano right)", NANO_R_PIN1 + (0,)),
    "J3": ("Buttons", "custom:Ribbon_2x08", "PEC08DAAN", "Sullins",
           "Male header 2x8 2.54mm (paddle ribbon)", None),
    "J4": ("Brain", "custom:Ribbon_2x08", "PEC08DAAN", "Sullins",
           "Male header 2x8 2.54mm (control board ribbon)", None),
    "J5": ("PWR", "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical", "PEC02SAAN", "Sullins",
           "Male header 1x2 2.54mm (+ = VIN, - = GND)", PWR_PIN1 + (0,)),
    "K1": ("CPC1117N", "Package_SO:SOP-4_3.8x4.1mm_P2.54mm", "CPC1117NTR", "Littelfuse",
           "OptoMOS relay 1-Form-B (normally closed) 60V 120mA", (24.5, 29.2, 0)),
    "U2": ("EL357N", "Package_SO:SO-4_4.4x3.6mm_P2.54mm", "EL357N(C)(TA)-G", "Everlight",
           "Optocoupler, paddle sense", (24.3, 34.6, 180)),
    "PS1": ("B0305S-1WR3", "Converter_DCDC:Converter_DCDC_Murata_MEE1SxxxxSC_THT", "B0305S-1WR3", "Mornsun",
            "Isolated DC-DC 3.3V->5V 1W SIP-4", (42.4, 38.2, 180)),
    "D1": ("1N4148WS", "Diode_SMD:D_SOD-323", "1N4148WS-7-F", "Diodes Inc", "Signal diode", (31.4, 32.45, 180)),
    "R1": ("390", R0603, "RC0603FR-07390RL", "Yageo", "K1 LED current", (19.25, 27.4, 90)),
    "R2": ("10k", R0603, "RC0603FR-0710KL", "Yageo", "K1 pull-down (closed in reset)", (13.3, 21.986, 0)),
    # R3/R6: about 1.7 mA through U2's LED with the paddle on. R6 < R3 keeps the LED
    # off when PS1 leaks into a 3.3 V paddle line with the paddle off.
    "R3": ("1k", R0603, "RC0603FR-071KL", "Yageo", "Paddle sense current", (33.6, 34.2, 0)),
    # R4: 21 mA, PS1's 10 % minimum load (unloaded, its output is unspecified
    # and can rise well above 5 V). About 105 mW, hence 1206.
    "R4": ("240", R1206, "RC1206FR-07240RL", "Yageo", "DC-DC minimum load (0.25 W)", (36.0, 31.15, 90)),
    "R6": ("680", R0603, "RC0603FR-07680RL", "Yageo", "U2 LED leakage shunt", (30.6, 34.2, 0)),
    "R7": ("47k", R0603, "RC0603FR-0747KL", "Yageo", "Paddle sense pull-up", (13.3, 11.3, 0)),
    "C1": ("4.7u", C0805, "CL21A475KAQNNNE", "Samsung", "DC-DC decoupling", (35.6, 38.0, 90)),
    "C2": ("4.7u", C0805, "CL21A475KAQNNNE", "Samsung", "DC-DC decoupling", (36.1, 27.15, 90)),
    "JP1": ("PADDLE 8|12", JUMPER3, "", "", "Solder jumper: paddle ribbon pin select", (29.0, 15.113, 270)),
    "JP2": ("BRAIN 8|12", JUMPER3, "", "", "Solder jumper: brain ribbon pin select", (35.3, 15.113, 270)),
    "JP3": ("8 THRU", JUMPER, "", "", "Solder jumper: pin 8 straight through", (32.15, 13.95, 0)),
    "JP4": ("12 THRU", JUMPER, "", "", "Solder jumper: pin 12 straight through", (32.15, 16.5, 0)),
}
NOT_ASSEMBLED = {"JP1", "JP2", "JP3", "JP4"}   # bare copper, no part

# --- Nets ---------------------------------------------------------------------------
NETS = {
    # logic side (Nano ground)
    "GND":    [("J1", "14"), ("J2", "12"), ("J5", "1"), ("K1", "2"), ("U2", "3"),
               ("R2", "1"), ("PS1", "1"), ("C1", "2")],
    "VIN":    [("J1", "15"), ("J5", "2")],
    "+3V3":   [("J1", "2"), ("R7", "1"), ("PS1", "2"), ("C1", "1")],
    "STOP":   [("J2", "8"), ("R1", "1"), ("R2", "2")],        # D5: HIGH opens K1
    "K1_A":   [("R1", "2"), ("K1", "1")],
    "SENSE":  [("J2", "3"), ("R7", "2"), ("U2", "4")],        # D10: LOW = paddle on (V2.1 "In")
    # machine side (isolated, referenced to ribbon COMMON)
    "ISO5V":  [("PS1", "4"), ("R3", "2"), ("R4", "1"), ("C2", "1")],
    "COMMON": [("PS1", "3"), ("R4", "2"), ("C2", "2"), ("J3", "6"), ("J4", "6")],
    "U2_A":   [("R3", "1"), ("R6", "1"), ("U2", "1")],
    "U2_K":   [("U2", "2"), ("R6", "2"), ("D1", "2")],
    "PADDLE": [("D1", "1"), ("K1", "4"), ("JP1", "2")],
    "BRAIN":  [("K1", "3"), ("JP2", "2")],
    "RIB8_P":  [("J3", "8"), ("JP1", "1"), ("JP3", "1")],
    "RIB8_B":  [("J4", "8"), ("JP2", "1"), ("JP3", "2")],
    "RIB12_P": [("J3", "12"), ("JP1", "3"), ("JP4", "1")],
    "RIB12_B": [("J4", "12"), ("JP2", "3"), ("JP4", "2")],
}
for n in (1, 2, 3, 4, 5, 7, 9, 10, 11, 13, 14, 15, 16):
    NETS[f"RIB{n}"] = [("J3", str(n)), ("J4", str(n))]

LOGIC_NETS = {"GND", "VIN", "+3V3", "STOP", "K1_A", "SENSE"}

TRACK_W = {}   # 0.25 mm everywhere: fits between 2.54 mm header pads; <0.5 A on every net
DEFAULT_TRACK_W = 0.25
CLEARANCE = 0.2
VIA_D, VIA_DRILL = 0.6, 0.3
EDGE_CLEARANCE = 0.5


def domain(x, y, layer):
    """'L' logic, 'M' machine, None = isolation gap. layer 'F' or 'B'."""
    logic = ((x < 20.4 and y < 25.6) or (x < 23.3 and y >= 25.6)
             or (x > 30.9 and y > 35.6)
             or (layer == "B" and y > 39.0))
    if logic:
        return "L"
    machine = ((x > 21.6 and y < 24.4) or (x > 26.6 and y >= 24.4))
    if machine:
        if x > 29.7 and y > 35.1:
            return None
        if layer == "B" and y > 37.9:
            return None
        return "M"
    return None
