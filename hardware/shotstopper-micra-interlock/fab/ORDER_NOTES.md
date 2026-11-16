# PCBWay order notes: ShotStopper Micra Interlock

Files to upload, all in `fab/`:

| File | For |
|---|---|
| `shotstopper-micra-interlock-gerbers.zip` | PCB fabrication (Gerbers and drill files) |
| `bom.csv` | Assembly: parts list |
| `centroid.csv` | Assembly: pick-and-place positions |
| `assembly-top.pdf` | Assembly drawing: part references, polarity and latch marks |

## PCB order form

| Field | Value |
|---|---|
| Board size | 51.94 x 41.28 mm, single pieces |
| Layers | 2 |
| Material / thickness | FR-4, 1.6 mm |
| Copper | 1 oz |
| Min track / spacing | 0.15 / 0.20 mm (6 / 8 mil) |
| Min hole | 0.3 mm (vias: 0.6 mm pad, tented) |
| Solder mask | Green (the USB-C pads leave a 0.1 mm mask web: at the limit for other colours) |
| Silkscreen | White |
| Surface finish | ENIG preferred (0.5 mm-pitch USB-C and module pads); lead-free HASL is workable |
| Edge connector, castellated holes, impedance control | None |
| Plated slots | 4 (USB-C shell, 0.6 mm wide) |

## Assembly order form

| Field | Value |
|---|---|
| Sides | Top only |
| Unique parts | 32 BOM lines |
| SMD parts / pads | 37 / 119 |
| Through-hole parts / pins | 4 / 14, plus the USB-C socket's 4 shell tabs |
| BGA / QFN | None. One module (U3) with a ground pad underneath |

## Notes to paste into the order

1. Please add process rails / panelise as needed: USB1 overhangs the top
   board edge by about 1.2 mm and the U3 module overhangs the cut-out.
2. Through-hole parts J3, J4, J7 and PS1: hand solder (no reflow, no wave).
3. USB1: the four shell tabs sit in plated slots with no paste. Please
   hand solder them.
4. Headers J3, J4, J7 fit either way round but are keyed. The latch (solid,
   tall) wall of each header goes on the side marked **LATCH** on the
   silkscreen, next to the thick bar: on the right for J3 and J4 with the USB
   connector at the top left, and toward the board edge for J7. See
   `assembly-top.pdf`.
5. K1 (CPC1117N) and U1 (EL357N) are symmetric 4-pin parts: pin 1 is at the
   round silkscreen dot. U1 is turned 180 degrees relative to K1.
6. D2 (RGB LED): the cathode mark (pin 2 corner) goes at the silkscreen dot.
   Please source by LCSC C409780. SW1 and SW2: LCSC C318884.
7. K1 must be CPC1117N (normally closed). No substitution.
8. Centroid file: origin is the board's top-left corner, Y negative downward
   (KiCad convention), the same origin as the Gerbers. The anchors of U3 and
   USB1 are the centres of their pad fields, not of their bodies.
9. There are no fiducials. Please use pad features for alignment.

## Checks on receipt

- J3, J4, J7: latch wall on the LATCH side.
- K1 and U1: pin 1 at the dot. D2: cathode mark at the dot.
- No solder bridge between the two pins of J7, or between PS1's pins.
- Then the bench checks in README.md, section "Before you order", item 4,
  before the board is connected to the machine.
