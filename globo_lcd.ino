/*
 * GLOBO LCD - Flowing gradient internet radio
 * LilyGo T-Display-S3 (ESP32-S3, 8MB PSRAM)
 *
 * Full-screen flowing color blobs with dynamically scaled
 * bold typography. Each station fills the display edge-to-edge.
 */

#include <WiFi.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include "Audio.h"
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

// Individual fonts to pick from — name and city pick independently
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
#define PIN_POWER_ON 15
#define BTN_LEFT     0
#define BTN_RIGHT    14
#define I2S_BCLK     1
#define I2S_LRC      2
#define I2S_DOUT     3

// ── WiFi ─────────────────────────────────────────────────
const char* WIFI_SSID     = "Odido-A0F423";
const char* WIFI_PASSWORD = "DD6F7V8RC4EMTALL";

// ── Display ──────────────────────────────────────────────
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);      // Main full-screen sprite
TFT_eSprite txtSpr = TFT_eSprite(&tft);   // Temp sprite for text rendering
TFT_eSprite nameMask = TFT_eSprite(&tft); // Cached name text mask
TFT_eSprite cityMask = TFT_eSprite(&tft); // Cached city text mask
int nameInkX, nameInkY, nameInkW, nameInkH;
int cityInkX, cityInkY, cityInkW, cityInkH;
int cachedStation = -1;
#define SW 320
#define SH 170
#define HALF_W (SW / 2)
#define HALF_H (SH / 2)

// ── Audio ────────────────────────────────────────────────
Audio audio;

// ── Color Palettes ───────────────────────────────────────
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

// ── Stations ─────────────────────────────────────────────
struct Station {
  const char* name;
  const char* city;
  const char* url;
  int palette;
  int font;  // index into fontSets[]
};

Station stations[] = {
  {"FIP",       "PARIS",      "http://icecast.radiofrance.fr/fip-midfi.mp3",                           4, 0},
  {"PARADISE",  "CALIFORNIA", "http://stream.radioparadise.com/mp3-192",                               0, 2},
  {"JAZZ",      "BERN",       "http://stream.srg-ssr.ch/m/rsj/mp3_128",                                2, 1},
  {"RAI",       "ROMA",       "http://icestreaming.rai.it/1.mp3",                                      0, 0},
  {"NTS",       "LONDON",     "http://stream-relay-geo.ntslive.net/stream",                             4, 1},
  {"MOSAIQUE",  "TUNIS",      "http://radiomosaiquefm.ice.infomaniak.ch/radiomosaiquefm-mp3-128.mp3",  3, 2},
  {"FIESTA",    "SEVILLA",    "http://195.55.74.211:8000/rtva_canalfiesta",                            0, 1},
  {"UNAM",      "MEXICO DF",  "http://132.248.218.50:8000/radioUNAM128",                               3, 0},
  {"TBILISI",   "GEORGIA",    "http://stream.rtvgeo.ge:8000/stereo128",                                2, 2},
  {"PICHINCHA", "QUITO",      "http://radio.pichincha.gob.ec:8000/stream",                             2, 0},
  {"TRIPLE J",  "SYDNEY",     "http://live-radio01.mediahubaustralia.com/2TJW/mp3/",                   1, 1},
  {"ISLA",      "SAN JUAN",   "http://11753.live.streamtheworld.com/WISLAM.mp3",                       1, 2},
  {"UNIDAS",    "GUATEMALA",  "http://eu.radioboss.fm:8124/stream",                                    2, 0},
  {"LEBANON",   "BEIRUT",     "http://lebradio.ddns.net:8000/stream",                                  3, 1},
  {"AFRICANA",  "KAMPALA",    "http://streams.radiomast.io/9d26e133-9973-4261-b1e0-12fe4e0f3c34",     2, 2},
};
#define NUM_STATIONS (sizeof(stations) / sizeof(stations[0]))

// ── State ────────────────────────────────────────────────
Preferences prefs;
int   currentStation = 0;
int   volume = 70;
bool  isPlaying = false, isConnecting = false, streamFailed = false;
unsigned long btnLeftDown = 0, btnRightDown = 0;
bool  btnLeftWasLong = false, btnRightWasLong = false;
unsigned long lastRetry = 0;
int   retryCount = 0;
unsigned long animFrame = 0, lastFrame = 0;

// Color transition
uint8_t curR[NUM_BLOBS], curG[NUM_BLOBS], curB[NUM_BLOBS];
uint8_t tgtR[NUM_BLOBS], tgtG[NUM_BLOBS], tgtB[NUM_BLOBS];
int transFrames = 0;
#define TRANS_LEN 60

// ── Blob system ──────────────────────────────────────────

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
    tgtR[i] = curR[i]; tgtG[i] = curG[i]; tgtB[i] = curB[i];
  }
}

void shuffleGradient() {
  // Random palette
  int pal = random(NUM_PALETTES);

  // Shuffle which blob gets which color from the palette
  int order[5] = {0,1,2,3,4};
  for (int i = 4; i > 0; i--) { int j = random(i+1); int t = order[i]; order[i] = order[j]; order[j] = t; }

  for (int i = 0; i < NUM_BLOBS; i++) {
    tgtR[i] = palettes[pal][order[i]][0];
    tgtG[i] = palettes[pal][order[i]][1];
    tgtB[i] = palettes[pal][order[i]][2];

    // Scramble blob positions and speeds for a fresh look
    blobs[i].x = random(SW);
    blobs[i].y = random(SH);
    blobs[i].vx = (random(100) - 50) / 40.0;
    blobs[i].vy = (random(100) - 50) / 40.0;
    blobs[i].baseRad = random(130, 210);
  }
  transFrames = TRANS_LEN;
}

void updateBlobs() {
  for (int i = 0; i < NUM_BLOBS; i++) {
    blobs[i].x += blobs[i].vx;
    blobs[i].y += blobs[i].vy;
    if (blobs[i].x < -80)     blobs[i].vx =  abs(blobs[i].vx);
    if (blobs[i].x > SW + 80) blobs[i].vx = -abs(blobs[i].vx);
    if (blobs[i].y < -60)     blobs[i].vy =  abs(blobs[i].vy);
    if (blobs[i].y > SH + 60) blobs[i].vy = -abs(blobs[i].vy);
    blobs[i].vx += (random(100) - 50) / 1500.0;
    blobs[i].vy += (random(100) - 50) / 1500.0;
    blobs[i].vx = constrain(blobs[i].vx, -2.5f, 2.5f);
    blobs[i].vy = constrain(blobs[i].vy, -2.0f, 2.0f);
  }

  if (transFrames > 0) {
    transFrames--;
    float t = 1.0 - (float)transFrames / TRANS_LEN;
    t = t * (2.0 - t);
    for (int i = 0; i < NUM_BLOBS; i++) {
      blobs[i].r = curR[i] + (int)((tgtR[i] - curR[i]) * t);
      blobs[i].g = curG[i] + (int)((tgtG[i] - curG[i]) * t);
      blobs[i].b = curB[i] + (int)((tgtB[i] - curB[i]) * t);
    }
    if (transFrames == 0) {
      for (int i = 0; i < NUM_BLOBS; i++) {
        curR[i] = tgtR[i]; curG[i] = tgtG[i]; curB[i] = tgtB[i];
        blobs[i].r = curR[i]; blobs[i].g = curG[i]; blobs[i].b = curB[i];
      }
    }
  }
}

// ── Film grain ───────────────────────────────────────────

static inline int grain(int x, int y) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return (int)((h >> 25) & 0x0F) - 8;
}

// ── Gradient rendering ───────────────────────────────────

void renderGradient() {
  int bx[NUM_BLOBS], by[NUM_BLOBS];
  uint32_t brs[NUM_BLOBS];

  for (int i = 0; i < NUM_BLOBS; i++) {
    bx[i] = (int)blobs[i].x;
    by[i] = (int)blobs[i].y;
    float breathe = 1.0f + 0.18f * sinf(animFrame * 0.06f + blobs[i].phase);
    float r = blobs[i].baseRad * breathe;
    brs[i] = (uint32_t)(r * r);
  }

  const int vcx = SW / 2, vcy = SH / 2;
  const uint32_t vmaxD2 = (uint32_t)(vcx * vcx + vcy * vcy);
  const uint32_t vigScale = vmaxD2 > 0 ? ((45u << 16) / vmaxD2) : 0;

  for (int hy = 0; hy < HALF_H; hy++) {
    if ((hy & 7) == 0) audio.loop();
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

      // Write 2x2 with grain
      for (int dy = 0; dy < 2; dy++) {
        for (int dx = 0; dx < 2; dx++) {
          int px = fx + dx, py = fy + dy;
          int n = grain(px, py);
          spr.drawPixel(px, py, tft.color565(
            constrain(baseR + n, 0, 255),
            constrain(baseG + n, 0, 255),
            constrain(baseB + n, 0, 255)));
        }
      }
    }
  }
}

// ── Scaled text rendering ────────────────────────────────
// Render text to temp sprite, then stretch to fill target rect.
// This creates the "variable width" effect - short text gets wide,
// long text gets condensed. Like a real variable font width axis.

// Render text to a mask sprite once, cache ink bounds
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
  mask.fillSprite(TFT_BLACK);
  mask.setFreeFont(font);
  mask.setTextColor(TFT_WHITE, TFT_BLACK);
  mask.setTextDatum(ML_DATUM);
  mask.drawString(text, 0, sprH / 2);

  // Find ink bounds
  uint16_t bgVal = mask.readPixel(0, 0);
  int minX = sprW, maxX = 0, minY = sprH, maxY = 0;
  for (int y = 0; y < sprH; y++) {
    for (int x = 0; x < sprW; x++) {
      if (mask.readPixel(x, y) != bgVal) {
        if (x < minX) minX = x;
        if (x > maxX) maxX = x;
        if (y < minY) minY = y;
        if (y > maxY) maxY = y;
      }
    }
  }
  if (maxX <= minX || maxY <= minY) { inkW = 0; return; }
  inkX = minX; inkY = minY;
  inkW = maxX - minX + 1;
  inkH = maxY - minY + 1;
}

// Blit cached mask with 2x2 supersampled anti-aliasing
void blitMask(TFT_eSprite &mask, int inkX, int inkY, int inkW, int inkH,
              int destX, int destY, int destW, int destH) {
  if (inkW <= 0 || inkH <= 0) return;
  uint16_t bgVal = mask.readPixel(0, 0);
  int mxX = inkX + inkW - 1;
  int mxY = inkY + inkH - 1;

  for (int dy = 0; dy < destH; dy++) {
    int sY  = inkY + (int)((long)dy * inkH / destH);
    int sY1 = min(sY + 1, mxY);
    for (int dx = 0; dx < destW; dx++) {
      int sX  = inkX + (int)((long)dx * inkW / destW);
      int sX1 = min(sX + 1, mxX);

      // 2x2 supersample
      int c = 0;
      if (mask.readPixel(sX,  sY)  != bgVal) c++;
      if (mask.readPixel(sX1, sY)  != bgVal) c++;
      if (mask.readPixel(sX,  sY1) != bgVal) c++;
      if (mask.readPixel(sX1, sY1) != bgVal) c++;

      if (c == 0) continue;

      int px = destX + dx;
      int py = destY + dy;

      if (c >= 2) {
        spr.drawPixel(px, py, 0x0000);  // white — clean threshold, no blending
      }
    }
  }
}

// Randomized layout params per station
int layoutPad;     // edge padding
int layoutGap;     // gap between lines
int layoutNamePct; // name gets this % of total height

// Cache text masks — name and city each get independent random font + weight
void cacheTextMasks() {
  if (cachedStation == currentStation) return;
  cachedStation = currentStation;

  // Independent random font for each line
  int nameFont = random(NUM_FONTS);
  int cityFont = random(NUM_FONTS);

  // Random layout
  layoutPad = random(50, 70);
  layoutGap = random(2, 10);
  layoutNamePct = random(42, 72);

  renderTextMask(stations[currentStation].name, fonts56[nameFont],
                 nameMask, nameInkX, nameInkY, nameInkW, nameInkH);
  renderTextMask(stations[currentStation].city, fonts32[cityFont],
                 cityMask, cityInkX, cityInkY, cityInkW, cityInkH);
}

// ── Main render frame ────────────────────────────────────

void renderFrame() {
  // 1. Render gradient background
  renderGradient();

  // 2. Cache text masks if station changed
  cacheTextMasks();

  // 3. Overlay text with randomized layout
  int totalH = SH - layoutPad * 2;
  int nameH = totalH * layoutNamePct / 100;
  int cityH = totalH - nameH - layoutGap;

  blitMask(nameMask, nameInkX, nameInkY, nameInkW, nameInkH,
           layoutPad, layoutPad, SW - layoutPad * 2, nameH);
  blitMask(cityMask, cityInkX, cityInkY, cityInkW, cityInkH,
           layoutPad, layoutPad + nameH + layoutGap, SW - layoutPad * 2, cityH);


  // 3. Push to display
  spr.pushSprite(0, 0);
}

// ── Setup ────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(200);
  psramInit();
  Serial.printf("\nGLOBO LCD | PSRAM: %dKB\n", ESP.getFreePsram() / 1024);

  pinMode(PIN_POWER_ON, OUTPUT);
  digitalWrite(PIN_POWER_ON, HIGH);
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  spr.setColorDepth(16);
  spr.createSprite(SW, SH);

  prefs.begin("globo", false);
  volume = prefs.getInt("volume", 70);

  randomSeed(esp_random());
  initBlobs(0);

  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  audio.setVolume(volume * 21 / 100);

  // Render gradient while connecting
  renderGradient();
  spr.pushSprite(0, 0);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.setSleep(false);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    attempts++;
    animFrame++;
    updateBlobs();
    renderGradient();
    spr.pushSprite(0, 0);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WiFi OK - %s\n", WiFi.localIP().toString().c_str());
    currentStation = random(NUM_STATIONS);
    shuffleGradient();
    startStream();
  }
}

// ── Main Loop ────────────────────────────────────────────

void loop() {
  audio.loop();
  handleButtons();

  if (millis() - lastFrame >= 55) {
    lastFrame = millis();
    animFrame++;
    updateBlobs();
    renderFrame();
  }

  if (streamFailed && WiFi.status() == WL_CONNECTED) {
    if (millis() - lastRetry > 3000 && retryCount < 5) {
      retryCount++;
      nextStation();
    } else if (retryCount >= 5) {
      streamFailed = false;
      retryCount = 0;
    }
  }
}

// ── Audio ────────────────────────────────────────────────

void startStream() {
  isConnecting = true; streamFailed = false;
  Serial.printf(">> %s (%s)\n", stations[currentStation].name, stations[currentStation].city);
  bool ok = audio.connecttohost(stations[currentStation].url);
  isPlaying = ok; isConnecting = false;
  if (!ok) { streamFailed = true; lastRetry = millis(); Serial.println("   FAIL"); }
  else Serial.println("   OK");
}

void nextStation() {
  audio.stopSong();
  currentStation = (currentStation + 1) % NUM_STATIONS;
  prefs.putInt("station", currentStation);
  shuffleGradient();
  startStream();
}

void prevStation() {
  audio.stopSong();
  currentStation = (currentStation - 1 + NUM_STATIONS) % NUM_STATIONS;
  prefs.putInt("station", currentStation);
  shuffleGradient();
  startStream();
}

// ── Buttons ──────────────────────────────────────────────

void handleButtons() {
  bool L = (digitalRead(BTN_LEFT) == LOW);
  bool R = (digitalRead(BTN_RIGHT) == LOW);
  unsigned long now = millis();

  if (L && btnLeftDown == 0) { btnLeftDown = now; btnLeftWasLong = false; }
  if (L && !btnLeftWasLong && btnLeftDown > 0 && (now - btnLeftDown > 800)) {
    btnLeftWasLong = true; adjustVolume(-10);
  }
  if (!L && btnLeftDown > 0) {
    if (!btnLeftWasLong && (now - btnLeftDown > 50)) prevStation();
    btnLeftDown = 0;
  }

  if (R && btnRightDown == 0) { btnRightDown = now; btnRightWasLong = false; }
  if (R && !btnRightWasLong && btnRightDown > 0 && (now - btnRightDown > 800)) {
    btnRightWasLong = true; adjustVolume(10);
  }
  if (!R && btnRightDown > 0) {
    if (!btnRightWasLong && (now - btnRightDown > 50)) nextStation();
    btnRightDown = 0;
  }
}

void adjustVolume(int delta) {
  volume = constrain(volume + delta, 0, 100);
  audio.setVolume(volume * 21 / 100);
  prefs.putInt("volume", volume);
  Serial.printf("Vol: %d%%\n", volume);
}

// ── Audio Callbacks ──────────────────────────────────────

void audio_info(const char *info) { Serial.printf("i: %s\n", info); }
void audio_eof_stream(const char *info) { streamFailed = true; lastRetry = millis(); retryCount = 0; }
void audio_showstreamtitle(const char *info) { Serial.printf("Now: %s\n", info); }
void audio_error(const char *info) { Serial.printf("ERR: %s\n", info); isPlaying = false; streamFailed = true; lastRetry = millis(); }
