# CLAUDE.md

Guidance for Claude Code when working in this repository.

## What this is

A fork of [tatemazer/AcaiaArduinoBLE](https://github.com/tatemazer/AcaiaArduinoBLE)
(remote `upstream`), an Arduino library that talks to Acaia, BooKoo and
Felicita espresso scales over Bluetooth LE, plus the `shotStopper` sketch that
stops a shot at a target weight.

The fork (branch `reliability-fixes`) adds three things:

1. **Library reliability fixes** for the Acaia Lunar (2021).
2. **A custom board**, the ShotStopper Micra Interlock, for brew-by-weight on a
   La Marzocco Linea Micra. It sits in series with the brew paddle.
3. **Firmware for that board**: paddle interlock, a fixed recovery firmware for
   WiFi updates, and a flash event log.

Nothing in the interlock, recovery or logging firmware has run on hardware yet.
It is compiled and host-tested only. Say so when reporting status, and do not
describe hardware behaviour as verified.

## Layout

| Path | What |
|---|---|
| `AcaiaArduinoBLE.{h,cpp}` | The scale library. **CRLF line endings: preserve them.** |
| `examples/shotStopper/shotStopper.ino` | The main firmware (one sketch, several boards, chosen by build flags) |
| `examples/shotStopper/eventlog.h` | Event log: flash ring buffer, event codes, text decoder. No hardware calls |
| `examples/shotStopper/README.md` | User documentation for the firmware: settings, Bluetooth values, updates, log |
| `examples/shotStopper/secrets.example.h` | Template for `secrets.h` (WiFi, upload password). `secrets.h` is git-ignored |
| `recovery/` | The recovery firmware: its own PlatformIO project (`recovery/src/main.cpp`) |
| `partitions_recovery.csv` | Flash layout shared by the recovery and main firmware |
| `platformio.ini` | Builds the main firmware; `src_dir` is `examples/shotStopper` |
| `tools/ota.sh` | Puts the board in recovery, then uploads over WiFi |
| `tests/interlock/` | Host test: the real sketch and library against stub Arduino/BLE headers |
| `tests/eventlog/` | Host test: the event log against a simulated flash chip |
| `hardware/shotstopper-micra-interlock/` | The board: generated KiCad files, fabrication outputs, order notes |

## Build and test

PlatformIO Core is required (`pipx install platformio` or `uv tool install platformio`).
On Apple Silicon the ESP32-C3 toolchain needs Rosetta.

```sh
pio run                      # main firmware, default env `micra` (no WiFi)
pio run -e micra_ota         # main firmware with WiFi, log and recovery routes (needs secrets.h)
pio run -e v31               # original ShotStopper V3.1 board
pio run -e v21               # original V2.1 board (Arduino Nano ESP32)
pio run -d recovery          # recovery firmware (needs secrets.h)
tests/interlock/run.sh       # 30 scripted scenarios; `run.sh NAME -v` for one with serial output
tests/eventlog/run.sh        # event log storage, including power cuts mid-write
```

After any firmware change, build **all four** main environments and the
recovery project, and run both host tests. `micra_ota` and `recovery` need
`examples/shotStopper/secrets.h`; if it is absent, copy `secrets.example.h` to
it for the build and remove the copy afterwards. Never commit `secrets.h`.

The interlock scenarios should give the same summary lines before and after a
change that is not meant to alter interlock behaviour. Compare them.

Builds use Arduino-ESP32 core 2.0.17 (`espressif32@6.10.0`). The compiler is
GCC 8: no `#elifdef`.

## Rules that must hold

These are the owner's requirements. Do not weaken them without being asked.

- **Fail-safe paddle.** Paddle off always stops the brew. Paddle on always
  starts it. The machine works by hand when the ESP32 is off, reset, crashed or
  in recovery. The interlock relay (K1) is normally closed, and `STOP` low
  means "do nothing".
- **No remote control.** No Bluetooth or network write may start or stop a
  brew, or release a held stop. Reporting is fine. A restart is allowed only
  while idle (paddle off, no stop held, no shot).
- **The stop comes first.** When a shot ends, open the interlock before any
  Bluetooth call or print: those can block.
- **Nothing slow in a shot.** No flash writes and no WiFi while a shot runs.
  `logEvent()` only queues to memory; `log_task()` writes flash while idle.
- **The paddle is sampled from a 5 ms timer** (`paddleTick`), not from
  `loop()`. Releasing a held stop must not wait for `loop()`.
- **The recovery firmware is never updated in the field.** It cannot be
  changed over WiFi, and USB is unreachable once the board is installed. Any
  change to `recovery/` needs a bench test before it is relied on. Keep it
  small and keep it from touching the machine (it holds `STOP` low).
- **The board is updated only over WiFi.** Do not remove a route into recovery
  (Bluetooth `FF14` = 2, web `POST /recovery`, three crashes in a row, five
  short power-ons) without replacing it.

## Sketch conventions

- **The first function in the `.ino` must be outside any `#if`.** PlatformIO
  inserts its generated prototypes just before the first function it finds;
  inside a conditional they vanish in the other configuration and every later
  call fails to compile. `logEvent()` is that function today.
- **No default arguments** on functions defined in the `.ino` (they clash with
  the generated prototypes).
- Settings wrapped in `#ifndef` at the top of the sketch can be set from
  `platformio.ini` build flags; the Arduino IDE uses the values in the sketch.
- The host harness includes the sketch as a header. A new function that is
  called before it is defined needs a prototype in `tests/interlock/protos.h`,
  and a new ESP-IDF or Arduino call needs a stub in `tests/interlock/stubs/`.
- Hardware-only code (WiFi, partitions, web server, OTA) goes behind
  `#if OTA_ENABLED`, `#if EVENT_LOG` or `#if RECOVERY`, so the host harness and
  the non-WiFi builds do not see it.
- New log events: add a code and its text in `eventlog.h`. Records are 16
  bytes and the codes are stored in flash, so append to the enum; do not
  renumber.
- Bluetooth service `0FFE`: `FF11` goal weight, `FF12` flow, `FF13` scale
  battery, `FF14` WiFi window / recovery, `FF15` status bits (read-only),
  `FF16` mode, `FF17` shot time, `FF18` final weight. Document any change in
  `examples/shotStopper/README.md`.

## Hardware

`hardware/shotstopper-micra-interlock/design.py` is the source of truth. The
`.kicad_pcb`, schematic, BOM and everything in `fab/` are generated:

```sh
cd hardware/shotstopper-micra-interlock
./make_all.sh /path/to/python3    # a Python with numpy; uses KiCad 10's Python and kicad-cli
```

`make_all.sh` wipes `fab/`. Do not hand-edit generated files. The run must end
with DRC at 0 errors and 0 unconnected. `ORDER_NOTES.md` holds the PCBWay order
settings and assembly notes.

A board revision is on order (PCBWay). Changing `design.py` after that point
means the ordered boards no longer match the repository: say so before doing it.

## Working here

- Commit and push only when asked. Work on `reliability-fixes`; do not push to
  `upstream`.
- Keep documentation in step with the code: `examples/shotStopper/README.md`
  for firmware behaviour, `hardware/.../README.md` for the board.
- Report test and build results as they are, including what could not be
  checked without hardware.
