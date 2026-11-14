# shotStopper firmware

Brew-by-weight for an espresso machine and a Bluetooth scale (Acaia Lunar,
Pyxis, BooKoo, Felicita). Written for the Arduino Nano ESP32.

## Settings (top of `shotStopper.ino`)

| Setting | Use |
|---------|-----|
| `MOMENTARY` | `true` for momentary brew buttons (GS3 AV, Silvia Pro); `false` for paddles (Linea Mini/Micra) |
| `PADDLE_INTERLOCK` | `true` with the Micra Interlock board (`hardware/shotstopper-micra-interlock`): Linea Mini-style stop with the paddle left on. Needs `MOMENTARY false` |
| `AUTOTARE` | Tare when the shot starts |
| `TIMER_ONLY` | Only start and stop the scale timer; no brew-by-weight |
| `MAX_SHOT_DURATION_S` | Safety cap for a weighed shot (default 50 s) |
| `INTERLOCK_MAX_BREW_S` | Interlock mode: stops any brew after the paddle has been on this long, with or without a scale (default 90 s, 0 = off) |
| `STOP_ON_SCALE_LOSS` | Interlock mode: stop a weighed shot if the scale is lost (default `true`); `false` leaves it to the paddle |
| `OTA_ENABLED` | Over-the-air updates over WiFi (see below) |

The goal weight is set over Bluetooth (below) and is kept across restarts.

## Building with PlatformIO

`platformio.ini` at the repo root builds this sketch together with the
library straight from the repo, so library edits are picked up without
installing anything. Install PlatformIO Core (`pipx install platformio`, or
the VS Code extension), then from the repo root:

| Command | Does |
|---------|------|
| `pio run` | Build the `micra` environment (Micra Interlock board) |
| `pio run -t upload` | Build and flash over USB |
| `pio device monitor` | Serial log at 115200 |
| `pio run -e micra_ota -t upload` | Flash the WiFi OTA build over USB (see OTA below) |
| `pio run -e micra_wifi -t upload` | Flash the WiFi OTA build over WiFi |
| `pio run -e v21` | Build for the original V2.1 board, using the settings in the sketch |

The `micra` environments set `MOMENTARY`, `PADDLE_INTERLOCK` and
`OTA_ENABLED` from `build_flags`. Those settings are wrapped in `#ifndef` in
the sketch so this works, and the Arduino IDE uses the values written in the
sketch.

The Arduino IDE works too: install this library and ArduinoBLE, open the
sketch, and select the Arduino Nano ESP32 board.

## Bluetooth values

The board advertises as **shotStopper** (service `0FFE`). Any BLE app works,
for example LightBlue or nRF Connect.

| Characteristic | Access | Value |
|----------------|--------|-------|
| `FF11` | read / write | Goal weight, grams (1 byte) |
| `FF12` | read / notify | Flow rate during a shot, g/s (float) |
| `FF13` | read / notify | Scale battery %, 255 = unknown |
| `FF14` | read / write | Write `1` to open a 10-minute OTA window, `0` to close it |

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

## Over-the-air updates (WiFi)

1. Copy `secrets.example.h` to `secrets.h` and fill in your WiFi network, an
   upload password, and a hostname. `secrets.h` is ignored by git.
2. Upload once over USB with OTA turned on:
   - **PlatformIO:** `pio run -e micra_ota -t upload`.
   - **Arduino IDE:** set `OTA_ENABLED true` in the sketch and upload.
3. Later uploads go over WiFi to `shotstopper.local` (or the IP address
   printed on the serial port), while the OTA window is open:
   - **PlatformIO:** `export SHOTSTOPPER_OTA_PASSWORD=<upload password>`, then
     `pio run -e micra_wifi -t upload`. If you changed `OTA_HOSTNAME`, change
     `upload_port` in `platformio.ini` to match.
   - **Arduino IDE:** Tools → Port → the "shotstopper" network port. Enter the
     upload password when asked.

WiFi and Bluetooth share the ESP32's radio, so WiFi is on **only while the
board is idle**: no shot running, paddle off, interlock closed. It is on for
the first 5 minutes after boot, or for 10 minutes after `1` is written to
`FF14`. It switches off as soon as a shot starts. While the board is being
flashed, the paddle works by hand, and the board restarts with the interlock
closed.

The Nano ESP32's default partition scheme has the two 3 MB app slots OTA
needs. The OTA build uses about 0.9 MB, the build without OTA about 0.5 MB.
