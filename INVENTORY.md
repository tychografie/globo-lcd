# Globo / tPod — Project Inventory & Merge Plan

*Written 2026-06-12. Covers three codebases: globo-eink, globo-lcd, and tPod.*

## The concept

You wake up every day with a random (but curated) radio stream from a country,
as if you're on holiday. That's the product. Everything else is implementation.

## The three projects at a glance

| | globo-eink | globo-lcd | tPod |
|---|---|---|---|
| **Location** | `~/globo-lcd/globo-eink/` | `~/globo-lcd/globo_lcd/` | `~/tPod/ipod/` |
| **Git** | full history, on GitHub | 1 commit, on GitHub | ⚠️ not a git repo |
| **Board** | ESP32-WROOM (no PSRAM) | LilyGo T-Display S3 | LilyGo T-Display S3 |
| **Display** | 2.13" e-ink 212×104 | 320×170 LCD (TFT_eSPI) | 320×170 LCD (TFT_eSPI) |
| **Audio lib** | ESP8266Audio | ESP32-audioI2S | ESP32-audioI2S |
| **Codecs** | MP3 only, HTTP only | MP3, HTTP/S | MP3 + AAC, HTTP/S |
| **Stream buffer** | 2 KB (no PSRAM) | default | 655 KB in PSRAM |
| **WiFi** | WiFiManager portal + multi-network | ⚠️ hardcoded creds | WiFiManager portal + QR code + remembers all networks, tries by RSSI |
| **Stations** | 13 (pipe-strings) + web UI add/delete | 15 (struct array) | 23 (struct array, w/ colors & logo text) |
| **Alarm / daily wake** | ✅ full alarm clock logic | ❌ | ❌ (only mentioned in a comment) |
| **Encoder** | EC11 w/ velocity accel | ❌ (2 buttons only) | KY-040 w/ IRAM ISR + spinlock |
| **Battery mgmt** | 5-state power policy, CPU scaling, light sleep | ❌ | ADC + EMA smoothing + icon |
| **Visual identity** | 5 alarm "skins", split-color progress bars | metaball gradient + stretched typography | iPod Classic skin + same stretched typography |
| **Size** | ~3,926 lines | ~567 lines | ~1,345 lines |

## What each project does best

### globo-eink — the feature champion (~3,900 lines)

The only one that actually implements the product concept: it's an **alarm
clock**. Wake at a set time, pick a random station, 20-second fade-in,
auto-retry silent/dead streams until one works (silent alarm as last resort).

Features the others don't have:

- **Alarm logic**: settable wake time, one-shot per day, won't trigger within
  10s of adjusting, silent-stream detection (15s timeout / 500 failed decode
  loops → next station).
- **20-second volume fade-in** — the gentle holiday wake-up.
- **5-state power management** (USB / high / medium / low / critical): CPU
  frequency scaling 240→80 MHz, WiFi power policies, display throttling.
- **Light sleep** (~0.8 mA vs 40 mA awake, 50× improvement), 60s timer wake +
  button wake, NTP re-sync around sleep.
- **Web UI** on device IP: set alarm time, add/delete stations.
- **NTP time** (pool.ntp.org, Europe/Amsterdam).
- **Encoder velocity tracking** — accelerating steps (1–5 min/tick) feel great.
- **Docs**: `AUDIO_NOISE_FIX.md` (decoupling caps, star grounding, WiFi TX
  power → 11 dBm), `BATTERY_POWER_PLAN.md`, `RADIO_STATIONS.md` (how to find
  HTTP/MP3/128k streams via radio-browser.info), `extract_stream.py` (M3U8 →
  direct MP3 URL).

Constraint that shaped it: **no PSRAM**, hence ESP8266Audio, MP3-only,
HTTP-only, 2 KB buffer. On the S3 (which has PSRAM) none of these limits apply.

### globo-lcd — the visual identity (~570 lines)

The typographic direction: flowing **metaball gradient** background (5 blobs,
brownian motion, film grain, vignette, breathing animation), 5 color palettes
(Sunrise/Ocean/Forest/Berry/Sky) with eased 60-frame transitions, and
**variable-width stretched typography** — station name and city rendered to
mask sprites then stretched to fill the layout, random font + layout
proportions per station. 14 baked font headers (Roboto Condensed, Big
Shoulders Inline, Savate — multiple weights at 32/56pt).

Weaknesses: hardcoded WiFi credentials, no encoder, no battery handling, no
sleep, silent failure after 5 stream retries.

### tPod — the engine (~1,350 lines)

Started as an iPod joke, ended up with the best streaming and system layer:

- **Audio**: ESP32-audioI2S with 655 KB PSRAM buffer, MP3 **and AAC**, HTTPS,
  ICY metadata, 3-band EQ, dedicated audio task pinned to core 1 (priority 5)
  so UI can never starve the decoder. `WiFi.setSleep(WIFI_PS_NONE)` to keep
  AAC latency-safe.
- **WiFi onboarding**: captive portal ("tPod-Setup") with **QR code on
  screen**, remembers *every* network ever joined in NVS, on boot tries them
  in RSSI order, hold-button-at-boot to wipe credentials.
- **Encoder**: KY-040 quadrature ISR in IRAM with portMUX spinlock, sub-step
  accumulation — rock solid.
- **Power UX**: 30s screen timeout with smooth backlight PWM fade (350 ms up /
  700 ms down), first-press-while-dark only wakes the screen.
- **Battery**: ADC with EMA smoothing (α=0.2), color-coded icon.
- Same mask/stretch typography trick as globo-lcd, plus the iPod chrome
  (click wheel, header gradient, station logo tiles, loading spinner).

Weaknesses: not in git, 23 hardcoded stations, no alarm, pause/play flag never
wired up, no recovery if `connecttohost()` hangs.

## Target hardware (the LCD build)

- LilyGo T-Display S3 (non-touch) — ESP32-S3, 8 MB PSRAM, 170×320 ST7789
- MAX98357 I2S 3W Class D amplifier breakout
- 3525 4Ω 3W speaker
- EC11 / KY-040 rotary encoder
- JST 1.25mm 2-pin 3.7V 1200 mAh Li-Po battery
- USB-C connector for power + flashing (four wires)

### Pin map (decided 2026-06-12)

Physical layout: the onboard USB-C sits at the bottom of the board. The audio
amp is already soldered to the P1 header (the 43/44/18/17/21/16 side); the
EC11 goes on the opposite P2 header, at the top next to the 3V pin — the far
end from the USB-C. The extra panel-mount USB-C (power + flashing, four wires)
taps the onboard USB-C pads, staying on the USB side of the enclosure.

| Signal | Pin | Where |
|---|---|---|
| I2S BCLK / LRC / DOUT | 44 / 43 / 18 | P1 header (already soldered) |
| EC11 A / B / SW | 1 / 2 / 3 | P2 header top, next to 3V |
| Buttons | 0 / 14 | onboard |
| Battery ADC | 4 | internal (LCD_BAT_VOLT, 100k/100k divider) |
| Backlight PWM | 38 | internal (TFT_BL) |
| LCD power enable | 15 | internal |

GPIO 43/44 are the UART TX/RX pins, but the S3 does serial debug over native
USB-CDC, so dedicating them to I2S costs nothing. GPIO 3 is a strapping pin
whose JTAG strap is ignored with default efuses — safe for the encoder switch.

## Merge plan

**Base repo: `globo-lcd`** (as you prefer). Treat tPod and globo-eink as organ
donors. Roughly in order:

1. **Get tPod into git first.** It's the newest work and has zero version
   control. Either commit it as a branch/folder inside globo-lcd
   (`git checkout -b tpod-import`) or at minimum `git init` + commit in place,
   so nothing is lost during the merge.
2. **System layer from tPod** (mostly lift-and-shift, same board, same libs):
   - WiFi: captive portal + QR + multi-network NVS memory (replaces the
     hardcoded credentials — biggest single win)
   - Audio task on core 1 + ESP32-audioI2S config (AAC + MP3, PSRAM buffer)
   - Encoder ISR (the EC11 is in the new BOM; globo-lcd has no encoder code)
   - Battery ADC + EMA + indicator
   - Screen timeout + backlight fade
3. **Visual layer stays globo-lcd**: metaball gradient + palettes + stretched
   typography. (The iPod chrome can live on as an optional theme later, or
   retire with honor.)
4. **Concept layer from globo-eink** (port, don't copy — it's written for
   no-PSRAM ESP32 + e-ink):
   - Alarm: settable wake time, NTP sync, one-shot trigger guard
   - 20s fade-in on alarm
   - Stream validation + auto-retry-next-station (tPod has nothing if a
     stream hangs; eink's 15s-timeout logic fixes that)
   - Encoder velocity/acceleration feel for setting alarm time & volume
   - Light sleep between idle and alarm (with LCD off this is what makes a
     1200 mAh battery last); the 5-state power policy can come later,
     simplified — the S3 + LCD power profile differs from the e-ink build
   - Optional: the web UI for alarm/station management
5. **Stations**: merge the three lists (13 + 15 + 23, heavy overlap — FIP,
   Tbilisi, Mexico, Guatemala, Beirut etc. appear in multiple) into one
   curated list with country metadata. On the S3 there's no HTTP/MP3-only
   restriction, so the eink-era stream URLs can be upgraded where better
   HTTPS/AAC variants exist. `RADIO_STATIONS.md` + `extract_stream.py`
   carry over as the curation toolkit.
6. **Docs carry over**: `AUDIO_NOISE_FIX.md` applies verbatim to the new
   build (same amp, same noise physics). `BATTERY_POWER_PLAN.md` needs an
   LCD-era revision.

### What deliberately does *not* carry over

- ESP8266Audio and all no-PSRAM workarounds (2 KB buffer, HTTP-only, MP3-only)
- E-ink partial-refresh logic, split-color clipping wrappers, alarm skins
  (the metaball gradient replaces them as the wake-up visual)
- tPod's GPIO 43/44 I2S pins, the decorative click wheel, hardcoded 23-station
  list as-is

## Open questions

1. **Daily wake behavior**: eink had a user-set alarm time. The pitch says
   "wake up every day" — fixed alarm time set on device (eink model), or
   always-on holiday radio with the alarm as one feature?
2. **iPod chrome**: retire, or keep as a switchable theme alongside the
   gradient look?
3. **Repo layout**: fold everything into one repo (`globo-lcd` with the eink
   variant archived), or keep eink as its own living project?
