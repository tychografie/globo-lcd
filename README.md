# Globo

**Wake up in another country.** Every day the alarm fades in a random curated
radio stream from somewhere else in the world, as if you're on holiday.

Internet radio alarm clock for the **LilyGo T-Display-S3** (ESP32-S3, 8MB
PSRAM) with a MAX98357 I2S amplifier. This is the merge of three earlier
prototypes — [globo-eink](https://github.com/tychografie/globo-eink) (the
alarm concept), the original globo-lcd (the visual identity), and tPod (the
streaming engine). Parts list in [INVENTORY.md](INVENTORY.md).

## What it does

- **Daily alarm**: at your wake time it picks a random station from the
  curated world list and fades it in over 20 seconds. Silent streams are
  detected and skipped automatically so the alarm can never be dead air.
- **Flowing gradient**: 5 huge color blobs drift like a lava lamp with sharp
  metaball falloff, film grain and vignette. Every station hop rerolls the
  palette and scrambles the blobs.
- **Typography**: station name + city each pick a random face (Roboto
  Condensed, Big Shoulders Inline, Savate × Light/Medium/Black) and stretch
  horizontally to fill the screen — a fake variable-width axis.
- **WiFi onboarding**: no credentials in code. First boot opens a captive
  portal with a join-QR on screen; every network you ever join is remembered
  in NVS and tried strongest-first on boot. When offline, a background
  watcher keeps scanning and joins a remembered network the moment it
  appears — made for the phone-hotspot dance while travelling.
- **iPod-style power UX**: 30s screen timeout with soft backlight fade;
  rendering pauses entirely while the screen is dark.

## Hardware

- LilyGo T-Display-S3, non-touch (ESP32-S3R8, 170×320 ST7789, 8-bit parallel)
- MAX98357 I2S Class D amplifier breakout (3W)
- 3525 4Ω 3W speaker
- EC11 / KY-040 rotary encoder
- 14500 Li-Ion cell, 3.7V 1600mAh (XH2.54 plug)
- TP4057 USB-C charging board (1A, with cell protection) between the cell and
  the board's 1.25mm battery connector — its USB-C is the external charge
  port; it carries no data, so flashing happens on the onboard USB-C
- SS12F15 SPDT slide switch as hard power switch, in the plus lead between
  TP4057 OUT and the battery connector. Off still charges the cell via the
  external USB-C; the onboard USB-C always powers the board regardless

### Pin map

The encoder and the amp share the header on the 3V3 side: encoder on 1/2/3
next to the 3V3 pin (the far end from the USB-C), amp on 10/11/12 below it.

| Signal | Pin | Notes |
|---|---|---|
| I2S BCLK / LRC / DOUT | 11 / 12 / 10 | DOUT drives the MAX98357 DIN |
| Encoder A / B / SW | 1 / 2 / 3 | P2 header top; module pull-ups or INPUT_PULLUP. GPIO 3 is a strapping pin, but its JTAG strap is ignored with default efuses |
| Buttons | 0 / 14 | onboard |
| Battery ADC | 4 | internal 100k/100k divider |
| Backlight PWM | 38 | internal (TFT_BL) |
| LCD power enable | 15 | internal |
| MAX98357 SD | tie to VIN | always enabled |

## Controls

Everything runs on the EC11 — the onboard buttons sit behind the enclosure
and are unused.

- **Rotate**: volume (big stretched percentage overlay)
- **Short press**: shuffle to a random station — the "take me somewhere else" button
- **Long press (1.2s)**: settings menu (ported from globo-eink), one big
  stretched word per item — rotate to browse, press to open, long-press to
  go back to the radio:
  - **ALARM** — rotate sets the wake time in 5-minute steps, press arms/disarms
  - **STATIONS** — rotate browses the list sequentially (visuals flip
    instantly, the stream connects 700ms after you stop turning), press returns
  - **NETWORK** — connected: QR + address of the web remote; offline: live
    search status (searching / joining / countdown to the next scan), click
    searches immediately, long-press goes back
  - **BATTERY** — voltage, charge, power source card
  - **WIFI RESET** — No/Yes confirmation, then forgets all networks and restarts
  - **BACK**
- **Hold encoder at boot**: wipe saved WiFi credentials
- Every settings screen times out back to the radio on its own.

Volume, alarm time and armed state persist across reboots via NVS.

## On the road (phone hotspot)

Globo re-joins remembered networks **whenever they appear**, not just at
boot — so the order of operations no longer matters:

1. Power up Globo. If the hotspot isn't broadcasting yet it simply boots
   offline.
2. Turn on the phone's hotspot and stay on the hotspot settings screen —
   phone hotspots only broadcast while that screen is open or a device is
   already attached.
3. Within ~15 s Globo finds it, joins, and the radio resumes on its own.

The first time on a new hotspot the setup portal appears. Hotspot quirks
the flow accounts for:

- **iPhone**: enable **Maximize Compatibility** — Globo is 2.4 GHz-only and
  never sees a 5 GHz-only hotspot.
- Typed credentials are kept even when that first join fails. (Your phone
  being parked on the Globo-Setup portal pauses its own hotspot, so the
  first attempt tends to fail through no fault of yours.) The moment the
  phone lets go and the hotspot resumes, Globo connects by itself.
- The **NETWORK** menu screen shows what the watcher is doing in real time;
  click to search immediately instead of waiting for the next pass.

## Build

Requires `arduino-cli` with the `esp32:esp32` core (3.x) and the `TFT_eSPI`,
`ESP32-audioI2S` (schreibfaul1, 3.4.x) and `WiFiManager` libraries.

Copy `User_Setup.h` from this repo to
`~/Documents/Arduino/libraries/TFT_eSPI/User_Setup.h` (T-Display-S3 8-bit
parallel config).

The T-Display-S3 board definition sets `psram_type=opi` but not
`-DBOARD_HAS_PSRAM`, so PSRAM fails to initialize without an override.

It also hardcodes `cdc_on_boot=1` — unlike the generic `esp32s3` board, this
one defaults USB CDC On Boot to *Enabled*, which is the WiFi-deafening mode
the sketch header warns about. `CDCOnBoot=default` (Disabled) must be pinned
on the FQBN for both compile and upload, or inbound connections — web remote,
mDNS, `/api/ota` — stop answering:

```bash
arduino-cli compile \
  --fqbn "esp32:esp32:lilygo_t_display_s3:CDCOnBoot=default" \
  --build-property "build.defines=-DBOARD_HAS_PSRAM" \
  globo_lcd/

arduino-cli upload \
  --fqbn "esp32:esp32:lilygo_t_display_s3:CDCOnBoot=default" \
  --port /dev/cu.usbmodem1101 \
  globo_lcd/
```

With CDC off the console is UART0, not USB — live telemetry comes from the
UDP heartbeat (port 9909) and `/api/status` instead.

### OTA (flash over WiFi)

Once a build with `/api/ota` is on the device, the enclosure never has to
open again for firmware:

```bash
globo_lcd/tools/ota.sh              # builds + uploads to globo.local
globo_lcd/tools/ota.sh 192.168.1.7  # or by IP if mDNS is being mDNS
```

Audio stops during the write (~30s), then the device reboots into the new
firmware. The 16MB scheme has two 3MB app slots, so a failed or aborted
upload leaves the running firmware untouched — but a build that *boots and
then crashes* still needs the USB cable, so try new firmware over USB when
the change is risky.

## Stations

34 curated stations across 6 continents, merged from all three prototypes.
How to find and test new ones: see `../globo-eink/RADIO_STATIONS.md` and
`../globo-eink/extract_stream.py` (M3U8 → direct stream URL). The S3 has
PSRAM, so HTTPS and AAC streams are fine here (unlike the e-ink build).

## Audio quality

The hardware noise fixes from `../globo-eink/AUDIO_NOISE_FIX.md` apply
verbatim to this build: decoupling capacitors on the amp's VIN, star
grounding, twisted I2S pairs. In software the amp gets a warm tone curve
(+5dB bass / -4dB treble) because the small speaker can't move air at the
low end.

## Fonts

Bitmap fonts are baked from variable TrueType sources with Adafruit GFX
`fontconvert`:

1. Instance a variable font at a specific weight with `fonttools.varLib.instancer`
2. `fontconvert <instanced.ttf> <pointSize> <firstChar> <lastChar>` → `.h`
3. Drop it next to the sketch and `#include` it

Sources (all OFL): [Roboto Condensed](https://fonts.google.com/specimen/Roboto+Condensed),
[Big Shoulders Inline](https://fonts.google.com/specimen/Big+Shoulders+Inline),
[Savate](https://fonts.google.com/specimen/Savate).

## Still on the wishlist (from globo-eink)

- Light sleep between idle and alarm (~50× battery improvement on the e-ink
  build; needs an LCD-era power plan, see `../globo-eink/BATTERY_POWER_PLAN.md`)
- Web UI for alarm/station management
- Battery-state power policy (CPU scaling, WiFi power modes)
