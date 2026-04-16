# Globo LCD

Small internet radio prototype for the **LilyGo T-Display-S3** (ESP32-S3, 8MB PSRAM) with a MAX98357 I2S amplifier. LCD variant of [globo-eink](https://github.com/tychografie/globo-eink) — same world-radio-alarm concept but with a flowing, mesmerizing visual interface instead of an e-ink display.

## What it does

Plays internet radio stations from around the world. Every time you switch stations, the entire visual identity rerolls:

- **Flowing gradient**: 5 huge color blobs move around the screen in a lava lamp, pushing against each other with sharp metaball falloff. Each station picks a random palette and scrambles blob positions. Fixed film grain adds an analog/print feel.
- **Typography**: Each line (station name + city) independently picks a random font and weight. Three typeface families (Roboto Condensed, Big Shoulders Inline, Savate) × multiple weights (Light / Medium / Black) baked from their variable font sources. Text is horizontally stretched to fill the screen, giving the look of a variable-width axis.
- **Supersampled anti-aliasing**: 2x2 oversampling with threshold for clean edges without muddy blending.
- **Full-screen PSRAM sprite**: 320×170 flicker-free rendering.

## Hardware

- LilyGo T-Display-S3 (ESP32-S3, 170×320 ST7789, 8-bit parallel, 8MB OPI PSRAM)
- MAX98357 I2S Class D amplifier (3W)
- 3525 8Ω 2W speaker
- Onboard buttons only (GPIO 0 + 14)

### I2S pins (amp side)

| MAX98357 | ESP32-S3 |
|----------|----------|
| BCLK | GPIO 1 |
| LRC | GPIO 2 |
| DIN | GPIO 3 |
| VIN | 3.3V or 5V |
| GND | GND |
| SD | VIN (enables the chip) |

## Controls

- **Left button (boot, GPIO 0)**: previous station — hold for volume down
- **Right button (GPIO 14)**: next station — hold for volume up
- Volume persists across reboots via NVS.

## Build

Requires `arduino-cli` with `esp32:esp32` core (3.3.8+) and `TFT_eSPI`, `ESP32-audioI2S-master`, `Preferences` libraries.

The T-Display-S3 board definition in the ESP32 core has a bug — it sets `psram_type=opi` but not `-DBOARD_HAS_PSRAM`, so PSRAM fails to initialize. Fix with a build property override:

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

Also copy `User_Setup.h` from this repo to `~/Documents/Arduino/libraries/TFT_eSPI/User_Setup.h` for the T-Display-S3 parallel driver config.

## Fonts

Bitmap fonts are baked from variable TrueType sources using the Adafruit GFX `fontconvert` tool. To re-generate:

1. Instance a variable font at a specific weight with Python / `fonttools.varLib.instancer`
2. Run `fontconvert <instanced.ttf> <pointSize> <firstChar> <lastChar>` to produce a `.h` header
3. Drop it next to the sketch and `#include` it

See the existing `*.h` files for the pattern. Sources used:

- [Roboto Condensed](https://fonts.google.com/specimen/Roboto+Condensed) (Light / Medium / Black)
- [Big Shoulders Inline](https://fonts.google.com/specimen/Big+Shoulders+Inline) (Light / Black)
- [Savate](https://fonts.google.com/specimen/Savate) (Light / Black)

All open source (OFL).

## Display inversion note

The T-Display-S3 panel displays colors inverted relative to the sprite buffer: `0x0000` in memory renders as white on screen, `0xFFFF` renders as black. The sketch writes text as `0x0000` directly and lets the gradient-generated colors display as their natural complements (which still look vivid).
