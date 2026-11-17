# shotStopper firmware

Brew-by-weight for an espresso machine and a Bluetooth scale (Acaia Lunar,
Pyxis, BooKoo, Felicita). Runs on the ShotStopper boards: the ESP32-C3 ones
(V3.1, Micra Interlock) and the Arduino Nano ESP32 (V2.1).

## Settings (top of `shotStopper.ino`)

| Setting | Use |
|---------|-----|
| `MOMENTARY` | `true` for momentary brew buttons (GS3 AV, Silvia Pro); `false` for paddles (Linea Mini/Micra) |
| `PADDLE_INTERLOCK` | `true` with the Micra Interlock board (`hardware/shotstopper-micra-interlock`): Linea Mini-style stop with the paddle left on, via GPIO5. Needs `MOMENTARY false` |
| `AUTOTARE` | Tare when the shot starts |
| `TIMER_ONLY` | Only start and stop the scale timer; no brew-by-weight |
| `MAX_SHOT_DURATION_S` | Safety cap for a weighed shot (default 50 s) |
| `INTERLOCK_MAX_BREW_S` | Interlock mode: stops any brew after the paddle has been on this long, with or without a scale (default 90 s, 0 = off) |
| `STOP_ON_SCALE_LOSS` | Interlock mode: stop a weighed shot if the scale is lost (default `true`); `false` leaves it to the paddle |
| `OTA_ENABLED` | WiFi: the event log page, and updates through the recovery firmware (see below) |
| `EVENT_LOG` | Keep an event log in flash, read over WiFi (see below). On when `OTA_ENABLED` is |

The goal weight is set over Bluetooth (below) and is kept across restarts.

## Building with PlatformIO

`platformio.ini` at the repo root builds this sketch together with the
library straight from the repo, so library edits are picked up without
installing anything. Install PlatformIO Core (`pipx install platformio`, or
the VS Code extension), then from the repo root:

| Command | Does |
|---------|------|
| `pio run` | Build the `micra` environment (Micra Interlock board, ESP32-C3) |
| `pio run -t upload` | Build and flash over USB |
| `pio device monitor` | Serial log at 115200 |
| `pio run -d recovery -t upload` | Flash the recovery firmware over USB (see Updates below) |
| `pio run -e micra_ota -t upload` | Flash the WiFi build of the main firmware over USB |
| `pio run -e micra_wifi -t upload` | Send the same build over WiFi to a board that is in recovery |
| `tools/ota.sh` | Put the board in recovery, then upload over WiFi |
| `pio run -e v31` | Build for the original V3.1 board (ESP32-C3), using the settings in the sketch |
| `pio run -e v21` | Build for the original V2.1 board (Nano ESP32), using the settings in the sketch |

The `micra` environments set `MOMENTARY`, `PADDLE_INTERLOCK` and
`OTA_ENABLED` from `build_flags`. Those settings are wrapped in `#ifndef` in
the sketch so this works, and the Arduino IDE uses the values written in the
sketch.

On an Apple Silicon Mac, the ESP32-C3 compiler for this core is an Intel
binary: install Rosetta once (`softwareupdate --install-rosetta`).

The paddle-interlock logic has a host test: `tests/interlock/run.sh` builds the
real sketch and library for this computer against stub Arduino and Bluetooth
headers and runs scripted scenarios (normal shots, early paddle-off, scale
loss, glitches on the sense line, resets, stalled Bluetooth writes, swapped
plugs). It needs only a C++17 compiler.

PlatformIO's `espressif32@6.10.0` builds against Arduino-ESP32 core 2.0.17.
The seller's shotStopper manual calls for core 3.3.3 in the Arduino IDE. The
sketch compiles against both (the watchdog set-up is the only core-specific
code), but only the 2.0.17 build has been compiled here and neither has run on
the Micra Interlock board yet.

The Arduino IDE works too: install this library and ArduinoBLE, open the
sketch, and select an ESP32-C3 board with "USB CDC On Boot" enabled (or the
Arduino Nano ESP32 for V2.1).

## Bluetooth values

The board advertises as **shotStopper** (service `0FFE`). Any BLE app works,
for example LightBlue or nRF Connect.

| Characteristic | Access | Value |
|----------------|--------|-------|
| `FF11` | read / write | Goal weight, grams (1 byte) |
| `FF12` | read / notify | Flow rate during a shot, g/s (float) |
| `FF13` | read / notify | Scale battery %, 255 = unknown |
| `FF14` | read / write | Write `1` to turn WiFi on for 10 minutes, `0` to turn it off, `2` to restart into the recovery firmware once idle (see Updates) |
| `FF15` | read / notify | Status bits (1 byte, below). Read-only |
| `FF16` | read / write | Mode: `0` brew by weight, `1` manual. Kept across restarts |
| `FF17` | read / notify | Shot time, seconds (float): counts during a shot, then holds its length |
| `FF18` | read / notify | Final weight of the last shot, grams (float) |

### Status (`FF15`)

| Bit | Value | Meaning |
|-----|-------|---------|
| 0 | `0x01` | Paddle (or brew switch) is on |
| 1 | `0x02` | Held: the paddle is on but the brew has been stopped. Turn the paddle off |
| 2 | `0x04` | A shot is being timed |
| 3 | `0x08` | The shot is being weighed and will stop at the goal weight |
| 4 | `0x10` | Scale connected |
| 5 | `0x20` | Interlock disabled until restart (the J3 and J4 plugs look swapped) |
| 6 | `0x40` | Manual mode is in effect |

Nothing in this value can be written, and none of the Bluetooth values can
start or stop a brew.

### Manual mode (`FF16`)

Manual mode switches brew-by-weight off. The paddle alone starts and stops
the brew: the board never stops a shot, by weight, by time or by the
90-second limit. The scale is still tared and the shot is still timed and
reported. The idle LED is blue instead of green.

A change of mode is taken up only while idle (paddle off, no stop held). A
write during a shot or a held stop applies from the next shot, so it cannot
start or stop a brew. `FF16` reads back what was requested; bit 6 of the
status shows what is in effect.

### Final weight (`FF18`)

Set 3 seconds after the shot ends, once the drips have settled. It is NaN
from the start of a shot until then, and stays NaN if the scale was not
zeroed for the shot or has stopped sending weight.

## Flow rate

Acaia scales don't send flow rate, so it is calculated from the weight
readings: it is the slope of the line fitted to the last 10 readings, the
same fit used to predict the end of the shot. The value is printed with each
reading, published on `FF12`, and included in the end-of-shot summary as
peak flow. BooKoo scales report their own flow, which is printed next to the
calculated value.

## Sanity checks

| Check | What happens |
|-------|--------------|
| Tare | Must read within 0.5 g of zero within 750 ms. Re-sent up to twice, otherwise stop-by-weight is off for that shot |
| Scale timer | Must be seen counting from zero within 2 s. Reset + start re-sent once. Display only: brew-by-weight uses the board's own clock |
| Weight stream at shot start | No reading in the last 1 s: the shot is left to the paddle (LED magenta) |
| Scale units | Scale set to ounces: shots are left to the paddle |
| Implausible readings | A jump of more than 5 g, a "flow" above 12 g/s, or the weight dropping more than 1 g is ignored. The stop offset isn't learned from that shot |
| Bad messages | Messages with a wrong checksum are dropped |
| Scale battery | Printed when it changes. At 15 % or below, the idle LED is cyan instead of green |
| Stream stops for 5 s | Treated as a disconnect. Interlock mode stops a weighed shot (`STOP_ON_SCALE_LOSS`); otherwise the paddle keeps control |

The scale timer and battery depend on the scale sending them. The library
asks for them, but if a scale never sends timer reports, the timer is marked
"unverified" rather than retried.

## Updates (WiFi and the recovery firmware)

The Micra Interlock board sits inside the machine, where its USB port cannot
be reached. So it carries two firmwares:

- **The recovery firmware** (`recovery/`). Small and fixed: flashed once over
  USB, and never written again. All it does is accept a new main firmware over
  WiFi and show the event log. Nothing that runs on the board can overwrite it.
- **The main firmware** (this sketch, the `micra_ota` build). It cannot replace
  itself; to update it, the board restarts into recovery.

While the board is in recovery the machine works **by hand**: the interlock
stays closed, and there is no brew-by-weight. The LED blinks yellow.

### First set-up, over USB, before the board goes in the machine

1. Copy `secrets.example.h` to `secrets.h` and fill in your WiFi network (it
   must be 2.4 GHz), an upload password of at least 8 characters, and a
   hostname. `secrets.h` is ignored by git, and both firmwares use it.
2. Flash the recovery firmware: `pio run -d recovery -t upload`.
3. Load the main firmware through it, which also tests the path you will
   depend on later: `export SHOTSTOPPER_OTA_PASSWORD=<upload password>`, then
   `pio run -e micra_wifi -t upload`. (Over USB also works:
   `pio run -e micra_ota -t upload`.)

Do not flash the plain `micra` build on a board that is going in the machine:
it has no WiFi and uses a different flash layout, so there is no way back
without USB.

### Updating

    export SHOTSTOPPER_OTA_PASSWORD=<upload password>
    tools/ota.sh

The script asks the board to restart into recovery, waits for it, and uploads.
The board must be idle (paddle off) with its WiFi on: for 5 minutes after
power-on, or for 10 minutes after `1` is written to `FF14`. By hand, the same
two steps are:

1. Put the board in recovery, either way:
   - write `2` to `FF14` over Bluetooth, or
   - `curl -u ota:<upload password> -X POST http://shotstopper.local/recovery`
2. `pio run -e micra_wifi -t upload`

After the upload the board starts the new firmware. **Leave the machine on for
a minute**: see probation, below. If nothing is uploaded, recovery starts the
main firmware again after 10 minutes.

### How the board ends up in recovery

| Cause | What happens next |
|---|---|
| You asked: `FF14` = 2, or the web request | Waits 10 minutes for an upload, then starts the main firmware again |
| **Probation failed.** A main firmware that recovery has just started must run 30 s (`OTA_CONFIRM_S`) without resetting. If it resets first (crash, watchdog, or the power switched off), the bootloader refuses to start it again | Recovery tries it twice more, 30 s apart. After three failures it waits for an upload |
| **Three crashes in a row.** The main firmware crashed or hit the watchdog three times (`CRASH_LIMIT`), each within 2 minutes of starting | Waits 10 minutes for an upload, then tries the main firmware again |
| **Power-switch signal.** The machine was switched on five times in a row (`POWER_SIGNAL_COUNT`), each time for less than 10 s. This needs no radio and no working Bluetooth: it is the way in when nothing else answers | Waits 10 minutes for an upload, then starts the main firmware again |
| There is no main firmware, or it is damaged | Waits for an upload |

The main firmware only goes to recovery while it is idle. If it is holding a
stop, or the paddle is on, it waits: recovery does not run the interlock.

### In recovery

- `http://shotstopper.local/` says why the board is there and what it will do.
- `http://shotstopper.local/log` shows the event log, so you can see what the
  main firmware was doing before it failed.
- `curl -u ota:<upload password> -X POST http://shotstopper.local/boot-main`
  starts the main firmware now.
- **If it cannot join your WiFi within 25 s** it opens its own network,
  `shotstopper-recovery`, with the upload password, at `192.168.4.1`. Join it
  and upload with `pio run -e micra_wifi -t upload --upload-port 192.168.4.1`.
  This is the way back after a router or WiFi password change. With nobody
  connected, it tries your network again every 3 minutes.

### WiFi in the main firmware

WiFi and Bluetooth share the ESP32's radio, so WiFi is on **only while the
board is idle**: no shot running, paddle off, interlock closed. It is on for
the first 5 minutes after boot, or for 10 minutes after `1` is written to
`FF14`. It switches off as soon as a shot starts.

### Flash layout

`partitions_recovery.csv`, for the board's 4 MB flash: 1 MB for the recovery
firmware (it uses 0.83 MB), 2.8 MB for the main firmware (1.0 MB today), 128 KB
for the event log.

None of this has run on hardware yet. Before the board goes in the machine,
check on the bench that: an upload through recovery works; a main firmware
that crashes at start (for example one built with a deliberate `abort()` in
`setup()`) ends up back in recovery; the power-switch signal works; and
recovery opens its own network when your WiFi is out of reach.

## Event log

Builds with `OTA_ENABLED` keep a log of what happened to the scale link and to
each shot, for working out problems after the fact. It is stored in the
board's flash, so it survives power cycles, and it is read over WiFi:

    http://shotstopper.local/           the newest 2000 events, as text
    http://shotstopper.local/?n=200     fewer
    http://shotstopper.local/log.bin    every stored record, raw (16 bytes each)

The page is there whenever WiFi is on: for 5 minutes after power-on, or for
10 minutes after `1` is written to `FF14`. It is read-only and has no
password, so anyone on your network can read it; it holds no secrets. The
recovery firmware shows the same log at `/log`.

A shot looks like this:

    2026-11-17 08:14:03.2  paddle on
    2026-11-17 08:14:03.2  shot start: weighed, goal 36.0 g, offset 1.5 g
    2026-11-17 08:14:12.9    no weight for 2564 ms at 9 s into the shot
    2026-11-17 08:14:23.5  shot end by weight after 20.3 s
    2026-11-17 08:14:23.5    shot link: 178 weight packets, longest gap 2564 ms, 1 gaps over 700 ms
    2026-11-17 08:14:23.5    shot quality: tare confirmed, 0 bad packets, peak flow 2.42 g/s
    2026-11-17 08:14:23.5  brew stopped: interlock open
    2026-11-17 08:14:26.5    final weight 35.70 g

What is logged:

- **Scale link:** each connection (how long it took, signal strength), each
  failed connection or set-up step, each drop (link lost, weight stream
  stopped, or a command that failed) with whether a shot was running, and how
  long each connection lasted with its count of corrupted packets.
- **Shots:** start (weighed or not, and why not), tare retries and failures,
  gaps in the weight stream, how the shot ended, packet count and longest
  gap, final weight, and the offset learned.
- **Board:** each start with its cause (power on, watchdog, crash), paddle and
  interlock changes, mode and goal changes, WiFi, firmware updates and
  rollbacks, and any pass of the main loop that took over a second.

Things to know:

- **Dates need the network.** The board has no clock. Each time WiFi connects
  it asks `pool.ntp.org` for the time, and every event since that power-on
  gets a date. Events from a power-on where WiFi never connected show the time
  since the board started instead (`+ 0h03m12.4s`). Dates are UTC unless
  `LOG_TZ` is set in `secrets.h`.
- **Nothing is written to flash during a shot.** Events wait in memory (64 at
  most) and are written once the paddle is off. That memory survives a crash
  or watchdog reset, but not a power cut.
- **Size:** the log uses the 128 KB `spiffs` partition directly, as a ring of
  about 8000 events; the oldest 256 are dropped each time it wraps. At roughly
  15 events a shot that is several hundred shots.
- A scale that fails to connect over and over is logged for the first ten
  attempts, then every 50th.

`tests/eventlog/run.sh` tests the log's storage on this computer against a
simulated flash chip, including power cuts in the middle of a write. The
logging itself has not yet run on hardware.

