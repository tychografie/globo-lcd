/*
 * GLOBO LCD — wake up in another country
 *
 * Every day the alarm fades in a random curated radio stream from somewhere
 * else in the world, as if you're on holiday. Flowing metaball gradient with
 * stretched variable-width typography; each station hop rerolls the identity.
 *
 * Merge of three projects (see INVENTORY.md):
 *   globo-lcd   → visual identity (gradient, palettes, stretched type)
 *   tPod        → engine (audio task, WiFi portal + QR, encoder ISR, battery,
 *                 screen timeout/backlight fade)
 *   globo-eink  → concept (alarm, 20s fade-in, stream validation/auto-retry)
 *
 * Hardware:
 *   LilyGo T-Display-S3 (ESP32-S3R8, 170x320 ST7789, 8MB PSRAM)
 *   MAX98357 I2S 3W amp + 3525 4ohm 3W speaker  — soldered on the P1 header
 *   EC11 / KY-040 rotary encoder                — on the P2 header (top, by 3V)
 *   JST 1.25mm 3.7V 1200mAh LiPo, extra USB-C wired to the onboard USB pads
 *
 * Controls — EC11 only (the onboard buttons are behind the enclosure):
 *   rotate                    volume
 *   short press               shuffle to a random station
 *   long press (1.2s)         settings menu (ported from globo-eink):
 *                             ALARM / STATIONS / NETWORK / BATTERY / WIFI RESET
 *                             rotate = browse, press = select, long = back
 *   hold encoder at boot      wipe saved WiFi credentials
 */

#include <TFT_eSPI.h>
#include <SPI.h>
#include <FS.h>
using fs::FS;          // TFT_eSPI defines FS_NO_GLOBALS on S3, undo it
#include <WiFi.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <vector>
#include <algorithm>
#include <time.h>
#include <qrcode.h>    // Espressif's esp_qrcode API (in core)
#include "Audio.h"     // ESP32-audioI2S by schreibfaul1 (3.4.x)

#include "RobotoBlack56.h"
#include "RobotoBlack32.h"
#include "RobotoMedium56.h"
#include "RobotoMedium32.h"
#include "RobotoLight56.h"
#include "RobotoLight32.h"
#include "BigShoulders56.h"
#include "BigShoulders32.h"
#include "BigShouldersLight56.h"
#include "BigShouldersLight32.h"
#include "Savate56.h"
#include "Savate32.h"
#include "SavateLight56.h"
#include "SavateLight32.h"

// Name and city each roll an independent random face per station.
const GFXfont* fonts56[] = {
  &RobotoCondensed_Black56pt7b,
  &RobotoCondensed_Medium56pt7b,
  &RobotoCondensed_Light56pt7b,
  &BigShouldersInline_Black56pt7b,
  &BigShouldersInline_Light56pt7b,
  &Savate_Black56pt7b,
  &Savate_Light56pt7b,
};
const GFXfont* fonts32[] = {
  &RobotoCondensed_Black32pt7b,
  &RobotoCondensed_Medium32pt7b,
  &RobotoCondensed_Light32pt7b,
  &BigShouldersInline_Black32pt7b,
  &BigShouldersInline_Light32pt7b,
  &Savate_Black32pt7b,
  &Savate_Light32pt7b,
};
#define NUM_FONTS 7

// ── Pins ─────────────────────────────────────────────────
// Audio sits on the P1 header (43/44/18 side, already soldered); the encoder
// gets the top of the P2 header next to 3V — physically the far end from the
// USB-C connector. GPIO 43/44 are the UART pins, but the S3 does serial over
// native USB-CDC, so nothing is lost.
#define PIN_POWER_ON  15
#define PIN_BACKLIGHT 38   // TFT_BL — PWM dim/off for screen timeout
#define I2S_BCLK      44
#define I2S_LRC       43
#define I2S_DOUT      18
#define PIN_ENC_A     1    // EC11 S1 — quadrature A
#define PIN_ENC_B     2    // EC11 S2 — quadrature B
#define PIN_ENC_SW    3    // EC11 KEY (strapping pin, but JTAG strap is
                           // ignored with default efuses — safe as input)
#define PIN_BAT_ADC   4    // VBAT/2 via the board's 100k/100k divider

#define BAT_FULL_MV  4200
#define BAT_EMPTY_MV 3300

#define SCREEN_TIMEOUT_MS 30000
#define BACKLIGHT_BRIGHT  255
#define BACKLIGHT_OFF     0

// ── Display ──────────────────────────────────────────────
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);          // full-screen canvas
TFT_eSprite nameMask = TFT_eSprite(&tft);     // cached station-name mask
TFT_eSprite cityMask = TFT_eSprite(&tft);     // cached city mask
TFT_eSprite ovMask   = TFT_eSprite(&tft);     // cached overlay mask (vol/alarm)
int nameInkX, nameInkY, nameInkW, nameInkH;
int cityInkX, cityInkY, cityInkW, cityInkH;
int ovInkX, ovInkY, ovInkW, ovInkH;
int cachedStation = -1;
char ovCachedStr[16] = "";
#define SW 320
#define SH 170
#define HALF_W (SW / 2)
#define HALF_H (SH / 2)

// ── Audio ────────────────────────────────────────────────
Audio audio;

// ── WiFi ─────────────────────────────────────────────────
// Configured via WiFiManager captive portal — no creds in code. First boot
// (or after a wipe) brings up an open AP with a join-QR on screen.
const char* WIFI_AP_NAME = "Globo-Setup";

// ── Time ─────────────────────────────────────────────────
const char* TZ_INFO    = "CET-1CEST,M3.5.0,M10.5.0/3";  // Europe/Amsterdam
const char* NTP_SERVER = "pool.ntp.org";

// ── Stations ─────────────────────────────────────────────
// Union of the tPod and globo-lcd lists, deduped. Curation rules in
// ../globo-eink/RADIO_STATIONS.md still apply, but the S3 has PSRAM so
// HTTPS and AAC streams are fine here.
struct Station {
  const char* name;
  const char* country;
  const char* city;
  const char* url;
};

const Station STATIONS[] = {
  {"SUBASIO",   "Italia",      "ASSISI",       "http://icy.unitedradio.it/Subasio.mp3"},
  {"SPRINT",    "Italia",      "PALERMO",      "http://nr8.newradio.it:9131/"},
  {"GLOBO",     "Italia",      "ROMA",         "https://globo.newradio.it/globorm64"},
  {"WWOZ",      "USA",         "NEW ORLEANS",  "http://wwoz-sc.streamguys1.com:80/wwoz-hi.mp3"},
  {"KEXP",      "USA",         "SEATTLE",      "http://kexp-mp3-128.streamguys1.com/kexp128.mp3"},
  {"WFMU",      "USA",         "JERSEY CITY",  "http://stream0.wfmu.org/freeform-128k.mp3"},
  {"DUBLAB",    "USA",         "LOS ANGELES",  "http://dublab.out.airtime.pro:8000/dublab_a"},
  {"PARADISE",  "USA",         "CALIFORNIA",   "http://stream.radioparadise.com/mp3-192"},
  {"NTS",       "UK",          "LONDON",       "http://stream-relay-geo.ntslive.net/stream"},
  {"CLASSIC",   "UK",          "LONDON",       "http://media-the.musicradio.com:80/ClassicFMMP3"},
  {"FIP",       "France",      "PARIS",        "http://icecast.radiofrance.fr/fip-midfi.mp3"},
  {"NOVA",      "France",      "PARIS",        "http://novazz.ice.infomaniak.ch/novazz-128.mp3"},
  {"LEIPZIG",   "Deutschland", "LEIPZIG",      "https://edge24.radio.radioleipzig.de/radioleipzig-live/stream/mp3"},
  {"JAZZ",      "Suisse",      "BERN",         "http://stream.srg-ssr.ch/m/rsj/mp3_128"},
  {"SEVILLANAS","Espana",      "SEVILLA",      "http://radio.wesped.com:8000/stream"},
  {"MAXXX",     "Polska",      "KRAKOW",       "http://195.150.20.7:8000/rmf_maxxx"},
  {"BYLGJAN",   "Iceland",     "REYKJAVIK",    "http://icecast.365net.is:8000/orbbylgjan.aac"},
  {"TBILISI",   "Georgia",     "TBILISI",      "http://iis.ge:8000/radiotbilisi.mp3"},
  {"JOY",       "Turkiye",     "ISTANBUL",     "http://21633.live.streamtheworld.com/JOY_FM128AAC.aac"},
  {"MOSAIQUE",  "Tunisia",     "TUNIS",        "https://radio.mosaiquefm.net/mosalive"},
  {"MEDINA",    "Morocco",     "MARRAKECH",    "https://cast5.my-control-panel.com/proxy/marrakec/stream"},
  {"DAKAR",     "Senegal",     "DAKAR",        "http://listen.senemultimedia.net:8090/stream"},
  {"AKABOOZI",  "Uganda",      "KAMPALA",      "http://162.244.80.52:8732/stream"},
  {"AL BAL",    "Lebanon",     "BEIRUT",       "https://albal-lbnet2.radioca.st/stream"},
  {"TOKYO",     "Japan",       "TOKYO",        "https://freefm80.radioca.st/"},
  {"CKUT",      "Canada",      "MONTREAL",     "https://ckut.out.airtime.pro/ckut_a"},
  {"LA MEJOR",  "Mexico",      "OAXACA",       "http://ororadio.serverroom.us:9142/;stream.mp3"},
  {"UNAM",      "Mexico",      "MEXICO DF",    "https://tv.radiohosting.online:9484/stream"},
  {"UNIDAS",    "Guatemala",   "GUATEMALA",    "https://stream.zenolive.com/z96sq8tndseuv"},
  {"SALSA",     "Puerto Rico", "SAN JUAN",     "https://cast4.my-control-panel.com/proxy/elpozosalsa/;"},
  {"BOLEROS",   "Peru",        "LIMA",         "https://stream.zeno.fm/5t45zksv7mruv"},
  {"PICHINCHA", "Ecuador",     "QUITO",        "https://icecast.radiopichincha.com/radiopichincha"},
  {"TRIPLE J",  "Australia",   "SYDNEY",       "http://live-radio01.mediahubaustralia.com/2TJW/mp3/"},
  {"3RRR",      "Australia",   "MELBOURNE",    "http://realtime.rrr.org.au/p1h"},
};
const int STATION_COUNT = sizeof(STATIONS) / sizeof(STATIONS[0]);
int currentStation = 0;   // randomized in setup() — shuffle on every boot

// ── State ────────────────────────────────────────────────
Preferences prefs;

enum UiMode {
  MODE_RADIO,        // gradient + station typography
  MODE_MENU,         // settings: spin through big stretched menu words
  MODE_STATION,      // browse stations sequentially, debounced connect
  MODE_ALARM_SET,    // rotate = time, press = arm/disarm
  MODE_INFO_NET,     // SSID / IP / signal card
  MODE_INFO_BAT,     // voltage / charge card
  MODE_WIFI_RESET,   // No/Yes confirmation, then wipe + restart
};
UiMode uiMode = MODE_RADIO;

const char* MENU_ITEMS[] = {"ALARM", "STATIONS", "NETWORK", "BATTERY", "WIFI RESET", "BACK"};
#define MENU_COUNT 6
int  menuIdx = 0;
bool wifiResetYes = false;          // selection on the confirm screen
uint32_t uiLastInputMs = 0;         // mode-timeout bookkeeping
uint32_t stationConnectAtMs = 0;    // debounced connect while browsing

int  volumeLevel = 12;             // 0..21 (mirrors audio.setVolume)
int  alarmHour   = 8;
int  alarmMinute = 0;
bool alarmArmed  = false;

// Loading: true from connect-request until the decoder reports a bitrate.
volatile bool g_loading        = true;
volatile bool g_connectRequest = false;   // audio task picks this up
volatile bool g_streamEof      = false;   // set from the audio callback
uint32_t connectStartMs = 0;
int      retryAttempts  = 0;
#define STREAM_VALIDATE_MS 15000   // silent for 15s → try the next station
#define MAX_RETRY_ATTEMPTS 8

// Alarm fade-in (globo-eink): 20s from silence to the saved volume.
bool     alarmFading  = false;
uint32_t fadeStartMs  = 0;
#define FADE_DURATION_MS 20000

uint32_t lastAlarmChange   = 0;   // 10s guard against trigger-while-adjusting
uint32_t volOverlayUntil   = 0;   // transient volume overlay
uint32_t lastFrame = 0, lastSecond = 0, lastHeartbeat = 0;
unsigned long animFrame = 0;
uint32_t g_frameCount = 0, g_renderMsAcc = 0;   // render diagnostics

// Screen timeout / backlight fade (tPod)
bool     screenAwake    = true;
uint32_t lastActivityMs = 0;
static int      g_blTarget  = BACKLIGHT_BRIGHT;
static int      g_blCurrent = 0;
static uint32_t g_blLastMs  = 0;
static const uint32_t BL_FADE_UP_MS   = 350;
static const uint32_t BL_FADE_DOWN_MS = 700;

// Battery (tPod): EMA-smoothed VBAT, polled ~1Hz.
static int   g_batPct = -1;
static float g_batMvEma = 0.0f;

// ── Color palettes ───────────────────────────────────────
const uint8_t palettes[][5][3] = {
  // 0 Sunrise
  {{255,160,60}, {255,90,130}, {255,210,50}, {255,190,140}, {180,120,220}},
  // 1 Ocean
  {{30,210,190}, {70,140,255}, {160,100,230}, {100,240,180}, {255,130,120}},
  // 2 Forest
  {{80,200,100}, {255,220,70}, {230,110,140}, {170,230,60}, {240,170,50}},
  // 3 Berry
  {{240,80,180}, {150,70,220}, {255,170,60}, {255,140,180}, {230,70,100}},
  // 4 Sky
  {{100,170,255}, {255,140,180}, {190,150,240}, {80,220,230}, {255,180,140}},
};
#define NUM_PALETTES 5

// ── Blobs ────────────────────────────────────────────────
#define NUM_BLOBS 5
struct Blob {
  float x, y, vx, vy;
  float baseRad, phase;
  uint8_t r, g, b;
};
Blob blobs[NUM_BLOBS];

// Color state is float so colors can ease forever (lava lamp): each blob
// always drifts toward its target, and every few seconds one blob quietly
// picks a fresh target. Station changes just retarget all five at once.
float curR[NUM_BLOBS], curG[NUM_BLOBS], curB[NUM_BLOBS];
uint8_t tgtR[NUM_BLOBS], tgtG[NUM_BLOBS], tgtB[NUM_BLOBS];
uint32_t nextRetargetMs = 0;

void initBlobs(int pal) {
  for (int i = 0; i < NUM_BLOBS; i++) {
    blobs[i].x = (SW / NUM_BLOBS) * i + random(SW / NUM_BLOBS);
    blobs[i].y = random(SH);
    blobs[i].vx = (random(100) - 50) / 40.0;
    blobs[i].vy = (random(100) - 50) / 40.0;
    blobs[i].baseRad = random(130, 210);
    blobs[i].phase = i * 1.257;
    blobs[i].r = palettes[pal][i][0];
    blobs[i].g = palettes[pal][i][1];
    blobs[i].b = palettes[pal][i][2];
    curR[i] = blobs[i].r; curG[i] = blobs[i].g; curB[i] = blobs[i].b;
    tgtR[i] = blobs[i].r; tgtG[i] = blobs[i].g; tgtB[i] = blobs[i].b;
  }
}

void shuffleGradient() {
  int pal = random(NUM_PALETTES);

  // Shuffle which blob gets which color from the palette
  int order[5] = {0,1,2,3,4};
  for (int i = 4; i > 0; i--) { int j = random(i+1); int t = order[i]; order[i] = order[j]; order[j] = t; }

  for (int i = 0; i < NUM_BLOBS; i++) {
    tgtR[i] = palettes[pal][order[i]][0];
    tgtG[i] = palettes[pal][order[i]][1];
    tgtB[i] = palettes[pal][order[i]][2];

    blobs[i].x = random(SW);
    blobs[i].y = random(SH);
    blobs[i].vx = (random(100) - 50) / 40.0;
    blobs[i].vy = (random(100) - 50) / 40.0;
    blobs[i].baseRad = random(130, 210);
  }
}

// dt is in "design frames" (55ms units) so blob speed stays the same however
// fast the renderer actually runs — at ~9fps each step just covers more
// ground instead of the whole animation crawling at half speed.
void updateBlobs(float dt) {
  for (int i = 0; i < NUM_BLOBS; i++) {
    blobs[i].x += blobs[i].vx * dt;
    blobs[i].y += blobs[i].vy * dt;
    if (blobs[i].x < -80)     blobs[i].vx =  abs(blobs[i].vx);
    if (blobs[i].x > SW + 80) blobs[i].vx = -abs(blobs[i].vx);
    if (blobs[i].y < -60)     blobs[i].vy =  abs(blobs[i].vy);
    if (blobs[i].y > SH + 60) blobs[i].vy = -abs(blobs[i].vy);
    blobs[i].vx += dt * (random(100) - 50) / 1500.0;
    blobs[i].vy += dt * (random(100) - 50) / 1500.0;
    blobs[i].vx = constrain(blobs[i].vx, -2.5f, 2.5f);
    blobs[i].vy = constrain(blobs[i].vy, -2.0f, 2.0f);
    // Sizes wander slowly too
    blobs[i].baseRad += dt * (random(100) - 50) * 0.01f;
    blobs[i].baseRad = constrain(blobs[i].baseRad, 130.0f, 225.0f);
  }

  // Lava lamp: colors ease toward their targets forever (~2.5s to settle
  // after a station change, imperceptibly smooth in between)...
  float k = 1.0f - powf(0.95f, dt);
  for (int i = 0; i < NUM_BLOBS; i++) {
    curR[i] += (tgtR[i] - curR[i]) * k;
    curG[i] += (tgtG[i] - curG[i]) * k;
    curB[i] += (tgtB[i] - curB[i]) * k;
    blobs[i].r = (uint8_t)(curR[i] + 0.5f);
    blobs[i].g = (uint8_t)(curG[i] + 0.5f);
    blobs[i].b = (uint8_t)(curB[i] + 0.5f);
  }
  // ...and every few seconds one blob quietly picks a fresh color so the
  // gradient never sits still between stations.
  if (millis() > nextRetargetMs) {
    nextRetargetMs = millis() + 5000 + random(8000);
    int i = random(NUM_BLOBS);
    int pal = random(NUM_PALETTES);
    int ci = random(5);
    tgtR[i] = palettes[pal][ci][0];
    tgtG[i] = palettes[pal][ci][1];
    tgtB[i] = palettes[pal][ci][2];
  }
}

// ── Film grain ───────────────────────────────────────────
static inline int grain(int x, int y) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return (int)((h >> 25) & 0x0F) - 8;
}

// TFT_eSprite stores 16-bit pixels byte-swapped for the push; writing the
// framebuffer directly (instead of drawPixel, which bounds-checks and swaps
// per call) roughly halves the render time, so all hot paths do this.
static inline uint16_t swap16(uint16_t c) { return (uint16_t)((c >> 8) | (c << 8)); }

// ── Gradient rendering ───────────────────────────────────
void renderGradient() {
  uint16_t* fb = (uint16_t*)spr.getPointer();
  int bx[NUM_BLOBS], by[NUM_BLOBS];
  uint32_t brs[NUM_BLOBS];

  for (int i = 0; i < NUM_BLOBS; i++) {
    bx[i] = (int)blobs[i].x;
    by[i] = (int)blobs[i].y;
    // Time-based so the breathing rate doesn't depend on render fps
    float breathe = 1.0f + 0.18f * sinf(millis() * 0.0011f + blobs[i].phase);
    float r = blobs[i].baseRad * breathe;
    brs[i] = (uint32_t)(r * r);
  }

  const int vcx = SW / 2, vcy = SH / 2;
  const uint32_t vmaxD2 = (uint32_t)(vcx * vcx + vcy * vcy);
  const uint32_t vigScale = vmaxD2 > 0 ? ((45u << 16) / vmaxD2) : 0;

  for (int hy = 0; hy < HALF_H; hy++) {
    int fy = hy * 2;

    for (int hx = 0; hx < HALF_W; hx++) {
      int fx = hx * 2;

      uint32_t rAcc = 0, gAcc = 0, bAcc = 0, wAcc = 0;
      for (int i = 0; i < NUM_BLOBS; i++) {
        int dx = fx - bx[i];
        int dy = fy - by[i];
        uint32_t d2 = (uint32_t)(dx * dx + dy * dy);
        uint32_t rs = brs[i];
        uint32_t denom = d2 + rs;
        if (denom == 0) denom = 1;
        uint32_t w = (rs << 8) / denom;
        w = (w * w) >> 8;
        w = (w * w) >> 8;  // ^4 for lava lamp sharpness
        if (w == 0) w = 1; // floor: far pixels average the blob colors
                           // instead of underflowing to black waves
        rAcc += blobs[i].r * w;
        gAcc += blobs[i].g * w;
        bAcc += blobs[i].b * w;
        wAcc += w;
      }
      if (wAcc == 0) wAcc = 1;
      int baseR = (int)(rAcc / wAcc);
      int baseG = (int)(gAcc / wAcc);
      int baseB = (int)(bAcc / wAcc);

      // Vignette
      int vdx = fx - vcx, vdy = fy - vcy;
      uint32_t vd2 = (uint32_t)(vdx * vdx + vdy * vdy);
      int dark = (int)((vd2 * vigScale) >> 16);
      baseR = baseR * (256 - dark) >> 8;
      baseG = baseG * (256 - dark) >> 8;
      baseB = baseB * (256 - dark) >> 8;

      // Write 2x2 with grain, straight into the framebuffer
      for (int dy = 0; dy < 2; dy++) {
        uint16_t* row = fb + (fy + dy) * SW + fx;
        for (int dx = 0; dx < 2; dx++) {
          int n = grain(fx + dx, fy + dy);
          row[dx] = swap16(tft.color565(
            constrain(baseR + n, 0, 255),
            constrain(baseG + n, 0, 255),
            constrain(baseB + n, 0, 255)));
        }
      }
    }
  }
}

// ── Stretched typography ─────────────────────────────────
// Render text to a mask sprite once at the font's natural width, find the ink
// bounds, then blitMask() stretches just that rectangle to any target rect.
// Short text gets wide, long text gets condensed — a fake variable width axis.

void renderTextMask(const char* text, const GFXfont* font,
                    TFT_eSprite &mask, int &inkX, int &inkY, int &inkW, int &inkH) {
  spr.setFreeFont(font);
  int naturalW = spr.textWidth(text);
  int naturalH = font->yAdvance;
  if (naturalW <= 0 || naturalH <= 0) { inkW = 0; return; }

  int sprW = naturalW + 4;
  int sprH = naturalH * 2;

  mask.deleteSprite();
  mask.setColorDepth(16);
  mask.createSprite(sprW, sprH);
  Serial.printf("[mask] '%s' %dx%d (%uKB) ptr=%p heap=%u psram=%u\n",
                text, sprW, sprH, (unsigned)(sprW * sprH * 2 / 1024),
                mask.getPointer(), (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getFreePsram());
  mask.fillSprite(TFT_BLACK);
  mask.setFreeFont(font);
  mask.setTextColor(TFT_WHITE, TFT_BLACK);
  mask.setTextDatum(ML_DATUM);
  mask.drawString(text, 0, sprH / 2);

  // Stride-2 ink scan over the raw buffer (the per-call readPixel version
  // took hundreds of ms on long 56pt names), dilated 1px afterwards to
  // cover what the stride skipped.
  uint16_t* mb = (uint16_t*)mask.getPointer();
  if (!mb) { inkW = 0; return; }
  uint16_t bgVal = mb[0];
  int minX = sprW, maxX = 0, minY = sprH, maxY = 0;
  for (int y = 0; y < sprH; y += 2) {
    const uint16_t* row = mb + y * sprW;
    for (int x = 0; x < sprW; x += 2) {
      if (row[x] != bgVal) {
        if (x < minX) minX = x;
        if (x > maxX) maxX = x;
        if (y < minY) minY = y;
        if (y > maxY) maxY = y;
      }
    }
  }
  if (maxX <= minX || maxY <= minY) {
    Serial.printf("[mask] '%s' NO INK FOUND\n", text);
    inkW = 0;
    return;
  }
  minX = max(0, minX - 1); minY = max(0, minY - 1);
  maxX = min(sprW - 1, maxX + 1); maxY = min(sprH - 1, maxY + 1);
  inkX = minX; inkY = minY;
  inkW = maxX - minX + 1;
  inkH = maxY - minY + 1;
}

// Box-filtered stretch with alpha blending. Each destination pixel averages
// its FULL source footprint (the masks are usually downscaled — the old
// 2x2-sample threshold skipped source rows, so thin strokes of the Light
// weights dropped out and edges stair-stepped), then the text color is
// blended onto the gradient by coverage: proper anti-aliasing.
int blitMask(TFT_eSprite &mask, int inkX, int inkY, int inkW, int inkH,
             int destX, int destY, int destW, int destH, uint16_t color) {
  if (inkW <= 0 || inkH <= 0 || destW <= 0 || destH <= 0) return -1;
  uint16_t* mb = (uint16_t*)mask.getPointer();
  uint16_t* fb = (uint16_t*)spr.getPointer();
  if (!mb || !fb) return -2;
  int painted = 0;
  int mw = mask.width();
  uint16_t bgVal = mb[0];
  uint16_t col = swap16(color);
  int tr = ((color >> 11) & 0x1F) << 3;
  int tg = ((color >>  5) & 0x3F) << 2;
  int tb = ( color        & 0x1F) << 3;
  int endX = inkX + inkW;
  int endY = inkY + inkH;

  // 8.8 fixed-point area sampling: every destination pixel averages its TRUE
  // fractional source window, with partial pixels weighted by overlap. The
  // previous integer-aligned box alternated 2px/3px footprints at scale
  // factors between 1x and 2x, which made stroke weight wobble — the
  // "glitchy" look. A minimum ~1.25px window keeps upscaled text smooth.
  uint32_t xStep = ((uint32_t)inkW << 8) / destW;
  uint32_t yStep = ((uint32_t)inkH << 8) / destH;
  uint32_t xWin = xStep < 320 ? 320 : xStep;
  uint32_t yWin = yStep < 320 ? 320 : yStep;
  const int32_t xMax = (int32_t)inkW << 8;
  const int32_t yMax = (int32_t)inkH << 8;

  for (int dy = 0; dy < destH; dy++) {
    int py = destY + dy;
    if (py < 0 || py >= SH) continue;
    int32_t cy = (int32_t)(dy * yStep) + (int32_t)(yStep >> 1);
    int32_t y0 = cy - (int32_t)(yWin >> 1);
    if (y0 < 0) y0 = 0;
    int32_t y1 = y0 + (int32_t)yWin;
    if (y1 > yMax) { y1 = yMax; y0 = max((int32_t)0, y1 - (int32_t)yWin); }
    int syA = y0 >> 8, syB = (y1 - 1) >> 8;
    uint16_t* out = fb + py * SW;

    for (int dx = 0; dx < destW; dx++) {
      int px = destX + dx;
      if (px < 0 || px >= SW) continue;
      int32_t cx = (int32_t)(dx * xStep) + (int32_t)(xStep >> 1);
      int32_t x0 = cx - (int32_t)(xWin >> 1);
      if (x0 < 0) x0 = 0;
      int32_t x1 = x0 + (int32_t)xWin;
      if (x1 > xMax) { x1 = xMax; x0 = max((int32_t)0, x1 - (int32_t)xWin); }
      int sxA = x0 >> 8, sxB = (x1 - 1) >> 8;

      uint32_t inkAcc = 0, totAcc = 0;
      for (int sy = syA; sy <= syB; sy++) {
        int32_t q0 = sy << 8;
        uint32_t wy = (uint32_t)(min(y1, q0 + 256) - max(y0, q0));
        const uint16_t* row = mb + (inkY + sy) * mw + inkX;
        for (int sx = sxA; sx <= sxB; sx++) {
          int32_t p0 = sx << 8;
          uint32_t w = wy * (uint32_t)(min(x1, p0 + 256) - max(x0, p0));
          totAcc += w;
          if (row[sx] != bgVal) inkAcc += w;
        }
      }
      if (inkAcc == 0 || totAcc == 0) continue;
      painted++;

      if (inkAcc >= totAcc) {
        out[px] = col;            // fully covered — solid text color
      } else {
        int a = (int)((uint64_t)inkAcc * 255 / totAcc);
        uint16_t bgc = swap16(out[px]);
        int br = ((bgc >> 11) & 0x1F) << 3;
        int bg = ((bgc >>  5) & 0x3F) << 2;
        int bb = ( bgc        & 0x1F) << 3;
        out[px] = swap16(spr.color565(
          (tr * a + br * (255 - a)) / 255,
          (tg * a + bg * (255 - a)) / 255,
          (tb * a + bb * (255 - a)) / 255));
      }
    }
  }
  return painted;
}

// Randomized layout params per station
int layoutPad;     // edge padding
int layoutGap;     // gap between lines
int layoutNamePct; // name gets this % of total height

void cacheTextMasks() {
  if (cachedStation == currentStation) return;
  cachedStation = currentStation;

  // Layout first, fonts second: the old pad range (50-70) could squeeze the
  // name into a ~15px band where it read as a smear — "the name never shows".
  layoutPad = random(24, 44);
  layoutGap = random(4, 10);
  layoutNamePct = random(48, 68);

  int totalH = SH - layoutPad * 2;
  int nameH = totalH * layoutNamePct / 100;
  int cityH = totalH - nameH - layoutGap;

  // Light weights vanish below ~45px of height; bias to the heavy faces then.
  static const int HEAVY[] = {0, 1, 3, 5};   // Black/Medium cuts
  int nameFont = (nameH < 45) ? HEAVY[random(4)] : random(NUM_FONTS);
  int cityFont = (cityH < 30) ? HEAVY[random(4)] : random(NUM_FONTS);

  renderTextMask(STATIONS[currentStation].name, fonts56[nameFont],
                 nameMask, nameInkX, nameInkY, nameInkW, nameInkH);
  renderTextMask(STATIONS[currentStation].city, fonts32[cityFont],
                 cityMask, cityInkX, cityInkY, cityInkW, cityInkH);
  Serial.printf("[typo] '%s' f=%d ink=%dx%d | '%s' f=%d ink=%dx%d | pad=%d nameH=%d cityH=%d\n",
                STATIONS[currentStation].name, nameFont, nameInkW, nameInkH,
                STATIONS[currentStation].city, cityFont, cityInkW, cityInkH,
                layoutPad, nameH, cityH);
}

// Overlay text (volume / alarm time), re-rendered only when the string changes.
void cacheOverlayMask(const char* str) {
  if (strcmp(str, ovCachedStr) == 0) return;
  strncpy(ovCachedStr, str, sizeof(ovCachedStr) - 1);
  renderTextMask(str, fonts56[0], ovMask, ovInkX, ovInkY, ovInkW, ovInkH);
}

// ── Small UI bits ────────────────────────────────────────
void drawBatteryIcon(int x, int y, int pct) {
  if (pct < 0) return;
  int w = 20, h = 10;
  spr.drawRoundRect(x, y, w, h, 2, TFT_WHITE);
  spr.fillRect(x + w, y + 3, 2, 4, TFT_WHITE);
  int fillW = ((w - 4) * pct + 50) / 100;
  uint16_t col = spr.color565(20, 120, 40);
  if (pct <= 10) col = spr.color565(220, 30, 30);
  else if (pct <= 25) col = spr.color565(240, 170, 0);
  if (fillW > 0) spr.fillRect(x + 2, y + 2, fillW, h - 4, col);
}

// ── Loading edge glow ────────────────────────────────────
// "Something is happening" the console way: while a stream is tuning, a
// luminous comet sweeps around the screen border, additively brightening
// the gradient underneath — no chrome, works on every palette. Intensity
// eases in and out so it never pops; it just dissolves when audio is live.
static float loadVis = 0.0f;

static inline void edgeBrighten(int x, int y, float a) {
  uint16_t* fb = (uint16_t*)spr.getPointer();
  uint16_t c = swap16(fb[y * SW + x]);
  int r = ((c >> 11) & 0x1F) << 3;
  int g = ((c >>  5) & 0x3F) << 2;
  int b = ( c        & 0x1F) << 3;
  r += (int)((255 - r) * a);
  g += (int)((255 - g) * a);
  b += (int)((255 - b) * a);
  fb[y * SW + x] = swap16(spr.color565(r, g, b));
}

void drawLoadingEdge() {
  loadVis += ((g_loading ? 1.0f : 0.0f) - loadVis) * 0.15f;
  if (loadVis < 0.03f) return;

  const int per   = 2 * (SW + SH);   // perimeter in px
  const int tail  = 260;             // comet tail length (long: reads smooth at ~9fps)
  const int depth = 3;               // how far the glow bleeds inward
  // 0.7 px/ms -> one lap in ~1.4s, integer math so it never drifts
  int head = (int)((millis() * 7UL / 10UL) % (uint32_t)per);

  for (int i = 0; i < tail; i++) {
    int p = head - i;
    if (p < 0) p += per;
    float t = 1.0f - (float)i / tail;
    float a = t * t * loadVis;       // quadratic falloff toward the tail

    // Map perimeter position to (x, y) plus the inward direction, clockwise
    // from the top-left corner.
    int x, y, dx, dy;
    if (p < SW)               { x = p;                     y = 0;                      dx = 0;  dy = 1; }
    else if (p < SW + SH)     { x = SW - 1;                y = p - SW;                 dx = -1; dy = 0; }
    else if (p < 2 * SW + SH) { x = SW - 1 - (p - SW - SH); y = SH - 1;                dx = 0;  dy = -1; }
    else                      { x = 0;                     y = SH - 1 - (p - 2 * SW - SH); dx = 1; dy = 0; }

    for (int d = 0; d < depth; d++) {
      edgeBrighten(x + dx * d, y + dy * d, a * (1.0f - (float)d / depth));
    }
  }
}

// ── Info cards (settings screens ported from globo-eink) ─
void drawCardBase() {
  spr.fillRoundRect(10, 10, SW - 20, SH - 20, 8, TFT_WHITE);
  spr.drawRoundRect(10, 10, SW - 20, SH - 20, 8, TFT_BLACK);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(TFT_BLACK);
}

void drawNetworkCard() {
  drawCardBase();
  spr.setFreeFont(&FreeSansBold9pt7b);
  spr.drawString("Network", 24, 24);
  spr.setFreeFont(&FreeSans9pt7b);
  spr.drawString("SSID: " + WiFi.SSID(), 24, 56);
  spr.drawString("IP: " + WiFi.localIP().toString(), 24, 80);
  int rssi = WiFi.RSSI();
  const char* quality = rssi > -50 ? "Excellent" : rssi > -60 ? "Good"
                      : rssi > -70 ? "Fair" : "Weak";
  spr.drawString("Signal: " + String(rssi) + " dBm (" + quality + ")", 24, 104);
  spr.setTextColor(spr.color565(120, 120, 120));
  spr.drawString("press: back", 24, 134);
}

void drawBatteryCard() {
  drawCardBase();
  spr.setFreeFont(&FreeSansBold9pt7b);
  spr.drawString("Battery", 24, 24);
  spr.setFreeFont(&FreeSans9pt7b);
  spr.drawString("Voltage: " + String(g_batMvEma / 1000.0f, 2) + " V", 24, 56);
  spr.drawString("Charge: " + String(g_batPct) + "%", 24, 80);
  // GPIO4 reads roughly VBUS/2 when USB is feeding the board and no cell
  // is fitted, so anything above the LiPo ceiling means external power.
  spr.drawString(String("Source: ") + (g_batMvEma >= 4300 ? "USB" : "Battery"), 24, 104);
  spr.setTextColor(spr.color565(120, 120, 120));
  spr.drawString("press: back", 24, 134);
}

void drawWifiResetCard() {
  drawCardBase();
  spr.setFreeFont(&FreeSansBold9pt7b);
  spr.drawString("WiFi Reset", 24, 24);
  spr.setFreeFont(&FreeSans9pt7b);
  spr.drawString("Forget all networks and", 24, 52);
  spr.drawString("restart the device?", 24, 74);

  int yNo = 104, yYes = 130, rowH = 22;
  if (!wifiResetYes) {
    spr.fillRoundRect(20, yNo - 4, 120, rowH, 4, TFT_BLACK);
    spr.setTextColor(TFT_WHITE);
    spr.drawString("> No", 28, yNo);
    spr.setTextColor(TFT_BLACK);
    spr.drawString("Yes", 28, yYes);
  } else {
    spr.drawString("No", 28, yNo);
    spr.fillRoundRect(20, yYes - 4, 120, rowH, 4, TFT_BLACK);
    spr.setTextColor(TFT_WHITE);
    spr.drawString("> Yes", 28, yYes);
  }
}

// Per-stage render profiling, printed with the [ui] heartbeat line
uint32_t g_gradMs = 0, g_textMs = 0, g_pushMs = 0;
int g_namePx = 0, g_cityPx = 0;   // pixels the last frame actually painted

// ── Main render ──────────────────────────────────────────
void renderFrame() {
  uint32_t t0 = millis();
  renderGradient();
  g_gradMs += millis() - t0;
  t0 = millis();

  if (uiMode == MODE_MENU) {
    // Settings the globo way: one big stretched word per item, spin to browse.
    cacheOverlayMask(MENU_ITEMS[menuIdx]);
    blitMask(ovMask, ovInkX, ovInkY, ovInkW, ovInkH, 30, 45, SW - 60, 80, TFT_WHITE);

    char buf[24];
    snprintf(buf, sizeof(buf), "%d / %d", menuIdx + 1, MENU_COUNT);
    spr.setTextDatum(TC_DATUM);
    spr.setFreeFont(&FreeSansBold9pt7b);
    spr.setTextColor(TFT_WHITE);
    spr.drawString("SETTINGS", SW / 2, 18);
    spr.drawString(buf, SW / 2, 142);
  } else if (uiMode == MODE_INFO_NET) {
    drawNetworkCard();
  } else if (uiMode == MODE_INFO_BAT) {
    drawBatteryCard();
  } else if (uiMode == MODE_WIFI_RESET) {
    drawWifiResetCard();
  } else if (uiMode == MODE_ALARM_SET) {
    // Big stretched alarm time on the gradient, typographic like everything else.
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", alarmHour, alarmMinute);
    cacheOverlayMask(buf);
    blitMask(ovMask, ovInkX, ovInkY, ovInkW, ovInkH, 40, 50, SW - 80, 80, TFT_WHITE);

    spr.setTextDatum(TC_DATUM);
    spr.setFreeFont(&FreeSansBold9pt7b);
    spr.setTextColor(TFT_WHITE);
    spr.drawString("ALARM", SW / 2, 18);
    spr.drawString(alarmArmed ? "ON - press to disarm" : "OFF - press to arm", SW / 2, 142);
  } else if (millis() < volOverlayUntil) {
    // Transient volume overlay: big stretched percentage.
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", volumeLevel * 100 / 21);
    cacheOverlayMask(buf);
    blitMask(ovMask, ovInkX, ovInkY, ovInkW, ovInkH, 70, 40, SW - 140, 90, TFT_WHITE);

    spr.setTextDatum(TC_DATUM);
    spr.setFreeFont(&FreeSansBold9pt7b);
    spr.setTextColor(TFT_WHITE);
    spr.drawString("VOLUME", SW / 2, 142);
  } else {
    cacheTextMasks();

    int totalH = SH - layoutPad * 2;
    int nameH = totalH * layoutNamePct / 100;
    int cityH = totalH - nameH - layoutGap;

    g_namePx = blitMask(nameMask, nameInkX, nameInkY, nameInkW, nameInkH,
             layoutPad, layoutPad, SW - layoutPad * 2, nameH, TFT_WHITE);
    g_cityPx = blitMask(cityMask, cityInkX, cityInkY, cityInkW, cityInkH,
             layoutPad, layoutPad + nameH + layoutGap, SW - layoutPad * 2, cityH, TFT_WHITE);

    if (uiMode == MODE_STATION) {
      char buf[24];
      snprintf(buf, sizeof(buf), "STATION %d / %d", currentStation + 1, STATION_COUNT);
      spr.setTextDatum(TC_DATUM);
      spr.setFreeFont(&FreeSansBold9pt7b);
      spr.setTextColor(TFT_WHITE);
      spr.drawString(buf, SW / 2, 18);
    }
  }

  // Corner chrome: alarm time top-left when armed, battery top-right.
  if (alarmArmed && uiMode == MODE_RADIO) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", alarmHour, alarmMinute);
    spr.setTextDatum(TL_DATUM);
    spr.setFreeFont(&FreeSansBold9pt7b);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(buf, 6, 5);
  }
  if (g_batPct >= 0 && g_batPct <= 25) drawBatteryIcon(SW - 28, 5, g_batPct);

  drawLoadingEdge();   // handles its own fade in/out, safe to call always
  g_textMs += millis() - t0;

  t0 = millis();
  spr.pushSprite(0, 0);
  g_pushMs += millis() - t0;
}

// ── Battery ──────────────────────────────────────────────
void updateBattery() {
  int pinMv = analogReadMilliVolts(PIN_BAT_ADC);
  int vbatMv = pinMv * 2;   // undo the 100k/100k divider
  if (g_batMvEma == 0.0f) g_batMvEma = (float)vbatMv;
  else g_batMvEma = 0.8f * g_batMvEma + 0.2f * (float)vbatMv;
  int mv = (int)g_batMvEma;
  if (mv <= BAT_EMPTY_MV) g_batPct = 0;
  else if (mv >= BAT_FULL_MV) g_batPct = 100;
  else g_batPct = (mv - BAT_EMPTY_MV) * 100 / (BAT_FULL_MV - BAT_EMPTY_MV);
}

// ── Screen timeout / backlight (tPod) ────────────────────
void stepBacklight() {
  if (g_blCurrent == g_blTarget) { g_blLastMs = millis(); return; }
  uint32_t now = millis();
  uint32_t dt  = now - g_blLastMs;
  if (dt < 4) return;
  g_blLastMs = now;
  bool goingUp = g_blCurrent < g_blTarget;
  uint32_t dur = goingUp ? BL_FADE_UP_MS : BL_FADE_DOWN_MS;
  int step = (int)((255UL * dt) / dur);
  if (step < 1) step = 1;
  if (goingUp) g_blCurrent = min(g_blCurrent + step, g_blTarget);
  else         g_blCurrent = max(g_blCurrent - step, g_blTarget);

  // At zero, drop out of PWM and drive the pin hard LOW — the panel stayed
  // visibly lit while LEDC claimed duty 0, so trust plain GPIO instead.
  static bool blPwm = true;
  if (g_blCurrent == 0) {
    if (blPwm) { ledcDetach(PIN_BACKLIGHT); blPwm = false; }
    pinMode(PIN_BACKLIGHT, OUTPUT);
    digitalWrite(PIN_BACKLIGHT, LOW);
  } else {
    if (!blPwm) { ledcAttach(PIN_BACKLIGHT, 2000, 8); blPwm = true; }
    ledcWrite(PIN_BACKLIGHT, (uint32_t)g_blCurrent);
  }
}

void wakeScreen() {
  screenAwake = true;
  g_blTarget = BACKLIGHT_BRIGHT;
  lastActivityMs = millis();
}

void updateScreenTimeout() {
  if (screenAwake && millis() - lastActivityMs > SCREEN_TIMEOUT_MS) {
    screenAwake = false;
    g_blTarget = BACKLIGHT_OFF;
    if (uiMode != MODE_RADIO) exitToRadio();
  }
  // Backlight stepping happens in the UI task, which never freezes.
}

// ── Stations / volume ────────────────────────────────────
void requestStation(int idx) {
  currentStation = idx;
  shuffleGradient();
  g_loading = true;
  connectStartMs = millis();
  g_connectRequest = true;   // the audio task does the actual connect
  Serial.printf("[station] -> %d/%d %s (%s, %s)\n", idx + 1, STATION_COUNT,
                STATIONS[idx].name, STATIONS[idx].country, STATIONS[idx].city);
}

void shuffleStation() {
  if (STATION_COUNT <= 1) { requestStation(currentStation); return; }
  int next;
  do { next = (int)(esp_random() % (uint32_t)STATION_COUNT); } while (next == currentStation);
  retryAttempts = 0;
  requestStation(next);
}

uint32_t volSaveAtMs = 0;   // debounced NVS write, 2s after the last detent

void changeVolume(int delta) {
  alarmFading = false;   // manual input cancels an in-progress fade
  volumeLevel = constrain(volumeLevel + delta, 0, 21);
  audio.setVolume((uint8_t)volumeLevel);
  volSaveAtMs = millis() + 2000;
  volOverlayUntil = millis() + 1500;
  Serial.printf("[vol] %d/21\n", volumeLevel);
}

// ── Alarm (globo-eink concept) ───────────────────────────
void exitAlarmMode() {
  uiMode = MODE_RADIO;
  prefs.putInt("alarmH", alarmHour);
  prefs.putInt("alarmM", alarmMinute);
  prefs.putBool("armed", alarmArmed);
  ovCachedStr[0] = '\0';
  Serial.printf("[alarm] %02d:%02d %s\n", alarmHour, alarmMinute, alarmArmed ? "armed" : "off");
}

void adjustAlarm(int detents) {
  int total = alarmHour * 60 + alarmMinute + detents * 5;
  total = ((total % 1440) + 1440) % 1440;
  alarmHour = total / 60;
  alarmMinute = total % 60;
  lastAlarmChange = millis();
  uiLastInputMs = millis();
}

// ── Mode plumbing ────────────────────────────────────────
void enterMode(UiMode m) {
  uiMode = m;
  ovCachedStr[0] = '\0';   // force the stretched overlay to re-render
  uiLastInputMs = millis();
}

void exitToRadio() {
  if (uiMode == MODE_ALARM_SET) { exitAlarmMode(); return; }   // saves to NVS
  enterMode(MODE_RADIO);
}

void doWifiReset() {
  Serial.println("[wifi] reset requested from menu — wiping and restarting");
  wipeSavedNetworks();
  WiFi.disconnect(true, true);   // also erase the core's stored credentials
  delay(200);
  ESP.restart();
}

void menuSelect() {
  switch (menuIdx) {
    case 0: enterMode(MODE_ALARM_SET); break;
    case 1: enterMode(MODE_STATION); break;
    case 2: enterMode(MODE_INFO_NET); break;
    case 3: enterMode(MODE_INFO_BAT); break;
    case 4: wifiResetYes = false; enterMode(MODE_WIFI_RESET); break;
    default: enterMode(MODE_RADIO); break;
  }
}

// Browsing flips the visuals instantly but only connects once the knob has
// rested for 700ms, so spinning through the list doesn't spam connects.
void browseStation(int detents) {
  currentStation = ((currentStation + detents) % STATION_COUNT + STATION_COUNT) % STATION_COUNT;
  shuffleGradient();
  stationConnectAtMs = millis() + 700;
  uiLastInputMs = millis();
}

void checkAlarm() {
  if (!alarmArmed || volumeLevel == 0) return;
  if (millis() - lastAlarmChange < 10000) return;   // just adjusted — hold off

  time_t now = time(nullptr);
  if (now < 1000000000) return;   // no NTP time yet
  struct tm t;
  localtime_r(&now, &t);

  static int lastTriggeredDay = -1;   // one-shot per day
  if (t.tm_hour == alarmHour && t.tm_min == alarmMinute && t.tm_yday != lastTriggeredDay) {
    lastTriggeredDay = t.tm_yday;
    Serial.printf("[alarm] triggered at %02d:%02d — good morning\n", t.tm_hour, t.tm_min);
    wakeScreen();
    audio.setVolume(0);
    alarmFading = true;
    fadeStartMs = millis();
    shuffleStation();   // a new country every morning
  }
}

void updateFade() {
  if (!alarmFading) return;
  uint32_t elapsed = millis() - fadeStartMs;
  if (elapsed >= FADE_DURATION_MS) {
    audio.setVolume((uint8_t)volumeLevel);
    alarmFading = false;
    return;
  }
  audio.setVolume((uint8_t)(volumeLevel * elapsed / FADE_DURATION_MS));
}

// If a stream never produces audio (or dies), march on to the next station
// so the alarm can't end up silent (globo-eink's validation logic).
void validateStream() {
  if (g_loading && audio.isRunning() && audio.getBitRate() > 0) {
    g_loading = false;
    retryAttempts = 0;
    Serial.println("[audio] stream is live");
  }
  if (g_loading && millis() - connectStartMs > STREAM_VALIDATE_MS) {
    if (retryAttempts < MAX_RETRY_ATTEMPTS) {
      retryAttempts++;
      Serial.printf("[audio] silent after %ds — retry %d/%d\n",
                    STREAM_VALIDATE_MS / 1000, retryAttempts, MAX_RETRY_ATTEMPTS);
      requestStation((currentStation + 1) % STATION_COUNT);
    } else {
      g_loading = false;   // give up until the user picks a station
      Serial.println("[audio] giving up after max retries");
    }
  }
  if (g_streamEof) {
    g_streamEof = false;
    Serial.println("[audio] stream ended — reconnecting");
    requestStation(currentStation);
  }
}

// ── Encoder (tPod ISR) ───────────────────────────────────
// EC11/KY-040 emits 4 quadrature transitions per detent. Buxton state machine
// in IRAM; the portMUX guards the detent counter against torn reads.
static volatile int32_t encDelta = 0;
static volatile uint8_t encState = 0;
static portMUX_TYPE encMux = portMUX_INITIALIZER_UNLOCKED;

static const int8_t QENC_TABLE[16] = {
   0, -1, +1,  0,
  +1,  0,  0, -1,
  -1,  0,  0, +1,
   0, +1, -1,  0,
};

static volatile uint32_t encIsrCount = 0;   // diagnostics: ISR storm detector

static void IRAM_ATTR encISR() {
  encIsrCount++;
  uint8_t newState = (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);
  int8_t step = QENC_TABLE[(encState << 2) | newState];
  encState = newState;
  static int8_t subStep = 0;
  subStep += step;
  portENTER_CRITICAL_ISR(&encMux);
  if (subStep >= 4)       { encDelta++; subStep = 0; }
  else if (subStep <= -4) { encDelta--; subStep = 0; }
  portEXIT_CRITICAL_ISR(&encMux);
}

static bool     encSwLast = HIGH;
static uint32_t encSwMs = 0, encSwPressMs = 0;
static bool     encLongFired = false;
#define LONG_PRESS_MS 1200

// All input goes through the EC11: rotate, short press, long press.
void onRotate(int d) {
  switch (uiMode) {
    case MODE_RADIO:      changeVolume(d); break;
    case MODE_MENU:       menuIdx = ((menuIdx + d) % MENU_COUNT + MENU_COUNT) % MENU_COUNT;
                          uiLastInputMs = millis(); break;
    case MODE_STATION:    browseStation(d); break;
    case MODE_ALARM_SET:  adjustAlarm(d); break;
    case MODE_WIFI_RESET: wifiResetYes = !wifiResetYes; uiLastInputMs = millis(); break;
    default:              uiLastInputMs = millis(); break;   // info cards ignore rotation
  }
}

void onShortPress() {
  switch (uiMode) {
    case MODE_RADIO:      shuffleStation(); break;
    case MODE_MENU:       menuSelect(); break;
    case MODE_STATION:    enterMode(MODE_RADIO); break;
    case MODE_ALARM_SET:
      alarmArmed = !alarmArmed;
      lastAlarmChange = millis();
      uiLastInputMs = millis();
      prefs.putBool("armed", alarmArmed);
      break;
    case MODE_INFO_NET:
    case MODE_INFO_BAT:   enterMode(MODE_MENU); break;
    case MODE_WIFI_RESET:
      if (wifiResetYes) doWifiReset();   // does not return
      enterMode(MODE_MENU);
      break;
  }
}

void onLongPress() {
  if (uiMode == MODE_RADIO) { menuIdx = 0; enterMode(MODE_MENU); }
  else exitToRadio();
}

void handleEncoder() {
  portENTER_CRITICAL(&encMux);
  int32_t d = encDelta;
  encDelta = 0;
  portEXIT_CRITICAL(&encMux);
  if (d != 0) {
    bool wasAwake = screenAwake;
    wakeScreen();
    if (wasAwake) onRotate((int)d);
  }

  bool sw = digitalRead(PIN_ENC_SW);
  uint32_t now = millis();
  if (sw != encSwLast && now - encSwMs > 30) {
    encSwMs = now;
    encSwLast = sw;
    if (sw == LOW) {
      encSwPressMs = now;
      encLongFired = false;
    } else if (encSwPressMs && !encLongFired) {
      // Short press fires on release. First press while asleep only wakes.
      bool wasAwake = screenAwake;
      wakeScreen();
      if (wasAwake) onShortPress();
      encSwPressMs = 0;
    }
  }
  if (sw == LOW && encSwPressMs && !encLongFired && now - encSwPressMs > LONG_PRESS_MS) {
    encLongFired = true;
    wakeScreen();
    onLongPress();
  }
}

// ── UI task (core 0) ─────────────────────────────────────
// Rendering lives on core 0 so it can never be starved by the audio task:
// connecttohost() (TLS handshake included) blocks the priority-5 audio task
// on core 1 for seconds, which froze the whole UI exactly when the loading
// animation mattered. WiFi tasks on core 0 preempt briefly but only cost
// the odd frame. Only this task touches tft/spr/masks after setup().
void uiTask(void*) {
  Serial.println("[uiTask] started");
  for (;;) {
    stepBacklight();
    bool visible = screenAwake || g_blCurrent > 0;
    if (visible) {
      uint32_t t0 = millis();
      static uint32_t lastBlobMs = 0;
      float blobDt = lastBlobMs ? (t0 - lastBlobMs) / 55.0f : 1.0f;
      lastBlobMs = t0;
      animFrame++;
      updateBlobs(constrain(blobDt, 0.5f, 4.0f));
      renderFrame();
      uint32_t dt = millis() - t0;
      g_renderMsAcc += dt;
      g_frameCount++;
      vTaskDelay(pdMS_TO_TICKS(dt >= 55 ? 5 : 55 - dt));
    } else {
      vTaskDelay(pdMS_TO_TICKS(50));
    }
  }
}

// ── Audio task (core 1, away from WiFi) ──────────────────
// All connecttohost() calls happen here so they never race audio.loop().
volatile uint32_t audioTaskTicks = 0;
void audioTask(void*) {
  Serial.println("[audioTask] started");
  for (;;) {
    if (g_connectRequest) {
      g_connectRequest = false;
      audio.stopSong();
      audio.connecttohost(STATIONS[currentStation].url);
    }
    audio.loop();
    audioTaskTicks++;
    // vTaskDelay(1) so the priority-1 Arduino loopTask on this core still
    // gets time to poll inputs and render.
    vTaskDelay(1);
  }
}

static const char* audioEventName(Audio::event_t e) {
  switch (e) {
    case Audio::evt_info:        return "info";
    case Audio::evt_eof:         return "eof";
    case Audio::evt_name:        return "station";
    case Audio::evt_streamtitle: return "title";
    case Audio::evt_bitrate:     return "bitrate";
    case Audio::evt_lasthost:    return "lasthost";
    default:                     return "?";
  }
}

void registerAudioCallbacks() {
  Audio::audio_info_callback = [](Audio::msg_t i) {
    if (i.e == Audio::evt_eof) g_streamEof = true;
    Serial.printf("[%s] %s\n", audioEventName(i.e), i.msg ? i.msg : (i.s ? i.s : ""));
  };
}

// ── WiFi portal + QR + saved-networks store (tPod) ───────
static int g_qrX0 = 0, g_qrY0 = 0, g_qrScale = 4;
static const int g_qrMaxN = 29;   // QR version 3

static void qrDisplayCb(esp_qrcode_handle_t handle) {
  for (int y = 0; y < g_qrMaxN; y++) {
    for (int x = 0; x < g_qrMaxN; x++) {
      if (esp_qrcode_get_module(handle, x, y)) {
        spr.fillRect(g_qrX0 + x * g_qrScale, g_qrY0 + y * g_qrScale,
                     g_qrScale, g_qrScale, TFT_BLACK);
      }
    }
  }
}

void drawQRPortal(const char* ssid) {
  String wifiURI = "WIFI:T:nopass;S:" + String(ssid) + ";;";

  int margin = 6;
  int availSide = SH - margin * 2 - 8;
  g_qrScale = availSide / g_qrMaxN;
  if (g_qrScale < 2) g_qrScale = 2;
  int qrPx = g_qrMaxN * g_qrScale;
  g_qrX0 = SW - qrPx - margin - 8;
  g_qrY0 = (SH - qrPx) / 2;

  spr.fillSprite(TFT_WHITE);

  int tx = 12;
  spr.setTextDatum(TL_DATUM);
  spr.setFreeFont(&FreeSansBold9pt7b);
  spr.setTextColor(TFT_BLACK);
  spr.drawString("WiFi Setup", tx, 22);

  spr.setFreeFont(&FreeSans9pt7b);
  spr.setTextColor(spr.color565(110, 110, 110));
  spr.drawString("Scan to join AP", tx, 52);
  spr.setTextColor(TFT_BLACK);
  spr.drawString(ssid, tx, 74);
  spr.setTextColor(spr.color565(110, 110, 110));
  spr.drawString("Portal opens", tx, 108);
  spr.drawString("automatically", tx, 128);

  spr.fillRect(g_qrX0 - margin, g_qrY0 - margin,
               qrPx + margin * 2, qrPx + margin * 2, TFT_WHITE);

  esp_qrcode_config_t cfg{};
  cfg.display_func = qrDisplayCb;
  cfg.max_qrcode_version = 3;
  cfg.qrcode_ecc_level = ESP_QRCODE_ECC_LOW;
  esp_qrcode_generate(&cfg, wifiURI.c_str());

  spr.pushSprite(0, 0);
}

// We keep every (ssid, psk) the user has ever joined, not just the last one.
// Namespace kept from tPod so networks saved on that build survive the merge.
static Preferences wifiPrefs;
static const char* WIFI_NS = "tpod-wifi";
static const char* WIFI_KEY_COUNT = "n";

struct WifiCred { String ssid; String psk; };

static String wifiSsidKey(int i) { return String("s") + i; }
static String wifiPskKey (int i) { return String("p") + i; }

std::vector<WifiCred> loadSavedNetworks() {
  std::vector<WifiCred> out;
  wifiPrefs.begin(WIFI_NS, true);
  int n = wifiPrefs.getInt(WIFI_KEY_COUNT, 0);
  for (int i = 0; i < n; i++) {
    String ssid = wifiPrefs.getString(wifiSsidKey(i).c_str(), "");
    String psk  = wifiPrefs.getString(wifiPskKey(i).c_str(),  "");
    if (ssid.length()) out.push_back({ssid, psk});
  }
  wifiPrefs.end();
  return out;
}

void rememberNetwork(const String& ssid, const String& psk) {
  if (!ssid.length()) return;
  auto creds = loadSavedNetworks();
  for (auto& c : creds) {
    if (c.ssid == ssid) {
      if (c.psk == psk) return;   // already stored
      c.psk = psk;                // updated password — rewrite the list
      wifiPrefs.begin(WIFI_NS, false);
      for (size_t i = 0; i < creds.size(); i++) {
        wifiPrefs.putString(wifiSsidKey(i).c_str(), creds[i].ssid);
        wifiPrefs.putString(wifiPskKey(i).c_str(),  creds[i].psk);
      }
      wifiPrefs.putInt(WIFI_KEY_COUNT, (int)creds.size());
      wifiPrefs.end();
      return;
    }
  }
  creds.push_back({ssid, psk});
  wifiPrefs.begin(WIFI_NS, false);
  int idx = (int)creds.size() - 1;
  wifiPrefs.putString(wifiSsidKey(idx).c_str(), ssid);
  wifiPrefs.putString(wifiPskKey(idx).c_str(),  psk);
  wifiPrefs.putInt(WIFI_KEY_COUNT, (int)creds.size());
  wifiPrefs.end();
  Serial.printf("[wifi] remembered SSID='%s' (total=%u)\n",
                ssid.c_str(), (unsigned)creds.size());
}

void wipeSavedNetworks() {
  wifiPrefs.begin(WIFI_NS, false);
  wifiPrefs.clear();
  wifiPrefs.end();
  Serial.println("[wifi] cleared saved networks store");
}

static bool tryConnect(const String& ssid, const String& psk, uint32_t timeoutMs) {
  Serial.printf("[wifi] trying SSID='%s'\n", ssid.c_str());
  WiFi.begin(ssid.c_str(), psk.c_str());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) {
    delay(200);
  }
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.disconnect(false, false);
  return false;
}

void ensureWiFi() {
  WiFiManager wm;
  wm.setConfigPortalTimeout(0);
  // Cap each waitForConnectResult at 8s — the "wait forever" default trips
  // the boot watchdog when the saved SSID isn't reachable (boot loop).
  wm.setConnectTimeout(8);

  // Hold the encoder button at boot >1.2s to wipe saved creds.
  uint32_t holdStart = millis();
  bool wipe = false;
  while (digitalRead(PIN_ENC_SW) == LOW && millis() - holdStart < 1500) {
    if (millis() - holdStart > 1200) { wipe = true; break; }
    delay(50);
  }
  if (wipe) {
    Serial.println("[wifi] encoder held at boot — wiping saved credentials");
    wm.resetSettings();
    wipeSavedNetworks();
  }

  // 1) Try every remembered network, strongest-first based on a scan.
  WiFi.mode(WIFI_STA);
  auto saved = loadSavedNetworks();
  if (!saved.empty()) {
    Serial.printf("[wifi] %u remembered network(s); scanning...\n", (unsigned)saved.size());
    int found = WiFi.scanNetworks(false, true);
    std::vector<std::pair<int32_t, int>> ranked;   // (rssi, index into saved)
    for (size_t i = 0; i < saved.size(); i++) {
      int32_t best = INT32_MIN;
      for (int j = 0; j < found; j++) {
        if (WiFi.SSID(j) == saved[i].ssid && WiFi.RSSI(j) > best) best = WiFi.RSSI(j);
      }
      ranked.push_back({best, (int)i});
    }
    std::sort(ranked.begin(), ranked.end(),
              [](const std::pair<int32_t,int>& a, const std::pair<int32_t,int>& b){
                return a.first > b.first;
              });
    WiFi.scanDelete();

    for (auto& r : ranked) {
      if (r.first == INT32_MIN) break;   // remaining ones aren't visible
      const auto& c = saved[r.second];
      if (tryConnect(c.ssid, c.psk, 10000)) {
        Serial.printf("[wifi] connected to '%s', ip=%s rssi=%d\n",
                      c.ssid.c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
        return;
      }
    }
    Serial.println("[wifi] no remembered network reachable; opening portal");
  }

  // 2) Fall back to the captive portal + QR code on screen.
  wm.setAPCallback([](WiFiManager* m){
    Serial.printf("[wifi] config portal up: SSID=%s IP=%s\n",
                  WIFI_AP_NAME, WiFi.softAPIP().toString().c_str());
    drawQRPortal(WIFI_AP_NAME);
  });

  Serial.println("[wifi] autoConnect()...");
  bool ok = wm.autoConnect(WIFI_AP_NAME, nullptr);
  if (ok) {
    Serial.printf("[wifi] connected, ip=%s rssi=%d\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
    rememberNetwork(WiFi.SSID(), WiFi.psk());
  } else {
    Serial.println("[wifi] autoConnect failed; rebooting in 5s");
    delay(5000); ESP.restart();
  }
}

// ── Setup ────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);   // don't block if no host is reading the CDC
  delay(1500);                // give USB CDC time to enumerate
  Serial.println("\n[boot] GLOBO starting...");
  Serial.printf("[boot] PSRAM: %u free, heap: %u free\n",
                (unsigned)ESP.getFreePsram(), (unsigned)ESP.getFreeHeap());

  pinMode(PIN_POWER_ON, OUTPUT);
  digitalWrite(PIN_POWER_ON, HIGH);

  pinMode(PIN_ENC_A,  INPUT_PULLUP);
  pinMode(PIN_ENC_B,  INPUT_PULLUP);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);
  encState = (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), encISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), encISR, CHANGE);

  tft.init();
  tft.setRotation(1);   // landscape 320x170
  tft.fillScreen(TFT_BLACK);

  // Backlight PWM MUST be set up after tft.init() — TFT_eSPI's init does its
  // own pinMode+digitalWrite on TFT_BL which would clobber the LEDC channel.
  // Explicit ledcAttach/ledcWrite (not analogWrite) so the channel is
  // definitely bound to the pin; full brightness immediately because
  // ensureWiFi() can block for many seconds before the UI task starts.
  ledcAttach(PIN_BACKLIGHT, 2000, 8);
  ledcWrite(PIN_BACKLIGHT, BACKLIGHT_BRIGHT);
  g_blCurrent = BACKLIGHT_BRIGHT;
  g_blTarget  = BACKLIGHT_BRIGHT;
  g_blLastMs  = millis();
  lastActivityMs = millis();

  spr.setColorDepth(16);
  spr.createSprite(SW, SH);
  spr.setTextWrap(false);

  prefs.begin("globo", false);
  volumeLevel = prefs.getInt("volume", 12);
  alarmHour   = prefs.getInt("alarmH", 8);
  alarmMinute = prefs.getInt("alarmM", 0);
  alarmArmed  = prefs.getBool("armed", false);
  Serial.printf("[prefs] vol=%d/21 alarm=%02d:%02d %s\n",
                volumeLevel, alarmHour, alarmMinute, alarmArmed ? "armed" : "off");

  analogReadResolution(12);
  for (int i = 0; i < 5; i++) updateBattery();   // burn off EMA warm-up
  Serial.printf("[bat] vbat=%.0fmV pct=%d\n", g_batMvEma, g_batPct);

  randomSeed(esp_random());
  initBlobs(0);

  // Gradient up while WiFi connects
  renderGradient();
  spr.pushSprite(0, 0);

  ensureWiFi();

  // Audio streaming hates WiFi modem-sleep: the default WIFI_PS_MIN_MODEM
  // adds 100-200ms packet latency cycles that drain the stream buffer on
  // marginal links. Costs ~80mA extra; worth it.
  WiFi.setSleep(WIFI_PS_NONE);

  configTzTime(TZ_INFO, NTP_SERVER);

  // I2S to the MAX98357. Warm tone curve for the small speaker: it can't
  // move air at the low end and shouts at the top, so +5dB bass / -4dB treble.
  // The library self-allocates ~655KB of PSRAM stream buffer — no manual
  // sizing needed, plenty of headroom for AAC to ride out WiFi hiccups.
  registerAudioCallbacks();
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  audio.setVolume((uint8_t)volumeLevel);
  audio.setTone(5.0f, 0.0f, -4.0f);

  // Shuffle: a random country every power-on. The actual connect happens on
  // the audio task so a slow host can't freeze setup().
  currentStation = (int)(esp_random() % (uint32_t)STATION_COUNT);
  shuffleGradient();
  g_loading = true;
  connectStartMs = millis();
  g_connectRequest = true;
  Serial.printf("[shuffle] boot station %d/%d %s\n",
                currentStation + 1, STATION_COUNT, STATIONS[currentStation].name);

  // Audio on core 1 at priority 5 — away from the WiFi stack on core 0, and
  // above the Arduino loop so TLS work can't starve the decoder. Rendering
  // on core 0 so the audio task can't starve the UI (see uiTask).
  xTaskCreatePinnedToCore(audioTask, "audio", 8192, NULL, 5, NULL, 1);
  xTaskCreatePinnedToCore(uiTask,    "ui",    8192, NULL, 1, NULL, 0);
}

// ── Main loop ────────────────────────────────────────────
void loop() {
  uint32_t now = millis();

  handleEncoder();
  updateScreenTimeout();
  updateFade();
  validateStream();

  // Debounced connect after browsing stations settles
  if (stationConnectAtMs && now >= stationConnectAtMs) {
    stationConnectAtMs = 0;
    retryAttempts = 0;
    g_loading = true;
    connectStartMs = now;
    g_connectRequest = true;
    Serial.printf("[station] browse -> %d/%d %s\n", currentStation + 1,
                  STATION_COUNT, STATIONS[currentStation].name);
  }

  // Auto-exit any settings screen after a while without input
  if (uiMode != MODE_RADIO) {
    uint32_t timeout = (uiMode == MODE_ALARM_SET) ? 6000 : 15000;
    if (now - uiLastInputMs > timeout) exitToRadio();
  }

  if (volSaveAtMs && now >= volSaveAtMs) {
    volSaveAtMs = 0;
    prefs.putInt("volume", volumeLevel);
  }

  if (now - lastSecond >= 1000) {
    lastSecond = now;
    updateBattery();
    checkAlarm();
  }

  if (now - lastHeartbeat >= 3000) {
    lastHeartbeat = now;
    Serial.printf("[hb] ticks=%u run=%d kbps=%u heap=%u psram=%u\n",
                  (unsigned)audioTaskTicks, (int)audio.isRunning(),
                  (unsigned)audio.getBitRate(), (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getFreePsram());
    uint32_t fc = g_frameCount ? g_frameCount : 1;
    Serial.printf("[ui] fps=%.1f avgRender=%ums (grad=%u text=%u push=%u) namePx=%d cityPx=%d awake=%d bl=%d mode=%d encIsr=%u loading=%d\n",
                  g_frameCount / 3.0f,
                  g_frameCount ? (unsigned)(g_renderMsAcc / g_frameCount) : 0,
                  (unsigned)(g_gradMs / fc), (unsigned)(g_textMs / fc),
                  (unsigned)(g_pushMs / fc), g_namePx, g_cityPx,
                  (int)screenAwake, g_blCurrent, (int)uiMode,
                  (unsigned)encIsrCount, (int)g_loading);
    g_frameCount = 0;
    g_renderMsAcc = 0;
    g_gradMs = g_textMs = g_pushMs = 0;
    encIsrCount = 0;
  }

  // Rendering happens in uiTask on core 0 — nothing to do here.
}
