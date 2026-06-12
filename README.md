# Globo

**Wake up in another country.** Every day the alarm fades in a random curated
radio stream from somewhere else in the world, as if you're on holiday.

Internet radio alarm clock for the **LilyGo T-Display-S3** (ESP32-S3, 8MB
PSRAM) with a MAX98357 I2S amplifier. This is the merge of three earlier
prototypes — [globo-eink](https://github.com/tychografie/globo-eink) (the
alarm concept), the original globo-lcd (the visual identity), and tPod (the
streaming engine). Full comparison and merge notes in [INVENTORY.md](INVENTORY.md).

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
  in NVS and tried strongest-first on boot.
- **iPod-style power UX**: 30s screen timeout with soft backlight fade;
  rendering pauses entirely while the screen is dark.

## Hardware

- LilyGo T-Display-S3, non-touch (ESP32-S3R8, 170×320 ST7789, 8-bit parallel)
- MAX98357 I2S Class D amplifier breakout (3W)
- 3525 4Ω 3W speaker
- EC11 / KY-040 rotary encoder
- JST 1.25mm 2-pin 3.7V 1200mAh Li-Po battery
- Panel-mount USB-C for power + flashing, wired to the onboard USB-C pads

### Pin map

The amp lives on the P1 header (USB side), the encoder on the opposite P2
header next to the 3V pin — the far end from the USB-C connector.

| Signal | Pin | Notes |
|---|---|---|
| I2S BCLK / LRC / DOUT | 44 / 43 / 18 | UART pins, but serial runs over USB-CDC |
| Encoder A / B / SW | 1 / 2 / 3 | P2 header top; module pull-ups or INPUT_PULLUP |
| Buttons | 0 / 14 | onboard |
| Battery ADC | 4 | internal 100k/100k divider |
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
  - **NETWORK** — SSID, IP, signal strength card
  - **BATTERY** — voltage, charge, power source card
  - **WIFI RESET** — No/Yes confirmation, then forgets all networks and restarts
  - **BACK**
- **Hold encoder at boot**: wipe saved WiFi credentials
- Every settings screen times out back to the radio on its own.

Volume, alarm time and armed state persist across reboots via NVS.

## Build

Requires `arduino-cli` with the `esp32:esp32` core (3.x) and the `TFT_eSPI`,
`ESP32-audioI2S` (schreibfaul1, 3.4.x) and `WiFiManager` libraries.

Copy `User_Setup.h` from this repo to
`~/Documents/Arduino/libraries/TFT_eSPI/User_Setup.h` (T-Display-S3 8-bit
parallel config).

The T-Display-S3 board definition sets `psram_type=opi` but not
`-DBOARD_HAS_PSRAM`, so PSRAM fails to initialize without an override:

```bash
arduino-cli compile \
  --fqbn "esp32:esp32:lilygo_t_display_s3" \
  --build-property "build.defines=-DBOARD_HAS_PSRAM" \
  globo_lcd/

arduino-cli upload \
  --fqbn esp32:esp32:lilygo_t_display_s3 \
  --port /dev/cu.usbmodem1101 \
  globo_lcd/
```

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
