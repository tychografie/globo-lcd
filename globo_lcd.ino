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
 * Build (arduino-cli): esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,
 *   PartitionScheme=app3M_fat9M_16MB + build.defines=-DBOARD_HAS_PSRAM
 *   ⚠ CDCOnBoot MUST stay OFF (default). With the console on USB-Serial-JTAG
 *   ("cdc"), a plugged-but-idle USB port degrades WiFi RX so badly that all
 *   inbound connections (web remote, mDNS) go deaf while outbound streaming
 *   still works — hours were lost to this. Console is UART0; live telemetry
 *   comes from the UDP heartbeat (port 9909) and /api/status instead.
 *
 * Web remote: http://globo.local — volume, stations, EQ, alarm, sleep timer.
 *
 * Controls — EC11 only (the onboard buttons are behind the enclosure):
 *   rotate                    volume
 *   short press               shuffle to a random station
 *   long press (0.8s)         settings menu (ported from globo-eink):
 *                             ALARM / STATIONS / NETWORK / BATTERY / WIFI RESET
 *                             rotate = browse, press = select, long = back
 *   hold encoder at boot      wipe saved WiFi credentials
 */

#include <TFT_eSPI.h>
#include <SPI.h>
#include <FS.h>
using fs::FS;          // TFT_eSPI defines FS_NO_GLOBALS on S3, undo it
#include <WiFi.h>
#include <DNSServer.h>   // captive redirect for the offline setup hub
#include <WebServer.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <Preferences.h>
#include <vector>
#include <algorithm>
#include <time.h>
#include <qrcode.h>    // Espressif's esp_qrcode API (in core)
#include <FFat.h>      // 9MB fat partition holds the soundscape loops
#include "Audio.h"     // ESP32-audioI2S by schreibfaul1 (3.4.x)
#include <esp_sleep.h>
#include <esp_wifi.h>
#include "driver/rtc_io.h"

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
#include "RobotoCondensedUI.h"   // Poster theme's UI voice (menu/labels)

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
// Audio sits on the 10/11/12 side of the header; the encoder gets the top of
// the P2 header next to 3V — physically the far end from the USB-C connector.
#define PIN_POWER_ON  15
#define PIN_BACKLIGHT 38   // TFT_BL — PWM dim/off for screen timeout
#define I2S_BCLK      11   // MAX98357 BCLK
#define I2S_LRC       12   // MAX98357 LRC/WS
#define I2S_DOUT      10   // MAX98357 DIN
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

// Captured stretch coverage for the radio typography (see blitCached).
// Declared up here so the .ino auto-prototypes compile.
struct BlitCache {
  int      station = -1;   // key: masks + layout are all per-station
  int      x = 0, y = 0, w = 0, h = 0;
  uint8_t* cov = nullptr;
};
static BlitCache nameBC, cityBC;

#define SW 320
#define SH 170
#define HALF_W (SW / 2)
#define HALF_H (SH / 2)

// Explicit prototype: the .ino auto-prototype chokes on default arguments.
void drawTextAlpha(const char* s, const GFXfont* font, int y, uint8_t alpha,
                   uint16_t color = TFT_WHITE, int cx = SW / 2);

// QR plumbing (shared by the setup portal and the network/remote card).
static int g_qrX0 = 0, g_qrY0 = 0, g_qrScale = 4;
static int g_qrMargin = 8;        // white quiet zone around the code

// ── Audio ────────────────────────────────────────────────
Audio audio;

// ── Web remote ───────────────────────────────────────────
// A control surface served by the device itself: http://globo.local. All
// handlers run in loop() context (core 1) — the exact same context as the
// encoder path — so they can safely reuse changeVolume()/requestStation()
// and the g_connectRequest plumbing without new locking.
WebServer server(80);
// UDP heartbeat telemetry: opening USB serial resets the S3, so the only way
// to watch a long-running device is to have it broadcast its own vitals.
// Listen with:  nc -ul 9909   (or any UDP listener on the LAN)
WiFiUDP telem;
volatile uint32_t g_webHits = 0;   // requests actually served

// Now-playing title from the stream's ICY metadata. Written by the audio
// callback (audio task), read by the web status handler (loop task) — a spin
// mux guards the copy both ways.
static portMUX_TYPE titleMux = portMUX_INITIALIZER_UNLOCKED;
static char g_streamTitle[128] = "";

// Framebuffer screenshot (/api/screen): the UI task copies the sprite into a
// PSRAM snapshot right after a push (no tearing), the handler streams it as
// raw byte-swapped RGB565. Converted to PNG on the client side.
static uint16_t*      g_shotBuf     = nullptr;
volatile bool         g_shotRequest = false;
volatile bool         g_shotReady   = false;

// Sleep timer (web-only feature): stop the stream after N minutes, with a
// gentle 15s fade so it never cuts out mid-note.
uint32_t sleepAtMs        = 0;   // 0 = no timer
bool     sleepFading      = false;
uint32_t sleepFadeStartMs = 0;
#define SLEEP_FADE_MS 15000

// ── WiFi ─────────────────────────────────────────────────
// No creds in code: first boot (or a wipe) raises the offline hub — an open
// AP with our own captive setup page, and a join-QR on the LCD's SETUP tab.
const char* WIFI_AP_NAME = "Globo-Setup";

// Runtime WiFi watcher — the travel fix. Boot-time ensureWiFi() is a one-shot,
// but on the road the network appears AFTER boot: a phone hotspot only
// broadcasts while its settings screen is open (or a client is attached), so
// "power up Globo, then fumble the hotspot on" used to mean permanent offline
// until a restart. The watcher runs from loop(): whenever the link is down it
// rescans for remembered networks every WW_SCAN_EVERY_MS and joins the
// strongest one that shows up — flip the hotspot on and Globo walks in by
// itself, no reboot, no portal. States: ONLINE (link up, idle) → WAIT
// (countdown to next scan) → SCANNING (async scan in flight) → JOINING
// (WiFi.begin issued, waiting on the handshake) → back to WAIT or ONLINE.
enum WifiWatch { WW_ONLINE, WW_WAIT, WW_SCANNING, WW_JOINING };
static WifiWatch wwState = WW_ONLINE;
static uint32_t  wwNextMs = 0;          // WW_WAIT: when the next scan fires
static uint32_t  wwJoinT0 = 0;          // WW_JOINING: when the attempt started
static String    wwSsid;                // network the current attempt targets
static char      wwLastResult[48] = ""; // last outcome, for the NETWORK card
static int       g_savedNetCount = 0;   // cached — NVS is too slow per frame
#define WW_SCAN_EVERY_MS   12000
#define WW_JOIN_TIMEOUT_MS 12000
#define WW_DROP_GRACE_MS    8000  // core autoReconnect gets first shot at a drop

// Services that normally start in setup() right after a successful connect.
// When the first connect happens mid-session (watcher), they start then.
static bool g_webUp = false, g_ntpUp = false;

// ── Offline hub: soft-AP + captive setup page + search, all at once ──
// While offline the device runs WIFI_AP_STA: the Globo-Setup AP serves a
// poster-styled setup page (our own web server + DNS hijack — WiFiManager is
// gone) WHILE the watcher keeps scanning for remembered networks. The LCD
// shows a two-tab hub (SEARCH | SETUP); menu and radio are online-only.
static DNSServer g_dns;
static bool g_hubUp = false;
static int  hubTab  = 1;               // 0 = SEARCH, 1 = SETUP (set on hub start)

// Last scan results, cached for the setup page — written by the watcher on
// scan completion (loop task), read by /api/scan (web task). Mux-guarded.
struct ScanHit { char ssid[33]; int8_t rssi; bool known; };
static ScanHit g_scanHits[15];
static volatile int g_scanHitCount = 0;
static portMUX_TYPE scanMux = portMUX_INITIALIZER_UNLOCKED;

// Saved-network names for the hub's SEARCH ticker (UI task reads these; NVS
// is too slow to query per frame).
static char g_savedSsids[8][33];

// A credential fresh from the setup page jumps the watcher's queue.
static String wwForceSsid, wwForcePsk;
static volatile bool wwForcePending = false;
void startWifiHub();
void stopWifiHub();

// Explicit prototypes: the .ino auto-prototype misses these (each is used
// before its definition — the retry hook in onShortPress, the web bring-up
// in the watcher's onWifiRegained).
void wifiWatcherRetryNow();
void webSetup();

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
  // Radio Globo Roma cut (newradio.it injects geo-targeted preroll ads);
  // its replacement Tele Radio Stereo cut too (Tycho). Roma lives on via TRE.
  {"WWOZ",      "USA",         "NEW ORLEANS",  "http://wwoz-sc.streamguys1.com:80/wwoz-hi.mp3"},
  {"WFMU",      "USA",         "JERSEY CITY",  "http://stream0.wfmu.org/freeform-128k.mp3"},
  // dublab_b = 128k (the _a is 192k with ~12% delivery headroom from LA — lags)
  {"DUBLAB",    "USA",         "LOS ANGELES",  "http://dublab.out.airtime.pro:8000/dublab_b"},
  {"NTS",       "UK",          "LONDON",       "http://stream-relay-geo.ntslive.net/stream"},
  {"CLASSIC",   "UK",          "LONDON",       "http://media-the.musicradio.com:80/ClassicFMMP3"},
  {"FIP",       "France",      "PARIS",        "http://icecast.radiofrance.fr/fip-midfi.mp3"},
  // NOVA cut: inline geo-targeted ads (Tycho). FIP + CULTURE carry Paris.
  {"JAZZ",      "Suisse",      "BERN",         "http://stream.srg-ssr.ch/m/rsj/mp3_128"},
  {"SEVILLANAS","Espana",      "SEVILLA",      "http://radio.wesped.com:8000/stream"},
  // Relaxed talk radios (Tycho's ask) — all verified ≥30% delivery headroom:
  {"CULTURE",   "France",      "PARIS",        "https://icecast.radiofrance.fr/franceculture-midfi.mp3"},
  {"TRE",       "Italia",      "ROMA",         "https://icestreaming.rai.it/3.mp3"},
  {"WORLD",     "UK",          "LONDON",       "http://stream.live.vc.bbcmedia.co.uk/bbc_world_service"},
  {"RN",        "Australia",   "SYDNEY",       "https://live-radio01.mediahubaustralia.com/2RNW/mp3/"},
  {"SHONAN",    "Japan",       "ZUSHI",        "http://shonanbeachfm.out.airtime.pro:8000/shonanbeachfm_a"},
  {"BYLGJAN",   "Iceland",     "REYKJAVIK",    "http://icecast.365net.is:8000/orbbylgjan.aac"},
  {"TBILISI",   "Georgia",     "TBILISI",      "http://iis.ge:8000/radiotbilisi.mp3"},
  {"MOSAIQUE",  "Tunisia",     "TUNIS",        "https://radio.mosaiquefm.net/mosalive"},
  {"MEDINA",    "Morocco",     "MARRAKECH",    "https://cast5.my-control-panel.com/proxy/marrakec/stream"},
  {"DAKAR",     "Senegal",     "DAKAR",        "http://listen.senemultimedia.net:8090/stream"},
  {"AMALIA",    "Portugal",    "LISBOA",       "http://centova.radios.pt:9496/stream"},
  {"2X4",       "Argentina",   "BUENOS AIRES", "https://media.radios.ar:9270/"},
  {"BATUTA",    "Brasil",      "SAO PAULO",    "http://radioims.out.airtime.pro:8000/radioims_a"},
  {"OKAPI",     "Congo",       "KINSHASA",     "http://rs1.radiostreamer.com:8000/;"},
  {"AKABOOZI",  "Uganda",      "KAMPALA",      "http://162.244.80.52:8732/stream"},
  {"AL BAL",    "Lebanon",     "BEIRUT",       "https://albal-lbnet2.radioca.st/stream"},
  {"TOKYO",     "Japan",       "TOKYO",        "https://freefm80.radioca.st/"},
  {"UNAM",      "Mexico",      "MEXICO DF",    "https://tv.radiohosting.online:9484/stream"},
  {"UNIDAS",    "Guatemala",   "GUATEMALA",    "https://stream.zenolive.com/z96sq8tndseuv"},
  {"SALSA",     "Puerto Rico", "SAN JUAN",     "https://cast4.my-control-panel.com/proxy/elpozosalsa/;"},
  {"BOLEROS",   "Peru",        "LIMA",         "https://stream.zeno.fm/5t45zksv7mruv"},
  {"PICHINCHA", "Ecuador",     "QUITO",        "https://icecast.radiopichincha.com/radiopichincha"},
  // 3RRR cut: laggy long-haul + "weird talking radio" (Tycho). RN keeps AU.
};
const int STATION_COUNT = sizeof(STATIONS) / sizeof(STATIONS[0]);
int currentStation = 0;   // randomized in setup() — shuffle on every boot

// ── Browse (split-screen combo picker) ───────────────────
// Left column = radio (OFF + stations), right = soundscape (NONE + catalog).
// Tap toggles the active column, rotate flicks through it, hold exits.
int      browseCol = 0;                   // 0 = radio column, 1 = scape column
int      browseRadio = 1;                 // 0 = OFF, else 1 + station index
int      browseBed = 0;                   // 0 = NONE, else bed index
uint32_t browseConnectAtMs = 0;           // debounced radio connect while browsing

// ── Ambience mix (radio + place) ─────────────────────────
// A soundscape bed mixed UNDER the live radio — "listening to a radio in
// that country". Beds are pre-mixed on the Mac (soundscapes/mix_presets.py),
// stored as headerless IMA-ADPCM mono @16kHz on FFat, streamed into a ring
// by the audio task and added to the decoded stream inside the lib's
// audio_process_raw_samples() hook (pre-volume, pre-EQ: the bed follows the
// volume knob and the tone preset). The same hook runs the slow auto-gain
// that levels out wildly different station loudness.
// THE soundscape catalog — one list, two uses: layered under the radio, or
// standalone (radio off → a tiny silent MP3 keeps the pipeline running and
// the bed plays through the same mixer at full level). Every station change
// rolls a random one (sometimes none). No fixed setting by design.
struct MixPreset { const char* name; const char* place; const char* file; float level; };
const MixPreset MIXES[] = {
  {"SAILING", "OPEN SEA",     "/mix_waves.ima",   0.45f},
  {"SEASIDE", "GULL COAST",   "/mix_seaside.ima", 0.45f},
  {"RAINY",   "SLOW MORNING", "/mix_rainy.ima",   0.40f},
  {"THUNDER", "FAR AWAY",     "/mix_thunder.ima", 0.45f},
  {"WIND",    "OPEN MOOR",    "/mix_wind.ima",    0.42f},
  {"JUNGLE",  "CANOPY",       "/mix_jungle.ima",  0.42f},
  {"GARDEN",  "MARCH GARDEN", "/mix_garden.ima",  0.42f},
  {"CITY",    "KATHMANDU",    "/mix_city.ima",    0.45f},
  {"BARISTA", "CORNER CAFE",  "/mix_barista.ima", 0.40f},
  {"STREET",  "MARKET DAY",   "/mix_street.ima",  0.40f},
  {"TRAIN",   "SLEEPER CAR",  "/mix_train.ima",   0.42f},
};
const int MIX_COUNT = sizeof(MIXES) / sizeof(MIXES[0]);
volatile int mixAutoSel = 0;              // the bed rolled for this station (0 = none)
// Solo level: 0.85 drove the 3W amp hard enough that a sustained loud bed
// (surf) browned out the flaky USB supply and dropped the whole board off
// the bus. 0.55 is still clearly foreground and leaves power headroom.
#define BED_SOLO_LEVEL 0.55f              // bed level when the radio is off

// Radio-off = pure soundscape: a 20KB silent MP3 loops from FFat as a carrier
// so the audio pipeline (and our mixer hook) keeps running.
bool          radioOff = false;
volatile bool g_carrierRequest = false;   // audio task: play /silence.mp3
// While a bed is rolled, the radio fades in AFTER the place establishes
// itself: stream muted for 5s, then a 2.5s ease-in (Tycho's choreography).
volatile uint32_t g_radioGateMs = 0;      // when the fade-in starts (0 = open)

// activeMixIdx() / rollMixCombo() live next to the mixer code below — putting
// function definitions this early breaks the .ino auto-prototype ordering.

#define AMB_RING 16384                    // 1s of 16kHz PCM, power of two
static int16_t* ambRing = nullptr;        // PSRAM
static uint32_t ambWr = 0, ambRd = 0;     // total-sample counters (mask to ring)
static File     ambFile;
static int      ambOpenFor = 0;           // which mixSel the file belongs to
// IMA-ADPCM decoder state (continuous stream, resets at file start)
static int32_t  imaPred = 0;
static int      imaIdx  = 0;

// AGC: normalize very different station levels. Slow envelope → slow gain.
static float agcEnv = 0.0f, agcGain = 1.0f;
volatile bool g_agcReset = false;         // set on any source change
bool agcOn = true;                        // persisted ("agc")

// FFat cannot serve a bed read and an upload write at once — uploads with an
// active reader produced 0-byte files. The upload handler raises this flag;
// ambService pauses (and scape playback is stopped) for the duration.
volatile bool g_uploadActive = false;

// ── State ────────────────────────────────────────────────
Preferences prefs;

enum UiMode {
  MODE_RADIO,        // gradient + station typography
  MODE_MENU,         // settings: spin through big stretched menu words
  MODE_BROWSE,       // split screen: radio column × soundscape column
  MODE_ALARM_SET,    // rotate = time, press = arm/disarm
  MODE_INFO_NET,     // SSID / IP / signal card
  MODE_INFO_BAT,     // voltage / charge card
  MODE_WIFI_RESET,   // No/Yes confirmation, then wipe + restart
  MODE_OFF_CONFIRM,  // "Turn off?" No/Yes before deep sleep
  MODE_WIFI_HUB,     // offline: SEARCH | SETUP tabs (watcher + captive AP)
};
UiMode uiMode = MODE_RADIO;
bool g_splashPreview = false;   // dev: uiTask renders the boot splash instead
// Which mode the previous frame rendered — screens use this to snap their
// entrance state instead of animating from wherever they last were.
UiMode g_lastRenderMode = MODE_RADIO;

// iPod-style menu. Index meaning is fixed (see menuSelect); the toggle item's
// label is rendered dynamically from alarmArmed.
enum { MI_TOGGLE_ALARM, MI_BROWSE, MI_NETWORK, MI_BATTERY, MI_TURN_OFF, MI_WIFI_RESET, MI_BACK };
const char* MENU_ITEMS[] = {"Alarm", "Browse", "Network", "Battery", "Turn off", "WiFi reset", "Back"};
#define MENU_COUNT 7
int  menuIdx = 0;
bool powerOffYes = false;      // selection on the turn-off confirm screen
bool armedEditVol   = false;   // on the armed clock, tap toggles time ⇄ wake volume
bool wifiResetYes = false;          // selection on the confirm screen
uint32_t uiLastInputMs = 0;         // mode-timeout bookkeeping

int  volumeLevel = 12;             // 0..21 (mirrors audio.setVolume)
int  alarmHour   = 8;
int  alarmMinute = 0;
bool alarmArmed  = false;

// ── Tone / EQ ────────────────────────────────────────────
// audio.setTone() is a 3-band parametric EQ: low-shelf @500Hz, peaking @1800Hz
// (the presence/voice band), high-shelf @6000Hz, each ±12dB. The library
// pre-attenuates the whole signal by the largest *boost* to avoid clipping, so
// a cut is "free" loudness but a boost costs it. These presets lean on cuts and
// a modest 1800Hz presence bump — the tiny speaker can't reproduce deep bass, so
// a big low-shelf boost would only throw away level for rumble you never hear.
// This little driver has a harsh, peaky midrange/treble ("shouts at the top"),
// so every usable preset CUTS the high shelf — boosting 1800/6000Hz on it is
// physically painful. Warmth (low shelf) is cheap and pleasant; the top gets
// tamed, not lifted. Flat is kept only as a bright reference.
// The tone is DUSK, full stop — Tycho auditioned two generations of presets
// (Warm/Balanced/Voice/Flat, then five Mellow variants) and landed here:
// warm low shelf, recessed mids, maximum tamed top. No picker anymore.
#define TONE_LOW   6.0f
#define TONE_PEAK -4.0f
#define TONE_HIGH -10.0f
bool loudnessComp = true;   // Fletcher-Munson: lift warmth when quiet

// Loading: true from connect-request until the decoder reports a bitrate.
volatile bool g_loading        = true;
volatile bool g_connectRequest = false;   // audio task picks this up
volatile bool g_streamEof      = false;   // set from the audio callback
uint32_t connectStartMs = 0;
int      retryAttempts  = 0;
#define STREAM_VALIDATE_MS 8000    // silent for 8s → try the next station
#define MAX_RETRY_ATTEMPTS 8
// connecttohost() told us outright the host is dead (DNS/TCP/TLS/HTTP error).
// No need to sit out the validate window — hop on after a short dwell.
volatile bool g_connectFailed = false;
#define CONNECT_FAIL_DWELL_MS 1200

// Alarm fade-in (globo-eink): 20s from silence to the saved volume.
bool     alarmFading  = false;
uint32_t fadeStartMs  = 0;
#define FADE_DURATION_MS 20000

// Arming the alarm silences the radio: fade the current stream out (3s), stop
// it, and wait quietly until the alarm time — then start a fresh station and
// fade in. alarmSilenced = armed & waiting (stream stopped).
bool     alarmSilenced  = false;
bool     alarmFadeOut   = false;   // ramping volume down before the stop
uint32_t fadeOutStartMs = 0;
int      alarmWakeVol   = 12;      // volume to fade up to when the alarm fires
#define FADE_OUT_MS 3000
volatile bool g_stopRequest = false;   // audio task: stop the current stream
bool     alarmPending   = false;   // woke from deep sleep for the alarm — don't re-sleep

// iPod menu labels: the toggle item shows its state, everything else is static.
// The alarm item states its state quietly (Braun, not shouting): armed shows
// the actual wake time, disarmed reads "Alarm off". Tap still toggles.
// Battery (tPod): EMA-smoothed VBAT, polled ~1Hz. (Lives above menuLabel —
// the label prints the percentage.)
static int   g_batPct = -1;
static float g_batMvEma = 0.0f;

const char* menuLabel(int idx) {
  static char buf[16];
  if (idx == MI_TOGGLE_ALARM) {
    if (alarmArmed) snprintf(buf, sizeof(buf), "Alarm %d:%02d", alarmHour, alarmMinute);
    else            snprintf(buf, sizeof(buf), "Alarm off");
    return buf;
  }
  if (idx == MI_NETWORK) {  // says what it IS; the address appears on arrival
    if (WiFi.status() == WL_CONNECTED) return "Remote control";
    return (wwState == WW_SCANNING || wwState == WW_JOINING) ? "Searching" : "Offline";
  }
  if (idx == MI_BATTERY && batteryPresent()) {
    static char bat[16];
    snprintf(bat, sizeof(bat), "Battery %d%%", g_batPct);
    return bat;
  }
  return MENU_ITEMS[idx];
}

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
static const uint32_t BL_FADE_UP_MS   = 220;   // wake must feel instant; the
                                               // slow part belongs to sleep
static const uint32_t BL_FADE_DOWN_MS = 700;


// ── Themes ───────────────────────────────────────────────
// Two visual templates for the now-playing identity. FLOW is the original
// drifting metaball gradient. POSTER is typography-first: a flat curated
// color field (film grain kept — it reads as print), edge-to-edge type in
// paired ink colors, like a type-foundry specimen poster. Each station hop
// rerolls the poster combo just like it rerolls the gradient palette.
// System screens (menu/info/alarm) always stay on the FLOW gradient.
// POSTER is the theme — Tycho's verdict. The FLOW gradient code stays in the
// tree (splash/menu could revive it) but there is no picker, no pref.
enum Theme { THEME_FLOW = 0, THEME_POSTER = 1 };
const int theme = THEME_POSTER;

struct PosterCombo {
  const char* name;
  uint8_t bg[3], ink[3], ink2[3];   // field, station name, city
};
const PosterCombo POSTER_COMBOS[] = {
  {"Electric", { 35,  80, 255}, { 12,  12,  16}, {246, 244, 238}},  // cobalt / black / white
  {"Acid",     {185, 228,  48}, { 15,  15,  20}, { 35,  80, 255}},  // acid green / black / blue
  {"Blush",    {255, 203, 208}, {232,  72,  22}, {180,  45,  15}},  // pale pink / orange-red
  {"Signal",   {228,  30,  25}, { 12,  10,  12}, {250, 245, 235}},  // red / black / white
  {"Noir",     { 10,  10,  13}, {248, 246, 240}, {135, 135, 140}},  // black / white / grey
  {"Ultra",    {  9,   9,  11}, {255, 122,  30}, {216,  62, 200}},  // black / orange / magenta
  {"Cream",    {243, 236, 218}, { 28,  38,  95}, {210,  60,  35}},  // cream / navy / red
  {"Butter",   { 28,  48, 165}, {255, 210,  60}, {242, 240, 232}},  // cobalt / butter / white
};
#define POSTER_COMBO_COUNT (int)(sizeof(POSTER_COMBOS) / sizeof(POSTER_COMBOS[0]))
int posterIdx = 0;

// Frame-wide ink palette, set at the top of renderFrame: every screen (menu,
// sound, info cards, overlays) draws with these so the whole UI follows the
// theme. FLOW = white/grey on gradient; POSTER = the combo's paired inks.
uint16_t g_inkPri = TFT_WHITE;   // primary ink (titles, selected, values)
uint16_t g_inkSec = TFT_WHITE;   // secondary ink (city line)
uint16_t g_inkDim = 0x9CD3;      // dimmed ink (hints, back arrows)

// Each theme also has its own typographic voice for the UI chrome: FLOW uses
// the humanist sans, POSTER the condensed grotesque (Roboto Condensed Black —
// same family as the display faces, generated by tools/genfont.py).
const GFXfont* uiFontBig()   { return theme == THEME_POSTER ? &RCUI_Black18pt7b  : &FreeSansBold18pt7b; }
const GFXfont* uiFontRow()   { return theme == THEME_POSTER ? &RCUI_Black12pt7b  : &FreeSansBold12pt7b; }
const GFXfont* uiFontLabel() { return theme == THEME_POSTER ? &RCUI_Black9pt7b   : &FreeSansBold9pt7b; }
const GFXfont* uiFontBody()  { return theme == THEME_POSTER ? &RCUI_Medium9pt7b  : &FreeSans9pt7b; }

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
  // Poster theme rerolls its color combo on every station hop, same gesture
  // as the gradient palette reroll (never the same combo twice in a row).
  if (POSTER_COMBO_COUNT > 1) {
    int next;
    do { next = random(POSTER_COMBO_COUNT); } while (next == posterIdx);
    posterIdx = next;
  }

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
    nextRetargetMs = millis() + 1500 + random(2500);   // recolour often so it visibly flows
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
// Two-pass: the 5-blob field (divisions, ^4 falloff) is evaluated on a coarse
// grid — one sample per 4x4 pixel block — then bilinearly reconstructed at
// 2x2-block resolution and written out with per-pixel film grain. The blobs
// are 130-225px soft metaballs, so a 4px field sample is visually identical
// to the old per-2x2 evaluation, at ~1/4 the blob math. Grain comes from a
// precomputed 64x64 tile (same hash, table lookup instead of 3 muls/pixel)
// and the ±grain clamp is a LUT instead of three constrain() branches.
#define GRID_X (HALF_W / 2 + 1)   // 81  coarse samples per row
#define GRID_Y (HALF_H / 2 + 2)   // 44  coarse rows (covers odd HALF_H)
static uint8_t gridR[GRID_Y * GRID_X], gridG[GRID_Y * GRID_X], gridB[GRID_Y * GRID_X];

// Shared by the gradient and poster background renderers.
static uint8_t grainT[64 * 64];
static uint8_t clampT[272];        // index = value + 8, clamped to 0..255
static void ensureRenderLuts() {
  static bool ready = false;
  if (ready) return;
  ready = true;
  for (int y = 0; y < 64; y++)
    for (int x = 0; x < 64; x++)
      grainT[y * 64 + x] = (uint8_t)(grain(x, y) + 8);     // 0..15
  for (int i = 0; i < 272; i++)
    clampT[i] = (uint8_t)constrain(i - 8, 0, 255);
}

// POSTER background: a flat curated color field with the same film grain —
// riso-print flat, no vignette, no motion. Much cheaper than the blob field.
void renderPosterBg() {
  ensureRenderLuts();
  const PosterCombo& c = POSTER_COMBOS[posterIdx];
  uint16_t* fb = (uint16_t*)spr.getPointer();
  for (int y = 0; y < SH; y++) {
    const uint8_t* gt = grainT + (y & 63) * 64;
    uint16_t* row = fb + y * SW;
    for (int x = 0; x < SW; x++) {
      int n = gt[x & 63];
      row[x] = swap16(((clampT[c.bg[0] + n] & 0xF8) << 8) |
                      ((clampT[c.bg[1] + n] & 0xFC) << 3) |
                       (clampT[c.bg[2] + n] >> 3));
    }
  }
}

void renderGradient() {
  ensureRenderLuts();
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

  // Pass 1: blob field + vignette on the coarse grid.
  for (int gy = 0; gy < GRID_Y; gy++) {
    int fy = gy * 4; if (fy > SH - 1) fy = SH - 1;
    uint8_t* gr = gridR + gy * GRID_X;
    uint8_t* gg = gridG + gy * GRID_X;
    uint8_t* gb = gridB + gy * GRID_X;
    for (int gx = 0; gx < GRID_X; gx++) {
      int fx = gx * 4; if (fx > SW - 1) fx = SW - 1;

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
      gr[gx] = (uint8_t)(baseR * (256 - dark) >> 8);
      gg[gx] = (uint8_t)(baseG * (256 - dark) >> 8);
      gb[gx] = (uint8_t)(baseB * (256 - dark) >> 8);
    }
  }

  // Pass 2: bilinear reconstruction per 2x2 block + per-pixel grain.
  for (int hy = 0; hy < HALF_H; hy++) {
    int fy = hy * 2;
    int gy = hy >> 1;
    bool oy = hy & 1;
    const uint8_t* r0R = gridR + gy * GRID_X; const uint8_t* r1R = r0R + GRID_X;
    const uint8_t* r0G = gridG + gy * GRID_X; const uint8_t* r1G = r0G + GRID_X;
    const uint8_t* r0B = gridB + gy * GRID_X; const uint8_t* r1B = r0B + GRID_X;
    const uint8_t* gt0 = grainT + ((fy    ) & 63) * 64;
    const uint8_t* gt1 = grainT + ((fy + 1) & 63) * 64;

    for (int hx = 0; hx < HALF_W; hx++) {
      int fx = hx * 2;
      int gx = hx >> 1;
      int baseR, baseG, baseB;
      if (!oy) {
        if (!(hx & 1)) { baseR = r0R[gx];                    baseG = r0G[gx];                    baseB = r0B[gx]; }
        else           { baseR = (r0R[gx] + r0R[gx+1]) >> 1; baseG = (r0G[gx] + r0G[gx+1]) >> 1; baseB = (r0B[gx] + r0B[gx+1]) >> 1; }
      } else {
        if (!(hx & 1)) { baseR = (r0R[gx] + r1R[gx]) >> 1;   baseG = (r0G[gx] + r1G[gx]) >> 1;   baseB = (r0B[gx] + r1B[gx]) >> 1; }
        else           { baseR = (r0R[gx] + r0R[gx+1] + r1R[gx] + r1R[gx+1]) >> 2;
                         baseG = (r0G[gx] + r0G[gx+1] + r1G[gx] + r1G[gx+1]) >> 2;
                         baseB = (r0B[gx] + r0B[gx+1] + r1B[gx] + r1B[gx+1]) >> 2; }
      }

      uint16_t* row0 = fb + fy * SW + fx;
      uint16_t* row1 = row0 + SW;
      int n;
      n = gt0[ fx      & 63];
      row0[0] = swap16(((clampT[baseR + n] & 0xF8) << 8) | ((clampT[baseG + n] & 0xFC) << 3) | (clampT[baseB + n] >> 3));
      n = gt0[(fx + 1) & 63];
      row0[1] = swap16(((clampT[baseR + n] & 0xF8) << 8) | ((clampT[baseG + n] & 0xFC) << 3) | (clampT[baseB + n] >> 3));
      n = gt1[ fx      & 63];
      row1[0] = swap16(((clampT[baseR + n] & 0xF8) << 8) | ((clampT[baseG + n] & 0xFC) << 3) | (clampT[baseB + n] >> 3));
      n = gt1[(fx + 1) & 63];
      row1[1] = swap16(((clampT[baseR + n] & 0xF8) << 8) | ((clampT[baseG + n] & 0xFC) << 3) | (clampT[baseB + n] >> 3));
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
// globalAlpha fades the whole blit (0..255) — overlay fade-out, name fade-in.
// covOut (destW*destH bytes, caller-zeroed) captures per-pixel coverage so the
// same stretch can be replayed later without re-area-sampling (see BlitCache).
int blitMask(TFT_eSprite &mask, int inkX, int inkY, int inkW, int inkH,
             int destX, int destY, int destW, int destH, uint16_t color,
             uint8_t globalAlpha = 255, uint8_t* covOut = nullptr) {
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

      int a = inkAcc >= totAcc ? 255 : (int)((uint64_t)inkAcc * 255 / totAcc);
      if (covOut) covOut[dy * destW + dx] = (uint8_t)a;
      if (globalAlpha < 255) a = a * globalAlpha / 255;

      if (a >= 255) {
        out[px] = col;            // fully covered — solid text color
      } else if (a > 0) {
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

// ── Cached stretch replay ────────────────────────────────
// The radio screen re-area-samples the same two stretched masks every single
// frame even though the stretch is fixed per station — that was ~35ms/frame.
// Instead, the first frame after a station change captures per-pixel coverage
// (via blitMask covOut) into a PSRAM buffer; every later frame just alpha-
// blends that coverage over the fresh gradient. Same pixels, ~4x cheaper.
// (struct BlitCache lives with the display globals for .ino prototype order.)
int blitCached(BlitCache& bc, TFT_eSprite& mask, int inkX, int inkY, int inkW, int inkH,
               int destX, int destY, int destW, int destH, uint16_t color, uint8_t globalAlpha) {
  bool hit = bc.station == activeKey() && bc.cov &&
             bc.x == destX && bc.y == destY && bc.w == destW && bc.h == destH;
  if (!hit) {
    if (destW <= 0 || destH <= 0) return -1;
    size_t need = (size_t)destW * destH;
    bc.cov = (uint8_t*)heap_caps_realloc(bc.cov, need, MALLOC_CAP_SPIRAM);
    if (!bc.cov) {   // PSRAM exhausted (shouldn't happen) — draw uncached
      bc.station = -1;
      return blitMask(mask, inkX, inkY, inkW, inkH, destX, destY, destW, destH, color, globalAlpha);
    }
    memset(bc.cov, 0, need);
    bc.station = activeKey();
    bc.x = destX; bc.y = destY; bc.w = destW; bc.h = destH;
    return blitMask(mask, inkX, inkY, inkW, inkH, destX, destY, destW, destH, color,
                    globalAlpha, bc.cov);
  }

  // Fast path: replay coverage over the current gradient.
  uint16_t* fb = (uint16_t*)spr.getPointer();
  uint16_t col = swap16(color);
  int tr = ((color >> 11) & 0x1F) << 3;
  int tg = ((color >>  5) & 0x3F) << 2;
  int tb = ( color        & 0x1F) << 3;
  int painted = 0;
  for (int dy = 0; dy < bc.h; dy++) {
    int py = bc.y + dy;
    if (py < 0 || py >= SH) continue;
    const uint8_t* crow = bc.cov + (size_t)dy * bc.w;
    uint16_t* out = fb + py * SW;
    for (int dx = 0; dx < bc.w; dx++) {
      int a = crow[dx];
      if (!a) continue;
      painted++;
      if (globalAlpha < 255) a = a * globalAlpha / 255;
      int px = bc.x + dx;
      if (px < 0 || px >= SW) continue;
      if (a >= 255) { out[px] = col; continue; }
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
  return painted;
}

// Randomized layout params per station
int layoutPad;     // edge padding
int layoutGap;     // gap between lines
int layoutNamePct; // name gets this % of total height

uint32_t g_masksReadyMs = 0;   // station typography fade-in starts here

// The active identity: the soundscape when the radio is off, else the station.
// activeKey() feeds every cache keyed on "what identity is on screen".
const char* activeName() {
  if (!radioOff) return STATIONS[currentStation].name;
  return mixAutoSel > 0 ? MIXES[mixAutoSel - 1].name : "QUIET";
}
const char* activeCity() {
  if (!radioOff) return STATIONS[currentStation].city;
  return mixAutoSel > 0 ? MIXES[mixAutoSel - 1].place : "";
}
int activeKey() { return radioOff ? 2000 + mixAutoSel : currentStation; }

void cacheTextMasks() {
  if (cachedStation == activeKey()) return;
  cachedStation = activeKey();
  g_masksReadyMs = millis();

  // Layout first, fonts second: the old pad range (50-70) could squeeze the
  // name into a ~15px band where it read as a smear — "the name never shows".
  // POSTER runs the type nearly edge-to-edge (specimen-poster crammed);
  // FLOW keeps the airier padding that suits the gradient.
  if (theme == THEME_POSTER) {
    layoutPad = random(8, 18);
    layoutGap = random(2, 8);
    layoutNamePct = random(52, 70);
  } else {
    layoutPad = random(24, 44);
    layoutGap = random(4, 10);
    layoutNamePct = random(48, 68);
  }

  int totalH = SH - layoutPad * 2;
  int nameH = totalH * layoutNamePct / 100;
  int cityH = totalH - nameH - layoutGap;

  // Light weights vanish below ~45px of height; bias to the heavy faces then.
  static const int HEAVY[] = {0, 1, 3, 5};   // Black/Medium cuts
  int nameFont = (nameH < 45) ? HEAVY[random(4)] : random(NUM_FONTS);
  int cityFont = (cityH < 30) ? HEAVY[random(4)] : random(NUM_FONTS);

  renderTextMask(activeName(), fonts56[nameFont],
                 nameMask, nameInkX, nameInkY, nameInkW, nameInkH);
  renderTextMask(activeCity(), fonts32[cityFont],
                 cityMask, cityInkX, cityInkY, cityInkW, cityInkH);
  Serial.printf("[typo] '%s' f=%d ink=%dx%d | '%s' f=%d ink=%dx%d | pad=%d nameH=%d cityH=%d\n",
                activeName(), nameFont, nameInkW, nameInkH,
                activeCity(), cityFont, cityInkW, cityInkH,
                layoutPad, nameH, cityH);
}

// Overlay text (volume / alarm time), re-rendered only when the string changes.
void cacheOverlayMask(const char* str) {
  if (strcmp(str, ovCachedStr) == 0) return;
  strncpy(ovCachedStr, str, sizeof(ovCachedStr) - 1);
  renderTextMask(str, fonts56[0], ovMask, ovInkX, ovInkY, ovInkW, ovInkH);
}

// Second overlay slot: the confirm screens set their title as TWO stretched
// display lines, so they need two cached masks at once.
TFT_eSprite ov2Mask = TFT_eSprite(&tft);
int ov2InkX, ov2InkY, ov2InkW, ov2InkH;
char ov2CachedStr[16] = "";
void cacheOverlay2Mask(const char* str) {
  if (strcmp(str, ov2CachedStr) == 0) return;
  strncpy(ov2CachedStr, str, sizeof(ov2CachedStr) - 1);
  renderTextMask(str, fonts56[0], ov2Mask, ov2InkX, ov2InkY, ov2InkW, ov2InkH);
}

// ── Small UI bits ────────────────────────────────────────
void drawBatteryIcon(int x, int y, int pct) {
  if (pct < 0) return;
  int w = 20, h = 10;
  spr.drawRoundRect(x, y, w, h, 2, g_inkPri);
  spr.fillRect(x + w, y + 3, 2, 4, g_inkPri);
  int fillW = ((w - 4) * pct + 50) / 100;
  if (fillW > 0) spr.fillRect(x + 2, y + 2, fillW, h - 4, g_inkPri);
  // Rail at charge voltage = USB is feeding the cell: a small bolt cut out
  // of the fill says "charging" without pretending to know the real SoC.
  if (g_batMvEma >= 4280.0f) {
    const PosterCombo& c = POSTER_COMBOS[posterIdx];
    uint16_t bg = spr.color565(c.bg[0], c.bg[1], c.bg[2]);
    spr.fillTriangle(x + 11, y + 1, x + 7, y + 6, x + 10, y + 6, bg);
    spr.fillTriangle(x + 9,  y + 9, x + 13, y + 4, x + 10, y + 4, bg);
  }
  // The number does the talking — icon fill alone was too quiet a warning.
  char b[8]; snprintf(b, sizeof(b), "%d%%", pct);
  spr.setTextDatum(TR_DATUM);
  spr.setFreeFont(uiFontLabel());
  spr.setTextColor(g_inkPri);
  spr.drawString(b, x - 5, y - 1);
}

// ── Loading edge glow ────────────────────────────────────
// "Something is happening" the console way: while a stream is tuning, a
// luminous comet sweeps around the screen border, additively brightening
// the gradient underneath — no chrome, works on every palette. Intensity
// eases in and out so it never pops; it just dissolves when audio is live.
static float loadVis = 0.0f;
static bool  g_edgeDark = false;   // light poster fields glow dark, not white

static inline void edgeBrighten(int x, int y, float a) {
  uint16_t* fb = (uint16_t*)spr.getPointer();
  uint16_t c = swap16(fb[y * SW + x]);
  int r = ((c >> 11) & 0x1F) << 3;
  int g = ((c >>  5) & 0x3F) << 2;
  int b = ( c        & 0x1F) << 3;
  if (g_edgeDark) {
    r -= (int)(r * a);
    g -= (int)(g * a);
    b -= (int)(b * a);
  } else {
    r += (int)((255 - r) * a);
    g += (int)((255 - g) * a);
    b += (int)((255 - b) * a);
  }
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
// A LiPo reads a plausible cell voltage; on USB with no cell the VBAT pin sits
// well above the LiPo ceiling (~4.8V), so hide the Battery item then.
// The ceiling is 4550, NOT 4400: a cell that is CHARGING clamps the rail to
// ~4.35-4.45V, and the old 4400 cutoff made a charging battery read as "no
// battery" — icon and menu % vanished exactly when plugged in (Tycho's bug).
bool batteryPresent() { return g_batMvEma > 2500.0f && g_batMvEma < 4550.0f; }
bool menuVisible(int idx) { return idx != MI_BATTERY || batteryPresent(); }

// Step the selection to the next visible item; clamps at the ends (no wrap) so
// nothing scrolls past the first/last item.
void menuStep(int delta) {
  int dir = delta > 0 ? 1 : -1;
  for (int s = 0; s < abs(delta); s++) {
    int i = menuIdx;
    while (true) {
      int ni = i + dir;
      if (ni < 0 || ni >= MENU_COUNT) return;   // at an end
      i = ni;
      if (menuVisible(i)) { menuIdx = i; break; }
    }
  }
}


// Dimmed uppercase "BACK" with a small left-arrow icon, centred at row y.
void drawBackHint(int y) {
  spr.setFreeFont(uiFontLabel());
  spr.setTextDatum(ML_DATUM);
  const int aw = 16;                         // arrow + gap
  int tw = spr.textWidth("BACK");
  int x  = (SW - (aw + tw)) / 2;
  spr.fillTriangle(x, y, x + 9, y - 6, x + 9, y + 6, g_inkDim);   // ◄
  spr.setTextColor(g_inkDim);
  spr.drawString("BACK", x + aw, y);
}

// Info screens live on the themed field like everything else — themed inks,
// no white card. Title big-and-centred, values below, a dim BACK hint at foot.
void drawInfoScreen(const char* title, const String& l1, const String& l2, const String& l3) {
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(g_inkPri);
  spr.setFreeFont(uiFontRow());
  spr.drawString(title, SW / 2, 20);
  spr.setFreeFont(uiFontBody());
  spr.drawString(l1, SW / 2, 62);
  spr.drawString(l2, SW / 2, 88);
  spr.drawString(l3, SW / 2, 114);
  drawBackHint(152);
}

// Pressing "globo.local" in the menu lands here: a scannable QR of the web
// remote — point the phone, get the controls. globo.local is THE address;
// the raw IP hides behind a knob turn for when mDNS lets you down. QR stays
// dark-on-white regardless of theme (scanners need contrast + quiet zone).
bool g_netShowIp = false;

void drawNetworkCard() {
  if (WiFi.status() != WL_CONNECTED) {
    // Live watcher status, not a shrug. The card animates through the search
    // cycle (searching → joining → countdown to the next pass) so on the road
    // you can SEE the device looking for your hotspot — and click to skip the
    // wait. Only the no-networks-saved case still points at WiFi reset.
    static const char* dots[4] = {"", ".", "..", "..."};
    String l1, l2;
    if (wwState == WW_SCANNING) {
      l1 = String("searching") + dots[(millis() / 350) % 4];
      l2 = String(g_savedNetCount) + " remembered network" + (g_savedNetCount == 1 ? "" : "s");
    } else if (wwState == WW_JOINING) {
      l1 = "joining " + wwSsid + dots[(millis() / 350) % 4];
      l2 = "";
    } else if (g_savedNetCount <= 0) {
      l1 = "no networks saved";
      l2 = "WiFi reset sets one up";
    } else {
      l1 = wwLastResult[0] ? wwLastResult : "offline";
      int32_t waitMs = (int32_t)(wwNextMs - millis());
      l2 = (wwState == WW_WAIT && waitMs > 0)
             ? "next search in " + String((waitMs + 999) / 1000) + "s"
             : String(g_savedNetCount) + " remembered network" + (g_savedNetCount == 1 ? "" : "s");
    }
    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(g_inkPri);
    spr.setFreeFont(uiFontRow());
    spr.drawString("NETWORK", SW / 2, 20);
    spr.setFreeFont(uiFontBody());
    spr.drawString(l1, SW / 2, 62);
    spr.drawString(l2, SW / 2, 88);
    drawTextAlpha(g_savedNetCount > 0 ? "click: search now   hold: back"
                                      : "click: back",
                  uiFontBody(), 148, 150, g_inkDim);
    return;
  }
  String url = g_netShowIp ? "http://" + WiFi.localIP().toString()
                           : "http://globo.local";

  esp_qrcode_config_t cfg{};
  cfg.display_func = qrDisplayCb;
  cfg.max_qrcode_version = 3;
  cfg.qrcode_ecc_level = ESP_QRCODE_ECC_LOW;
  g_qrMargin = 8;
  esp_qrcode_generate(&cfg, url.c_str());   // draws card + code, sets g_qrX0

  // Just the address and the code — no label, no narration (Tycho).
  int cx = (g_qrX0 - g_qrMargin) / 2;   // text column left of the code
  if (g_netShowIp) {
    drawTextAlpha(WiFi.localIP().toString().c_str(), uiFontRow(), 62, 255, g_inkPri, cx);
    drawTextAlpha("turn for url", uiFontBody(), 94, 150, g_inkDim, cx);
  } else {
    drawTextAlpha("globo.local", uiFontRow(), 62, 255, g_inkPri, cx);
    drawTextAlpha("turn for ip", uiFontBody(), 94, 150, g_inkDim, cx);
  }
  drawTextAlpha("click: back", uiFontBody(), 122, 140, g_inkDim, cx);
}

// ── The offline hub screen: SEARCH | SETUP ───────────────
// Two tabs on distinct poster fields (renderFrame swaps the combo). SEARCH
// shows the hunt — status line plus the remembered networks fading past one
// by one (empty when nothing is saved, by design). SETUP is the join-QR for
// the Globo-Setup AP with its captive setup page. Tap or turn flips the tab;
// hold reaches TURN OFF (the only other place you can go while offline).
void drawWifiHub() {
  spr.setTextDatum(TL_DATUM);
  spr.setFreeFont(uiFontLabel());
  int x = 18, y = 24;
  spr.setTextColor(hubTab == 0 ? g_inkPri : g_inkDim);
  spr.drawString("SEARCH", x, y - 10);
  int w1 = spr.textWidth("SEARCH");
  spr.setTextColor(hubTab == 1 ? g_inkPri : g_inkDim);
  spr.drawString("SETUP", x + w1 + 20, y - 10);
  int ux = hubTab ? x + w1 + 20 : x;
  int uw = hubTab ? spr.textWidth("SETUP") : w1;
  spr.fillRect(ux, y + 12, uw, 3, g_inkPri);   // active-tab underline

  if (hubTab == 1) {
    // SETUP: the ask IS the poster — "SCAN / ME IN" in stretched display
    // type beside the QR; the AP name (or live join feedback) underneath.
    String wifiURI = "WIFI:T:nopass;S:" + String(WIFI_AP_NAME) + ";;";
    esp_qrcode_config_t cfg{};
    cfg.display_func = qrDisplayCb;
    cfg.max_qrcode_version = 3;
    cfg.qrcode_ecc_level = ESP_QRCODE_ECC_LOW;
    g_qrMargin = 8;
    esp_qrcode_generate(&cfg, wifiURI.c_str());
    int bw = g_qrX0 - g_qrMargin - 30;   // left column width
    cacheOverlayMask("SCAN");
    blitMask(ovMask, ovInkX, ovInkY, ovInkW, ovInkH, 16, 36, bw, 44, g_inkPri);
    cacheOverlay2Mask("ME IN");
    blitMask(ov2Mask, ov2InkX, ov2InkY, ov2InkW, ov2InkH, 16, 86, bw, 44, g_inkPri);
    int cx = (g_qrX0 - g_qrMargin) / 2;
    bool phone = WiFi.softAPgetStationNum() > 0;
    drawTextAlpha(phone ? "phone connected" : WIFI_AP_NAME,
                  uiFontBody(), 150, 200, g_inkSec, cx);
  } else {
    // SEARCH: "LOOKING / FOR WIFI" edge to edge, the hunt itself as a soft
    // line underneath — joining status or the remembered names ticking past.
    // Nothing saved → nothing to look for → the field stays empty (by design).
    int n = min(g_savedNetCount, 8);
    if (n > 0) {
      cacheOverlayMask("LOOKING");
      blitMask(ovMask, ovInkX, ovInkY, ovInkW, ovInkH, 24, 32, SW - 48, 46, g_inkPri);
      cacheOverlay2Mask("FOR WIFI");
      blitMask(ov2Mask, ov2InkX, ov2InkY, ov2InkW, ov2InkH, 24, 84, SW - 48, 46, g_inkPri);
      static const char* dots[4] = {"", ".", "..", "..."};
      if (wwState == WW_JOINING) {
        String st = "joining " + wwSsid + dots[(millis() / 350) % 4];
        drawTextAlpha(st.c_str(), uiFontBody(), 150, 220, g_inkSec);
      } else {
        uint32_t per = 1600, t = millis() % (per * n);
        int idx = (int)(t / per);
        float ph = (t % per) / (float)per;
        uint8_t a = 30 + (uint8_t)(sinf(ph * (float)PI) * 200.0f);
        char nm[33];
        portENTER_CRITICAL(&scanMux);
        memcpy(nm, g_savedSsids[idx], sizeof(nm));
        portEXIT_CRITICAL(&scanMux);
        nm[32] = '\0';
        drawTextAlpha(nm, uiFontBody(), 150, a, g_inkSec);
      }
    }
  }
}

void drawBatteryCard() {
  bool usb = g_batMvEma >= 4300;
  drawInfoScreen("BATTERY",
                 String(g_batMvEma / 1000.0f, 2) + " V",
                 String(g_batPct) + " %",
                 usb ? "USB power" : "On battery");
}


// Themed No/Yes confirmation — destructive actions get a deliberate pause.
// One friendly question set as TWO stretched display lines (the same type as
// a station name): the decision IS the identity of this screen. No subtitle,
// no control narration (Tycho) — just the question and No/Yes; the selected
// answer carries full ink at display size, the other recedes.
void drawConfirmScreen(const char* line1, const char* line2, bool yesSelected) {
  cacheOverlayMask(line1);
  blitMask(ovMask, ovInkX, ovInkY, ovInkW, ovInkH, 24, 12, SW - 48, 52, g_inkPri);
  cacheOverlay2Mask(line2);
  blitMask(ov2Mask, ov2InkX, ov2InkY, ov2InkW, ov2InkH, 24, 70, SW - 48, 52, g_inkPri);

  const int y = 148;
  spr.setTextDatum(MC_DATUM);
  for (int i = 0; i < 2; i++) {
    int cx = SW / 2 + (i == 0 ? -64 : 64);
    bool sel = (i == 1) == yesSelected;
    spr.setFreeFont(sel ? uiFontBig() : uiFontRow());
    spr.setTextColor(sel ? g_inkPri : g_inkDim);
    spr.drawString(i == 0 ? "No" : "Yes", cx, y);
  }
}

// Per-stage render profiling, printed with the [ui] heartbeat line
uint32_t g_gradMs = 0, g_textMs = 0, g_pushMs = 0;
int g_namePx = 0, g_cityPx = 0;   // pixels the last frame actually painted

// Draw white text centred horizontally at row y, at a given opacity, blending
// each glyph pixel with the gradient already in the framebuffer — so the
// gradient shows through at (255-alpha)/255. Used for the dimmed neighbour
// items in the iPod menu (true 50% opacity, not a flat grey).
void drawTextAlpha(const char* s, const GFXfont* font, int y, uint8_t alpha,
                   uint16_t color, int cx) {
  static TFT_eSprite m = TFT_eSprite(&tft);
  static bool inited = false;
  const int MH = 44;
  if (!inited) { m.setColorDepth(8); m.createSprite(SW, MH); inited = true; }
  m.fillSprite(TFT_BLACK);
  m.setFreeFont(font);
  m.setTextColor(TFT_WHITE);
  m.setTextDatum(MC_DATUM);
  m.drawString(s, cx, MH / 2);

  uint8_t*  mb = (uint8_t*)m.getPointer();
  uint16_t* fb = (uint16_t*)spr.getPointer();
  if (!mb || !fb) return;
  int y0 = y - MH / 2;
  for (int row = 0; row < MH; row++) {
    int sy = y0 + row;
    if (sy < 0 || sy >= SH) continue;
    const uint8_t* mrow = mb + row * SW;
    uint16_t* frow = fb + sy * SW;
    for (int x = 0; x < SW; x++) {
      if (mrow[x]) {   // glyph pixel: blend the ink over whatever is underneath
        uint16_t bg = swap16(frow[x]);
        frow[x] = swap16(tft.alphaBlend(alpha, color, bg));
      }
    }
  }
}

// The now-playing identity: cached stretched typography in the given inks.
// Shared by the radio screen and the theme picker's live preview.
void drawStationIdentity(uint16_t inkName, uint16_t inkCity, uint8_t alpha) {
  cacheTextMasks();
  int totalH = SH - layoutPad * 2;
  int nameH = totalH * layoutNamePct / 100;
  int cityH = totalH - nameH - layoutGap;
  g_namePx = blitCached(nameBC, nameMask, nameInkX, nameInkY, nameInkW, nameInkH,
           layoutPad, layoutPad, SW - layoutPad * 2, nameH, inkName, alpha);
  g_cityPx = blitCached(cityBC, cityMask, cityInkX, cityInkY, cityInkW, cityInkH,
           layoutPad, layoutPad + nameH + layoutGap, SW - layoutPad * 2, cityH, inkCity, alpha);
}

// ── Main render ──────────────────────────────────────────
void renderFrame() {
  uint32_t t0 = millis();

  // The theme owns the WHOLE UI: background, ink colors, and the chrome
  // typeface (see uiFont*). The poster field is flat, so the loading comet
  // flips to a dark glow when the field is light.
  // The hub owns its palette: SETUP wears Electric (bright cobalt + black,
  // matching the phone portal page), SEARCH wears Butter (deep cobalt +
  // butter yellow) — one glance tells the tabs apart.
  if (uiMode == MODE_WIFI_HUB) posterIdx = hubTab ? 0 : 7;
  const PosterCombo& pc = POSTER_COMBOS[posterIdx];
  g_edgeDark = false;
  if (theme == THEME_POSTER) {
    renderPosterBg();
    g_inkPri = spr.color565(pc.ink[0], pc.ink[1], pc.ink[2]);
    g_inkSec = spr.color565(pc.ink2[0], pc.ink2[1], pc.ink2[2]);
    g_inkDim = spr.color565((pc.ink[0] + pc.bg[0]) / 2,      // primary ink
                            (pc.ink[1] + pc.bg[1]) / 2,      // half-faded
                            (pc.ink[2] + pc.bg[2]) / 2);     // into the field
    g_edgeDark = (pc.bg[0] * 3 + pc.bg[1] * 6 + pc.bg[2]) / 10 > 140;   // light field
  } else {
    renderGradient();
    g_inkPri = TFT_WHITE;
    g_inkSec = TFT_WHITE;
    g_inkDim = spr.color565(150, 150, 150);
  }
  uint16_t inkName = g_inkPri, inkCity = g_inkSec;
  g_gradMs += millis() - t0;
  t0 = millis();

  if (uiMode == MODE_MENU) {
    // iPod wheel with real motion: the list eases toward the selection
    // (exponential smoothing, ~90ms time constant), rows fade with distance
    // from centre, and the selected item crossfades from the list weight up
    // to the big display face as the wheel settles — no pops anywhere.
    const int cy = SH / 2, rowH = 40;
    static float    wheel = 0.0f;
    static uint32_t wheelLastMs = 0;

    // Row positions in visible-row space (the battery item can be hidden).
    int rowOf[MENU_COUNT];
    int rows = 0, selRow = 0;
    for (int i = 0; i < MENU_COUNT; i++) {
      rowOf[i] = rows;
      if (i == menuIdx) selRow = rows;
      if (menuVisible(i)) rows++;
    }

    uint32_t nowMs = millis();
    if (g_lastRenderMode != MODE_MENU) {
      wheel = (float)selRow;                    // entering: no spurious slide
    } else {
      float dtm = (float)min((uint32_t)200, nowMs - wheelLastMs);
      wheel += ((float)selRow - wheel) * (1.0f - expf(-dtm / 90.0f));
      if (fabsf((float)selRow - wheel) < 0.02f) wheel = (float)selRow;
    }
    wheelLastMs = nowMs;

    for (int i = 0; i < MENU_COUNT; i++) {
      if (!menuVisible(i)) continue;
      float d  = (float)rowOf[i] - wheel;
      float ad = fabsf(d);
      if (ad > 1.8f) continue;                  // rows fade out past the edge
      int y = cy + (int)lroundf(d * rowH);
      float aF  = ad <= 1.0f ? 255.0f - 127.0f * ad : 128.0f * (1.8f - ad) / 0.8f;
      float sel = 1.0f - constrain(ad / 0.45f, 0.0f, 1.0f);   // selectedness
      uint8_t aSmall = (uint8_t)constrain(aF * (1.0f - sel), 0.0f, 255.0f);
      uint8_t aBig   = (uint8_t)constrain(aF * sel,          0.0f, 255.0f);
      // The network row says what it IS from afar ("Remote control") and
      // where to GO once you arrive: the big settled word crossfades to the
      // address itself.
      const char* small = menuLabel(i);
      const char* big   = (i == MI_NETWORK && WiFi.status() == WL_CONNECTED)
                            ? "globo.local" : small;
      if (aSmall > 4) drawTextAlpha(small, uiFontRow(), y, aSmall, g_inkPri);
      if (aBig   > 4) drawTextAlpha(big,   uiFontBig(), y, aBig,   g_inkPri);
    }
  } else if (uiMode == MODE_INFO_NET) {
    drawNetworkCard();
  } else if (uiMode == MODE_INFO_BAT) {
    drawBatteryCard();
  } else if (uiMode == MODE_WIFI_RESET) {
    // Titles live in the display face, which is UPPERCASE-ONLY (0x20-0x5A) —
    // lowercase or '?' silently vanish ("Reset WiFi?" once drew as "R WF").
    drawConfirmScreen("FORGET ALL", "WIFI?", wifiResetYes);
  } else if (uiMode == MODE_OFF_CONFIRM) {
    drawConfirmScreen("TIME TO", "SLEEP?", powerOffYes);
  } else if (uiMode == MODE_WIFI_HUB) {
    drawWifiHub();
  } else if (uiMode == MODE_BROWSE) {
    // Split screen: radio column left, soundscape column right. The active
    // column carries full ink; the other recedes. Both have an OFF entry, so
    // any combination — including silence — is composable by hand.
    int cxL = SW / 4 + 4, cxR = 3 * SW / 4 - 4;
    bool L = browseCol == 0;

    drawTextAlpha("RADIO", uiFontLabel(), 24, L ? 255 : 90, g_inkPri, cxL);
    drawTextAlpha("SCAPE", uiFontLabel(), 24, L ? 90 : 255, g_inkPri, cxR);
    // active-column marker: a small filled dot under the label
    spr.fillCircle(L ? cxL : cxR, 40, 3, g_inkPri);

    uint8_t aL = L ? 255 : 110, aR = L ? 110 : 255;
    const char* rName = browseRadio == 0 ? "OFF" : STATIONS[browseRadio - 1].name;
    const char* rCity = browseRadio == 0 ? "quiet"  : STATIONS[browseRadio - 1].city;
    const char* sName = browseBed == 0 ? "OFF" : MIXES[browseBed - 1].name;
    const char* sCity = browseBed == 0 ? "no soundscape" : MIXES[browseBed - 1].place;
    drawTextAlpha(rName, uiFontRow(),  84, aL, g_inkPri, cxL);
    drawTextAlpha(rCity, uiFontBody(), 110, (uint8_t)(aL * 3 / 4), g_inkSec, cxL);
    drawTextAlpha(sName, uiFontRow(),  84, aR, g_inkPri, cxR);
    drawTextAlpha(sCity, uiFontBody(), 110, (uint8_t)(aR * 3 / 4), g_inkSec, cxR);

    // centre divider, quiet
    for (int y = 26; y < 132; y += 6)
      spr.drawFastVLine(SW / 2, y, 3, g_inkDim);

    drawTextAlpha("click: switch   hold: back", uiFontBody(), 152, 150, g_inkDim);
  } else if (uiMode == MODE_ALARM_SET) {
    // Big stretched alarm time on the gradient, typographic like everything else.
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", alarmHour, alarmMinute);
    cacheOverlayMask(buf);
    blitMask(ovMask, ovInkX, ovInkY, ovInkW, ovInkH, 40, 50, SW - 80, 80, g_inkPri);

    spr.setTextDatum(TC_DATUM);
    spr.setFreeFont(uiFontLabel());
    spr.setTextColor(g_inkPri);
    spr.drawString("SET ALARM", SW / 2, 18);
    spr.setTextColor(g_inkDim);
    spr.drawString("press: back", SW / 2, 142);
  } else if (uiMode == MODE_RADIO && alarmArmed && armedEditVol) {
    // Setting the wake volume: big stretched percentage, vertically centred.
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", volumeLevel * 100 / 21);
    cacheOverlayMask(buf);
    blitMask(ovMask, ovInkX, ovInkY, ovInkW, ovInkH, 70, 45, SW - 140, 80, inkName);
  } else if (uiMode == MODE_RADIO && alarmArmed) {
    // Surprise mode: armed shows only the target alarm time (rotate to set it),
    // vertically centred. The station is a secret picked at trigger.
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", alarmHour, alarmMinute);
    cacheOverlayMask(buf);
    blitMask(ovMask, ovInkX, ovInkY, ovInkW, ovInkH, 20, 45, SW - 40, 80, inkName);
  } else if (millis() < volOverlayUntil) {
    // Transient volume overlay: big stretched percentage. Pops in instantly
    // (responsiveness) but dissolves over its last 300ms instead of vanishing.
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", volumeLevel * 100 / 21);
    cacheOverlayMask(buf);
    uint32_t rem = volOverlayUntil - millis();
    uint8_t ova = rem >= 300 ? 255 : (uint8_t)(rem * 255 / 300);
    blitMask(ovMask, ovInkX, ovInkY, ovInkW, ovInkH, 70, 40, SW - 140, 90, inkName, ova);
    // No "VOLUME" label — the big stretched percentage speaks for itself.
  } else {
    // New station identity fades in over 400ms rather than popping.
    uint32_t age = millis() - g_masksReadyMs;
    uint8_t inA = age >= 400 ? 255 : (uint8_t)(age * 255 / 400);
    drawStationIdentity(inkName, inkCity, inA);
    // Demo mode shows just the station name/city — no "DEMO" label, no counter.
  }

  // Corner chrome: battery icon + percentage top-right whenever a cell is
  // attached (Tycho: always show capacity, charging or not).
  if (batteryPresent()) drawBatteryIcon(SW - 28, 5, g_batPct);

  // Crossing 15% on battery: one full-screen typographic moment (no sound —
  // the display speaks). 4 seconds, once per threshold-crossing.
  static bool batWarned = false;
  static uint32_t batWarnUntil = 0;
  if (batteryPresent() && g_batPct <= 15 && !batWarned) {
    batWarned = true;
    batWarnUntil = millis() + 4000;
    wakeScreen();
  }
  if (g_batPct > 20) batWarned = false;        // re-arm after a charge
  if (millis() < batWarnUntil) {
    char b[8]; snprintf(b, sizeof(b), "%d%%", g_batPct);
    cacheOverlayMask(b);
    blitMask(ovMask, ovInkX, ovInkY, ovInkW, ovInkH, 70, 34, SW - 140, 84, g_inkPri);
    drawTextAlpha("BATTERY LOW", uiFontLabel(), 138, 230, g_inkPri);
  }

  drawLoadingEdge();   // handles its own fade in/out, safe to call always
  g_textMs += millis() - t0;
  g_lastRenderMode = uiMode;

  t0 = millis();
  spr.pushSprite(0, 0);
  g_pushMs += millis() - t0;

  // Screenshot request: copy the exact frame that just went to the panel.
  if (g_shotRequest) {
    if (!g_shotBuf) g_shotBuf = (uint16_t*)heap_caps_malloc(SW * SH * 2, MALLOC_CAP_SPIRAM);
    if (g_shotBuf) memcpy(g_shotBuf, spr.getPointer(), SW * SH * 2);
    g_shotRequest = false;
    g_shotReady = g_shotBuf != nullptr;
  }
}

// ── Battery ──────────────────────────────────────────────
// No fuel gauge on this board — the divider on VBAT is all there is, so the
// percentage is a voltage estimate. A 1S LiPo discharges NON-linearly (long
// 3.7-3.9V plateau); the old linear map read ~55% for a half-full cell. This
// piecewise curve tracks a typical cell much closer. While charging the
// charger lifts the rail toward 4.2-4.35V, so the estimate runs optimistic —
// physics, not fixable without a gauge chip.
void updateBattery() {
  int pinMv = analogReadMilliVolts(PIN_BAT_ADC);
  int vbatMv = pinMv * 2;   // undo the 100k/100k divider
  if (g_batMvEma == 0.0f) g_batMvEma = (float)vbatMv;
  else g_batMvEma = 0.8f * g_batMvEma + 0.2f * (float)vbatMv;
  int mv = (int)g_batMvEma;
  static const int   CURVE_MV[]  = {3300, 3400, 3500, 3600, 3700, 3800, 3900, 4000, 4100, 4200};
  static const int   CURVE_PCT[] = {   0,    5,   10,   18,   30,   45,   60,   78,   90,  100};
  static const int   CURVE_N = 10;
  if (mv <= CURVE_MV[0]) { g_batPct = 0; return; }
  if (mv >= CURVE_MV[CURVE_N - 1]) { g_batPct = 100; return; }
  for (int i = 1; i < CURVE_N; i++) {
    if (mv < CURVE_MV[i]) {
      g_batPct = CURVE_PCT[i - 1] + (CURVE_PCT[i] - CURVE_PCT[i - 1]) *
                 (mv - CURVE_MV[i - 1]) / (CURVE_MV[i] - CURVE_MV[i - 1]);
      return;
    }
  }
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
  // No going dark while offline: getting WiFi IS the job then, and a sleeping
  // screen made the wake press read as "setup skipped" (Tycho). A screen that
  // is ALREADY dark stays dark (a 3am mesh hiccup must not light the bedroom);
  // the countdown just never starts. Critical-battery shutdown still protects.
  if (WiFi.status() != WL_CONNECTED) { lastActivityMs = millis(); return; }
  // The idle countdown only starts once the first station has finished loading:
  // hold the screen lit through the whole startup/connect so a slow boot never
  // goes dark before you hear anything.
  static bool firstLoadDone = false;
  if (!firstLoadDone) {
    if (g_loading) { lastActivityMs = millis(); return; }  // still booting → stay awake
    firstLoadDone = true;                                  // first station is live
  }
  if (screenAwake && millis() - lastActivityMs > SCREEN_TIMEOUT_MS) {
    screenAwake = false;
    g_blTarget = BACKLIGHT_OFF;   // blackout is fine; stay in whatever mode we're in
    // Deep sleep only when armed and idle on the main screen — never in the menu
    // or demo (those just black out and keep running). The RTC wakes for alarm.
    // If the alarm is within ~6 min, stay awake (dark) instead — a deep sleep
    // that short would just wake-loop, and this way checkAlarm fires cleanly.
    if (alarmArmed && !alarmPending && uiMode == MODE_RADIO) {
      long s = secondsUntilAlarm();
      if (s > 360) enterDeepSleep();   // never returns
    }
  }
  // Backlight stepping happens in the UI task, which never freezes.
}

// ── Stations / volume ────────────────────────────────────
void requestStation(int idx) {
  currentStation = idx;
  radioOff = false;                   // any station request turns the radio on
  shuffleGradient();
  portENTER_CRITICAL(&titleMux);      // old song title would be a lie now
  g_streamTitle[0] = '\0';
  portEXIT_CRITICAL(&titleMux);
  g_loading = true;
  g_agcReset = true;         // fresh source, fresh normalization
  rollMixCombo();            // every station is a new radio×soundscape combo
  // Bed-first choreography, EVERY start and switch: the place fades up
  // instantly (local file), the radio joins 4s later with a 2.5s ease —
  // like walking towards it.
  g_radioGateMs = mixAutoSel > 0 ? millis() + 4000 : 0;
  connectStartMs = millis();
  g_connectRequest = true;   // the audio task does the actual connect
  Serial.printf("[station] -> %d/%d %s (%s, %s)\n", idx + 1, STATION_COUNT,
                STATIONS[idx].name, STATIONS[idx].country, STATIONS[idx].city);
}

// Radio off: the soundscape becomes the whole show. The silent carrier keeps
// the pipeline alive; the bed plays at solo level through the same mixer.
void requestRadioOff() {
  radioOff = true;
  shuffleGradient();                  // still a fresh identity
  portENTER_CRITICAL(&titleMux);
  g_streamTitle[0] = '\0';
  portEXIT_CRITICAL(&titleMux);
  g_loading = false;                  // local file — no loading choreography
  g_agcReset = true;
  g_radioGateMs = 0;
  connectStartMs = millis();
  g_carrierRequest = true;            // the audio task opens /silence.mp3
  Serial.printf("[radio] off — soundscape %s\n",
                mixAutoSel > 0 ? MIXES[mixAutoSel - 1].name : "none");
}

void shuffleStation() {
  if (STATION_COUNT <= 1) { requestStation(currentStation); return; }
  int next;
  do { next = (int)(esp_random() % (uint32_t)STATION_COUNT); } while (next == currentStation);
  retryAttempts = 0;
  requestStation(next);
}

uint32_t volSaveAtMs = 0;   // debounced NVS write, 2s after the last detent
uint32_t alarmSaveAtMs = 0; // debounced NVS write of the alarm time

// Push the active preset to the decoder, with optional loudness compensation.
// Our ears lose presence/treble as level drops (Fletcher-Munson), so when the
// volume is low we add up to +4dB @1800Hz and +3dB @6000Hz on top of the
// preset, tapering to nothing by ~2/3 volume. Clamped to the library's ±12dB.
void applyTone() {
  float low = TONE_LOW, peak = TONE_PEAK, high = TONE_HIGH;
  if (loudnessComp && volumeLevel > 0) {
    float t = 1.0f - constrain((float)volumeLevel / 14.0f, 0.0f, 1.0f);   // 1 quiet → 0 loud
    low  += 4.0f * t;    // add warmth when quiet — NOT treble; this speaker is
    peak -= 1.0f * t;    // already harsh up top, so soften the shouty mid instead
  }
  audio.setTone(constrain(low, -12.0f, 12.0f), constrain(peak, -12.0f, 12.0f),
                constrain(high, -12.0f, 12.0f));
}

void changeVolume(int delta) {
  alarmFading = false;   // manual input cancels an in-progress fade
  if (sleepFading) { sleepAtMs = 0; sleepFading = false; }  // knob during the
  // sleep fade = "I'm still listening" — cancel the timer, restore level
  int prev = volumeLevel;
  volumeLevel = constrain(volumeLevel + delta, 0, 21);
  audio.setVolume((uint8_t)volumeLevel);
  if (loudnessComp) applyTone();   // tone tracks level while comp is on
  volSaveAtMs = millis() + 2000;
  volOverlayUntil = millis() + 1500;

  // Live radio: volume 0 stops the stream, turning back up restarts it. Skipped
  // while armed — there the "volume" is the wake level, not a playing stream.
  if (!alarmArmed) {
    if (volumeLevel == 0 && prev != 0)      g_stopRequest = true;
    else if (volumeLevel > 0 && prev == 0) {
      if (radioOff) requestRadioOff();                     // resume the soundscape
      else          requestStation(currentStation);
    }
  }
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

// Arm = fade the stream out, then stop it and wait silently for the alarm time.
void armAlarm() {
  alarmArmed     = true;
  alarmSilenced  = true;    // guards reconnect straight away; stream stops after fade
  alarmFadeOut   = true;
  alarmFading    = false;
  armedEditVol   = false;
  g_loading      = false;   // not loading — keep the edge glow off while waiting
  fadeOutStartMs = millis();
  prefs.putBool("armed", true);
  Serial.println("[alarm] armed — fading out, stream stops until alarm time");
}

// Disarm = if we were waiting, resume the radio at the normal volume.
void disarmAlarm() {
  bool wasWaiting = alarmSilenced || alarmFadeOut;
  alarmArmed    = false;
  alarmSilenced = false;
  alarmFadeOut  = false;
  alarmFading   = false;
  armedEditVol  = false;
  g_stopRequest = false;
  prefs.putBool("armed", false);
  if (wasWaiting) {
    audio.setVolume((uint8_t)volumeLevel);
    requestStation(currentStation);   // resume playing
    Serial.println("[alarm] disarmed — resuming radio");
  }
}

void adjustAlarm(int detents) {
  int total = alarmHour * 60 + alarmMinute + detents * 5;
  total = ((total % 1440) + 1440) % 1440;
  alarmHour = total / 60;
  alarmMinute = total % 60;
  lastAlarmChange = millis();
  uiLastInputMs = millis();
}

// ── Deep sleep (battery) ─────────────────────────────────
// Seconds from now until the alarm, minus a 5-minute margin so RTC drift over
// the night can't make us wake late. Returns -1 if we have no real time yet.
long secondsUntilAlarm() {   // raw seconds to the next alarm time, -1 if no clock yet
  time_t now = time(nullptr);
  if (now < 1000000000) return -1;
  struct tm t; localtime_r(&now, &t);
  long nowS = t.tm_hour * 3600L + t.tm_min * 60 + t.tm_sec;
  long almS = alarmHour * 3600L + alarmMinute * 60;
  long d = almS - nowS;
  if (d <= 0) d += 86400;   // it's tomorrow
  return d;
}

// Blank the panel and drop into deep sleep. Wakes on the encoder button, and —
// when armed — on a timer a few minutes before the alarm. NVS holds all state.
void enterDeepSleep() {
  Serial.println("[sleep] entering deep sleep");
  prefs.putInt("volume", volumeLevel);
  audio.stopSong();

  ledcWrite(PIN_BACKLIGHT, 0);
  pinMode(PIN_BACKLIGHT, OUTPUT);
  digitalWrite(PIN_BACKLIGHT, LOW);
  tft.writecommand(0x28);   // display off
  tft.writecommand(0x10);   // sleep in

  // Wake on the encoder button (active-low), holding its pull-up through sleep.
  esp_sleep_enable_ext1_wakeup(1ULL << PIN_ENC_SW, ESP_EXT1_WAKEUP_ANY_LOW);
  rtc_gpio_pullup_en((gpio_num_t)PIN_ENC_SW);
  rtc_gpio_pulldown_dis((gpio_num_t)PIN_ENC_SW);

  // ...and on the alarm timer if armed.
  if (alarmArmed) {
    long s = secondsUntilAlarm();
    if (s > 0) {
      long wake = s - 300;   // wake 5 min early, then poll to the exact minute
      if (wake < 1) wake = 1;
      esp_sleep_enable_timer_wakeup((uint64_t)wake * 1000000ULL);
      Serial.printf("[sleep] alarm wake in %lds (alarm in %lds)\n", wake, s);
    }
  }
  Serial.flush();
  esp_deep_sleep_start();   // never returns
}

// ── Mode plumbing ────────────────────────────────────────
void enterMode(UiMode m) {
  uiMode = m;
  ovCachedStr[0] = '\0';   // force the stretched overlay to re-render
  uiLastInputMs = millis();
}

void exitToRadio() {
  if (uiMode == MODE_ALARM_SET) { exitAlarmMode(); return; }   // saves to NVS
  // Offline there is no radio to exit to — every back-out lands on the hub.
  enterMode(WiFi.status() == WL_CONNECTED ? MODE_RADIO : MODE_WIFI_HUB);
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
    case MI_TOGGLE_ALARM: if (alarmArmed) disarmAlarm(); else armAlarm();
                          uiLastInputMs = millis(); break;   // stay in the menu
    case MI_BROWSE:       browseCol = 0;
                          browseRadio = radioOff ? 0 : currentStation + 1;
                          browseBed = mixAutoSel;
                          enterMode(MODE_BROWSE); break;
    case MI_NETWORK:      enterMode(MODE_INFO_NET); break;
    case MI_BATTERY:      enterMode(MODE_INFO_BAT); break;
    case MI_TURN_OFF:     powerOffYes = false; enterMode(MODE_OFF_CONFIRM); break;
    case MI_WIFI_RESET:   wifiResetYes = false; enterMode(MODE_WIFI_RESET); break;
    case MI_BACK:         exitToRadio(); break;
    default:              exitToRadio(); break;
  }
}


void checkAlarm() {
  if (!alarmArmed || !alarmSilenced) return;   // only fires while armed & waiting
  if (millis() - lastAlarmChange < 10000) return;   // just toggled — hold off

  time_t now = time(nullptr);
  if (now < 1000000000) return;   // no NTP time yet
  struct tm t;
  localtime_r(&now, &t);

  static int lastTriggeredDay = -1;   // one-shot per day
  int nowMin = t.tm_hour * 60 + t.tm_min;
  int almMin = alarmHour * 60 + alarmMinute;
  int diff   = (nowMin - almMin + 1440) % 1440;   // minutes since the alarm time
  if (diff <= 1 && t.tm_yday != lastTriggeredDay) {   // on the minute (+1 grace for RTC drift)
    lastTriggeredDay = t.tm_yday;
    Serial.printf("[alarm] triggered at %02d:%02d — good morning\n", t.tm_hour, t.tm_min);
    alarmPending  = false;
    alarmSilenced = false;
    alarmFadeOut  = false;
    armedEditVol  = false;
    alarmWakeVol  = volumeLevel > 0 ? volumeLevel : 12;   // never a silent alarm
    alarmArmed    = false;                                 // it's going off — clear armed
    prefs.putBool("armed", false);
    wakeScreen();
    audio.setVolume(0);
    alarmFading = true;
    fadeStartMs = millis();
    shuffleStation();   // a new country every morning
  }
}

void updateFade() {
  // Arming: fade the stream OUT, then stop it and stay silent.
  if (alarmFadeOut) {
    uint32_t el = millis() - fadeOutStartMs;
    if (el >= FADE_OUT_MS) {
      audio.setVolume(0);
      g_stopRequest = true;      // audio task stops the stream → silence
      alarmFadeOut = false;
      Serial.println("[alarm] stream stopped — waiting for alarm time");
    } else {
      audio.setVolume((uint8_t)((uint32_t)volumeLevel * (FADE_OUT_MS - el) / FADE_OUT_MS));
    }
    return;
  }
  // Alarm firing: fade IN up to the wake volume.
  if (!alarmFading) return;
  uint32_t elapsed = millis() - fadeStartMs;
  if (elapsed >= FADE_DURATION_MS) {
    audio.setVolume((uint8_t)alarmWakeVol);
    volumeLevel = alarmWakeVol;   // land on the wake volume as the new setting
    alarmFading = false;
    return;
  }
  audio.setVolume((uint8_t)((uint32_t)alarmWakeVol * elapsed / FADE_DURATION_MS));
}

// If a stream never produces audio (or dies), march on to the next station
// so the alarm can't end up silent (globo-eink's validation logic).
void validateStream() {
  if (alarmSilenced) { g_streamEof = false; g_loading = false; return; }  // armed & waiting: silent, no load glow
  if (g_loading && audio.isRunning() && audio.getBitRate() > 0) {
    g_loading = false;
    retryAttempts = 0;
    Serial.println("[audio] stream is live");
  }
  // Two failure paths converge on "hop to the next station": an explicit
  // connect error (fast — after a short dwell so identities don't strobe)
  // and the silent-stream timeout (slow — stream connected but never plays).
  bool failedFast = g_loading && g_connectFailed &&
                    millis() - connectStartMs > CONNECT_FAIL_DWELL_MS;
  bool failedSlow = g_loading && millis() - connectStartMs > STREAM_VALIDATE_MS;
  if (failedFast || failedSlow) {
    g_connectFailed = false;
    if (radioOff) {
      g_loading = false;   // carrier missing/broken — report, don't hop
      Serial.println("[radio] silence carrier failed (silence.wav uploaded?)");
    } else if (WiFi.status() != WL_CONNECTED) {
      g_loading = false;   // offline: stay quiet instead of hop-looping
      Serial.println("[audio] no WiFi — radio idle");
    } else if (retryAttempts < MAX_RETRY_ATTEMPTS) {
      retryAttempts++;
      Serial.printf("[audio] %s — retry %d/%d\n",
                    failedFast ? "connect failed" : "silent too long",
                    retryAttempts, MAX_RETRY_ATTEMPTS);
      requestStation((currentStation + 1) % STATION_COUNT);
    } else {
      g_loading = false;   // give up until the user picks a station
      Serial.println("[audio] giving up after max retries");
    }
  }
  if (g_streamEof) {
    g_streamEof = false;
    if (radioOff) {
      g_carrierRequest = true;   // loop the silent carrier
    } else {
      Serial.println("[audio] stream ended — reconnecting");
      requestStation(currentStation);
    }
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

static uint32_t encSwPressMs = 0;
static uint32_t encSwStableLowMs = 0;
static bool     encLongFired = false;
static bool     encPressWasAwake = false;
static uint32_t encLastRotateMs = 0;
#define LONG_PRESS_MS 800    // hold opens/closes the menu, fires on threshold.
// 2s felt broken ("is it dead?") — 0.8s is the classic iPod/Nokia hold: long
// enough that no deliberate click ever trips it, short enough to feel alive.
#define ENC_ROTATE_GUARD_MS 200   // ignore switch dips while/just after turning
#define PRESS_MIN_LOW_MS 60       // GPIO3 must hold LOW this long to count as a click

// All input goes through the EC11: rotate, short press, long press.
void onRotate(int d) {
  switch (uiMode) {
    case MODE_RADIO:
      // Clockwise (up) should increase; the encoder reports that as -d here, so
      // flip it for volume/time. The menu keeps the raw direction (feels right).
      if (alarmArmed) {
        if (armedEditVol) changeVolume(-d);                        // set wake volume
        else { adjustAlarm(-d); alarmSaveAtMs = millis() + 1500; } // set alarm time
      } else changeVolume(-d);
      break;
    case MODE_MENU:        menuStep(d); uiLastInputMs = millis(); break;
    case MODE_ALARM_SET:   adjustAlarm(d); break;
    case MODE_WIFI_RESET:  wifiResetYes = !wifiResetYes; uiLastInputMs = millis(); break;
    case MODE_OFF_CONFIRM: powerOffYes = !powerOffYes; uiLastInputMs = millis(); break;
    case MODE_INFO_NET:    g_netShowIp = !g_netShowIp; uiLastInputMs = millis(); break;
    case MODE_WIFI_HUB:    hubTab ^= 1; uiLastInputMs = millis(); break;
    case MODE_BROWSE:
      if (browseCol == 0) {
        browseRadio = (browseRadio + (d > 0 ? 1 : -1) + STATION_COUNT + 1) % (STATION_COUNT + 1);
        browseConnectAtMs = millis() + 700;   // debounced radio switch
      } else {
        browseBed = (browseBed + (d > 0 ? 1 : -1) + MIX_COUNT + 1) % (MIX_COUNT + 1);
        mixAutoSel = browseBed;               // bed applies live (hook fades)
      }
      uiLastInputMs = millis(); break;
    default:               uiLastInputMs = millis(); break;   // info cards ignore rotation
  }
}

void onShortPress() {
  switch (uiMode) {
    case MODE_RADIO:      if (alarmArmed) { armedEditVol = !armedEditVol; uiLastInputMs = millis(); }  // time ⇄ wake vol
                          else if (!alarmSilenced) shuffleStation();
                          break;
    case MODE_MENU:       menuSelect(); break;
    case MODE_BROWSE:     browseCol = 1 - browseCol;          // tap: switch column
                          uiLastInputMs = millis(); break;
    case MODE_ALARM_SET:
      // Set-time screen only saves the time; arming is a separate menu toggle.
      prefs.putInt("alarmH", alarmHour);
      prefs.putInt("alarmM", alarmMinute);
      enterMode(MODE_MENU);
      break;
    case MODE_INFO_NET:
      // Offline with networks on file: the click is a "search now" button —
      // skip the countdown and rescan immediately. Back is a long press.
      if (WiFi.status() != WL_CONNECTED && g_savedNetCount > 0) {
        wifiWatcherRetryNow();
        uiLastInputMs = millis();
        break;
      }
      enterMode(MODE_MENU); break;
    case MODE_INFO_BAT:   enterMode(MODE_MENU); break;
    case MODE_WIFI_RESET:
      if (wifiResetYes) doWifiReset();   // does not return
      enterMode(MODE_MENU);
      break;
    case MODE_OFF_CONFIRM:
      if (powerOffYes) enterDeepSleep(); // does not return
      enterMode(WiFi.status() == WL_CONNECTED ? MODE_MENU : MODE_WIFI_HUB);
      break;
    case MODE_WIFI_HUB:
      // Tap flips the tab; landing on SEARCH also skips the scan countdown.
      hubTab ^= 1;
      if (hubTab == 0) wifiWatcherRetryNow();
      uiLastInputMs = millis();
      break;
  }
}

void onLongPress() {
  if (uiMode == MODE_WIFI_HUB) {       // offline: hold reaches TURN OFF only
    powerOffYes = false;
    enterMode(MODE_OFF_CONFIRM);
    return;
  }
  if (uiMode == MODE_RADIO) { menuIdx = 0; enterMode(MODE_MENU); }
  else exitToRadio();
}

void handleEncoder() {
  portENTER_CRITICAL(&encMux);
  int32_t d = encDelta;
  encDelta = 0;
  portEXIT_CRITICAL(&encMux);
  uint32_t now = millis();
  if (d != 0) {
    encLastRotateMs = now;
    bool wasAwake = screenAwake;
    wakeScreen();
    if (wasAwake) onRotate((int)d);
  }

  bool sw = digitalRead(PIN_ENC_SW);   // LOW = pressed (INPUT_PULLUP)
  // A cheap EC11 dips its shared-ground switch line while turning, read as
  // phantom clicks that shuffled the station. Two defences: (1) swallow the
  // switch for a guard window after any detent; (2) require GPIO3 to stay LOW
  // for PRESS_MIN_LOW_MS before it counts, rejecting brief electrical spikes.
  if (now - encLastRotateMs < ENC_ROTATE_GUARD_MS) {
    encSwStableLowMs = 0;
    encSwPressMs = 0;
    encLongFired = false;
    return;
  }
  if (sw == LOW) {
    if (encSwStableLowMs == 0) encSwStableLowMs = now;
    if (!encSwPressMs && now - encSwStableLowMs >= PRESS_MIN_LOW_MS) {
      encSwPressMs = now;               // sustained LOW: a genuine press
      encLongFired = false;
      encPressWasAwake = screenAwake;
    }
    if (encSwPressMs && !encLongFired && now - encSwPressMs > LONG_PRESS_MS) {
      encLongFired = true;
      wakeScreen();
      onLongPress();
    }
  } else {                             // released (HIGH)
    if (encSwPressMs && !encLongFired) {
      wakeScreen();                    // a press while asleep only wakes
      if (encPressWasAwake) onShortPress();
    }
    encSwStableLowMs = 0;
    encSwPressMs = 0;
    encLongFired = false;
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
      // While loading, race the gradient blobs so it's obvious the radio is
      // busy — eased in/out so the speed-up flows rather than snaps. Pairs with
      // the side-edge glow (drawLoadingEdge), both driven by g_loading.
      static float loadFlow = 2.5f;
      loadFlow += ((g_loading ? 6.0f : 2.5f) - loadFlow) * 0.08f;   // base flow much quicker now
      blobDt *= loadFlow;
      animFrame++;
      updateBlobs(constrain(blobDt, 0.5f, 12.0f));
      if (g_splashPreview) drawSplashFrame("preview");
      else                 renderFrame();
      uint32_t dt = millis() - t0;
      g_renderMsAcc += dt;
      g_frameCount++;
      vTaskDelay(pdMS_TO_TICKS(dt >= 55 ? 5 : 55 - dt));
    } else {
      vTaskDelay(pdMS_TO_TICKS(50));
    }
  }
}

// ── Ambience mixer + AGC (audio task context only) ───────
// Everything here runs inside the audio task: the ring producer (ambService,
// called from the task loop) and the consumer (the lib's raw-samples hook,
// called from audio.loop()) never race.
static const int16_t IMA_STEPS[89] = {
  7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,
  88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,
  544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,
  2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,
  10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767};
static const int8_t IMA_IDX_ADJ[8] = {-1,-1,-1,-1,2,4,6,8};

static inline int16_t imaDecodeNibble(uint8_t nib) {
  int32_t step = IMA_STEPS[imaIdx];
  int32_t diff = step >> 3;
  if (nib & 1) diff += step >> 2;
  if (nib & 2) diff += step >> 1;
  if (nib & 4) diff += step;
  if (nib & 8) imaPred -= diff; else imaPred += diff;
  imaPred = constrain(imaPred, -32768, 32767);
  imaIdx  = constrain(imaIdx + IMA_IDX_ADJ[nib & 7], 0, 88);
  return (int16_t)imaPred;
}

// The bed that should actually play right now (0 = none).
int activeMixIdx() { return mixAutoSel; }

// Every station change rolls a fresh radio×soundscape combo — ALWAYS with a
// bed (Tycho: the pairing IS the product; pure radio lives in Browse's OFF).
void rollMixCombo() {
  int prev = mixAutoSel;
  int next;
  do { next = 1 + (int)(esp_random() % MIX_COUNT); } while (next == prev && MIX_COUNT > 1);
  mixAutoSel = next;
  Serial.printf("[mix] random roll: %s\n", MIXES[mixAutoSel - 1].name);
}

// Keep the ambience ring topped up from FFat; seek(0) = gapless bed loop.
void ambService() {
  int want = activeMixIdx();
  if (want != ambOpenFor) {
    if (ambFile) ambFile.close();
    imaPred = 0; imaIdx = 0;
    ambWr = ambRd = 0;
    ambOpenFor = want;
    if (want > 0) {
      ambFile = FFat.open(MIXES[want - 1].file, FILE_READ);
      if (!ambFile) Serial.printf("[mix] %s missing — upload beds\n", MIXES[want - 1].file);
    }
  }
  if (want <= 0 || !ambFile || !ambRing || g_uploadActive) return;
  uint8_t chunk[256];
  int emptyReads = 0;
  while (AMB_RING - (ambWr - ambRd) >= 2 * sizeof(chunk) + 4) {
    int n = ambFile.read(chunk, sizeof(chunk));
    if (n <= 0) {
      // Empty/corrupt bed (e.g. a failed upload): bail out instead of
      // spinning forever inside the audio task.
      if (ambFile.size() == 0 || ++emptyReads > 2) {
        Serial.printf("[mix] bed unreadable (%u bytes) — disabled\n", (unsigned)ambFile.size());
        ambFile.close();
        return;
      }
      ambFile.seek(0); imaPred = 0; imaIdx = 0; continue;
    }
    emptyReads = 0;
    for (int i = 0; i < n; i++) {
      ambRing[ambWr++ & (AMB_RING - 1)] = imaDecodeNibble(chunk[i] & 0x0F);
      ambRing[ambWr++ & (AMB_RING - 1)] = imaDecodeNibble(chunk[i] >> 4);
    }
  }
}

// Library hook: every decoded frame passes through here BEFORE volume/EQ.
// 1) slow AGC levels out station loudness differences; 2) the ambience bed
// is resampled 16k → stream rate and added, so radio plays "in" a place.
// ⚠ SAMPLE SCALE: m_outBuff goes to I2S as 32-BIT frames — samples here are
// full int32 scale (int16 << 16), NOT 16-bit. A 16-bit clamp here silences
// the device at -66dB (hard-won lesson).
#define PCM_FS        2147000000.0f          // ~int32 full scale, clamp ceiling
#define AGC_TARGET    (5000.0f * 65536.0f)   // mean-abs target (≈ -16dB-ish)
// Gate low enough that genuinely quiet stations (Kampala!) still get lifted,
// but true dead air stays untouched. Max gain 3.5x reaches the whisperers.
#define AGC_GATE      (180.0f  * 65536.0f)   // below this: hold gain (silence)
#define BED_SCALE     65536.0f               // bed PCM int16 → 32-bit scale

void audio_process_raw_samples(int32_t* buf, int16_t frames) {
  if (frames <= 0) return;
  uint32_t rate = audio.getSampleRate();
  if (rate < 8000) rate = 44100;

  // ---- bed-first choreography: gate the radio until its entrance ----
  if (g_radioGateMs && !radioOff) {
    int32_t d = (int32_t)(millis() - g_radioGateMs);
    if (d >= 2500) {
      g_radioGateMs = 0;                                    // gate fully open
    } else {
      float g = d <= 0 ? 0.0f : (float)d / 2500.0f;
      g *= g;                                               // ease-in
      for (int i = 0; i < 2 * frames; i++) buf[i] = (int32_t)(buf[i] * g);
    }
  }

  // ---- AGC ----
  if (g_agcReset) { g_agcReset = false; agcEnv = 0; agcGain = 1.0f; }
  if (agcOn) {
    float acc = 0;
    for (int i = 0; i < frames; i++) acc += fabsf((float)buf[i * 2]);
    float blockAbs = acc / frames;
    float aTau = blockAbs > agcEnv ? 2.0f : 6.0f;            // s, attack/release
    float k = (float)frames / (rate * aTau);
    agcEnv += (blockAbs - agcEnv) * min(k, 1.0f);
    if (agcEnv > AGC_GATE) {
      float want = constrain(AGC_TARGET / agcEnv, 0.4f, 3.5f);
      float slew = (float)frames / rate * 0.35f;              // ~3dB/s max
      agcGain += constrain(want - agcGain, -slew, slew);
    }
    for (int i = 0; i < 2 * frames; i++) {
      float v = (float)buf[i] * agcGain;
      buf[i] = (int32_t)constrain(v, -PCM_FS, PCM_FS);
    }
  }

  // ---- ambience bed ----
  static float ambPos = 0, ambLevel = 0;
  int am = activeMixIdx();
  float target = (am > 0 && ambRing) ? (radioOff ? BED_SOLO_LEVEL : MIXES[am - 1].level) : 0.0f;
  if (target <= 0.001f && ambLevel <= 0.001f) return;
  float lk = (float)1.0f / (rate * 1.5f);                     // ~1.5s fade
  float step = 16000.0f / rate;
  for (int i = 0; i < frames; i++) {
    ambLevel += (target - ambLevel) * lk;                     // per-frame ease
    float s = 0;
    if (ambWr - ambRd > 2) {
      int16_t a = ambRing[ambRd & (AMB_RING - 1)];
      int16_t b = ambRing[(ambRd + 1) & (AMB_RING - 1)];
      s = (a + (b - a) * ambPos) * ambLevel * BED_SCALE;
      ambPos += step;
      while (ambPos >= 1.0f) { ambPos -= 1.0f; ambRd++; }
    }
    buf[i * 2]     = (int32_t)constrain((float)buf[i * 2]     + s, -PCM_FS, PCM_FS);
    buf[i * 2 + 1] = (int32_t)constrain((float)buf[i * 2 + 1] + s, -PCM_FS, PCM_FS);
  }
}

// ── Audio task (core 1, away from WiFi) ──────────────────
// All connecttohost() calls happen here so they never race audio.loop().
volatile uint32_t audioTaskTicks = 0;
void audioTask(void*) {
  Serial.println("[audioTask] started");
  for (;;) {
    if (g_stopRequest) {
      g_stopRequest = false;
      audio.stopSong();
    }
    if (g_connectRequest) {
      g_connectRequest = false;
      g_connectFailed = false;
      audio.stopSong();
      if (!audio.connecttohost(STATIONS[currentStation].url)) {
        g_connectFailed = true;   // loop() hops to the next station quickly
      }
    }
    if (g_carrierRequest) {
      g_carrierRequest = false;
      g_connectFailed = false;
      audio.stopSong();
      // WAV, not MP3: an 8kbps MP3 of "silence" decodes to audible artifact
      // noise (QUIET read vu≈26k). A WAV of literal zeros is truly silent.
      if (!audio.connecttoFS(FFat, "/silence.wav")) {
        g_connectFailed = true;   // carrier missing → loop() reports
      }
    }
    audio.loop();
    ambService();      // keep the ambience bed ring topped up
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
    if (i.e == Audio::evt_streamtitle) {
      const char* t = i.msg ? i.msg : (i.s ? i.s : "");
      portENTER_CRITICAL(&titleMux);
      strncpy(g_streamTitle, t, sizeof(g_streamTitle) - 1);
      g_streamTitle[sizeof(g_streamTitle) - 1] = '\0';
      portEXIT_CRITICAL(&titleMux);
    }
    Serial.printf("[%s] %s\n", audioEventName(i.e), i.msg ? i.msg : (i.s ? i.s : ""));
  };
}

// ── WiFi portal + QR + saved-networks store (tPod) ───────
// Card and code drawn together: only the generated handle knows the real
// module count (version 1 = 21, 2 = 25, 3 = 29). Sizing the card from the
// version-3 maximum left short URLs hugging the top-left of an oversized
// card. Right-anchored, vertically centred; sets g_qrX0 for the caller's
// text column.
static void qrDisplayCb(esp_qrcode_handle_t handle) {
  int n = esp_qrcode_get_size(handle);
  g_qrScale = (SH - 2 * g_qrMargin - 12) / n;
  int qrPx = n * g_qrScale;
  g_qrX0 = SW - qrPx - 21;
  g_qrY0 = (SH - qrPx) / 2;
  spr.fillRoundRect(g_qrX0 - g_qrMargin, g_qrY0 - g_qrMargin,
                    qrPx + 2 * g_qrMargin, qrPx + 2 * g_qrMargin, 6, TFT_WHITE);
  for (int y = 0; y < n; y++) {
    for (int x = 0; x < n; x++) {
      if (esp_qrcode_get_module(handle, x, y)) {
        spr.fillRect(g_qrX0 + x * g_qrScale, g_qrY0 + y * g_qrScale,
                     g_qrScale, g_qrScale, TFT_BLACK);
      }
    }
  }
}

// (First-run setup now lives on the hub screen — drawWifiHub's SETUP tab.)

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
  g_savedNetCount = (int)out.size();   // keep the UI's cached count honest
  portENTER_CRITICAL(&scanMux);        // + names for the hub's SEARCH ticker
  for (int i = 0; i < 8; i++) {
    if (i < (int)out.size()) {
      strncpy(g_savedSsids[i], out[i].ssid.c_str(), 32);
      g_savedSsids[i][32] = '\0';
    } else g_savedSsids[i][0] = '\0';
  }
  portEXIT_CRITICAL(&scanMux);
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
  g_savedNetCount = (int)creds.size();
  Serial.printf("[wifi] remembered SSID='%s' (total=%u)\n",
                ssid.c_str(), (unsigned)creds.size());
}

void wipeSavedNetworks() {
  wifiPrefs.begin(WIFI_NS, false);
  wifiPrefs.clear();
  wifiPrefs.end();
  g_savedNetCount = 0;
  Serial.println("[wifi] cleared saved networks store");
}

// Animated boot splash: wire globe + GLOBO, optional status line, and a live
// gradient so it never looks frozen while WiFi connects.
// A real spinning globe: orthographic wireframe sphere, Earth-like 20° axial
// tilt, meridians sweeping as it rotates (~5.5s/rev), latitude arcs steady.
// Theme-aware: gradient + white in FLOW, flat field + ink in POSTER. The
// wordmark and globe fade up over the first 700ms — nothing pops.
static uint32_t g_splashStartMs = 0;

void drawSplashFrame(const char* status) {
  if (!g_splashStartMs) g_splashStartMs = millis();
  uint16_t ink, inkDim;
  if (theme == THEME_POSTER) {
    renderPosterBg();
    const PosterCombo& c = POSTER_COMBOS[posterIdx];
    ink    = spr.color565(c.ink[0], c.ink[1], c.ink[2]);
    inkDim = spr.color565((c.ink[0] + c.bg[0]) / 2, (c.ink[1] + c.bg[1]) / 2,
                          (c.ink[2] + c.bg[2]) / 2);
  } else {
    renderGradient();
    ink    = TFT_WHITE;
    inkDim = spr.color565(200, 200, 200);
  }
  uint32_t age = millis() - g_splashStartMs;
  uint8_t  in  = age >= 700 ? 255 : (uint8_t)(age * 255 / 700);

  const float gx = SW / 2.0f, gy = 58.0f, r = 32.0f;
  const float tilt = 20.0f * (float)M_PI / 180.0f;
  const float ct = cosf(tilt), st = sinf(tilt);
  float th = millis() * 0.00115f;                       // spin: ~5.5s per rev

  // Project (λ latitude-longitude on unit sphere) → screen; z' decides front.
  auto project = [&](float lon, float lat, float& px, float& py, bool& front) {
    float cl = cosf(lat);
    float x = cl * sinf(lon + th);
    float z = cl * cosf(lon + th);
    float y = sinf(lat);
    float y2 = y * ct - z * st;                         // axial tilt
    float z2 = y * st + z * ct;
    px = gx + r * x;
    py = gy - r * y2;
    front = z2 > 0.02f;
  };
  auto arc = [&](float lon0, float lat0, float lon1, float lat1) {
    float ax, ay, bx, by; bool af, bf;
    project(lon0, lat0, ax, ay, af);
    project(lon1, lat1, bx, by, bf);
    if (af && bf) spr.drawWideLine(ax, ay, bx, by, 1.4f, ink);
  };

  // 8 half-meridians (full sphere coverage) sweep past; 3 parallels hold form.
  for (int m = 0; m < 8; m++) {
    float lon = m * (float)M_PI / 4.0f;
    for (int s = 0; s < 20; s++) {
      float a = -(float)M_PI / 2 + (float)M_PI * s / 20.0f;
      float b = -(float)M_PI / 2 + (float)M_PI * (s + 1) / 20.0f;
      arc(lon, a, lon, b);
    }
  }
  for (int p = -1; p <= 1; p++) {
    float lat = p * (float)M_PI / 4.0f;
    for (int s = 0; s < 28; s++) {
      float a = 2 * (float)M_PI * s / 28.0f;
      float b = 2 * (float)M_PI * (s + 1) / 28.0f;
      arc(a, lat, b, lat);
    }
  }
  spr.drawSmoothCircle(gx, gy, r, ink, ink);            // crisp limb on top

  drawTextAlpha("GLOBO", uiFontBig(), 116, in, ink);
  if (status && status[0])
    drawTextAlpha(status, uiFontLabel(), 150, (uint8_t)(in * 3 / 4), inkDim);

  // Screenshot support during splash (renderFrame's copy isn't running yet).
  if (g_shotRequest) {
    if (!g_shotBuf) g_shotBuf = (uint16_t*)heap_caps_malloc(SW * SH * 2, MALLOC_CAP_SPIRAM);
    if (g_shotBuf) memcpy(g_shotBuf, spr.getPointer(), SW * SH * 2);
    g_shotRequest = false;
    g_shotReady = g_shotBuf != nullptr;
  }
  spr.pushSprite(0, 0);
}

void splashTick(const char* label) {
  updateBlobs(1.6f);
  static const char* dots[4] = {"", ".", "..", "..."};
  char buf[28];
  snprintf(buf, sizeof(buf), "%s%s", label, dots[(millis() / 350) % 4]);
  drawSplashFrame(buf);
}

static bool tryConnect(const String& ssid, const String& psk, uint32_t timeoutMs) {
  Serial.printf("[wifi] trying SSID='%s'\n", ssid.c_str());
  WiFi.begin(ssid.c_str(), psk.c_str());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) {
    splashTick("connecting");
    delay(120);
  }
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.disconnect(false, false);
  return false;
}

// The captive setup page lives in setup_page.h (same pattern as web_page.h:
// the .ino preprocessor mangles raw strings with inline JS).

// Boot connect: try remembered networks strongest-first (scan-based — the
// mesh lies to fast-reconnect). No blocking portal anymore: if nothing is
// reachable, setup() raises the hub — soft-AP + captive setup page + the
// watcher searching, all at once (AP_STA) — and the LCD shows SEARCH | SETUP.
// The radio UI stays locked out until a link exists.
void ensureWiFi() {
  // Hold the encoder button at boot >1.2s to wipe saved creds.
  uint32_t holdStart = millis();
  bool wipe = false;
  while (digitalRead(PIN_ENC_SW) == LOW && millis() - holdStart < 1500) {
    if (millis() - holdStart > 1200) { wipe = true; break; }
    delay(50);
  }
  if (wipe) {
    Serial.println("[wifi] encoder held at boot — wiping saved credentials");
    WiFi.disconnect(true, true);   // also erases the core's stored creds
    wipeSavedNetworks();
  }

  // Try every remembered network, strongest-first based on a scan.
  WiFi.mode(WIFI_STA);
  auto saved = loadSavedNetworks();
  if (!saved.empty()) {
    Serial.printf("[wifi] %u remembered network(s); scanning...\n", (unsigned)saved.size());
    // async (splash animates), no hidden, active scan, 150ms/channel instead of
    // the 300ms default — roughly halves the scan, still finds every real AP.
    WiFi.scanNetworks(true, false, false, 150);
    int found;
    while ((found = WiFi.scanComplete()) == WIFI_SCAN_RUNNING) { splashTick("searching"); delay(80); }
    if (found < 0) found = 0;
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
        Serial.printf("[wifi] connected to '%s', ip=%s rssi=%d mac=%s bssid=%s\n",
                      c.ssid.c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI(),
                      WiFi.macAddress().c_str(), WiFi.BSSIDstr().c_str());
        return;
      }
    }
    Serial.println("[wifi] no remembered network reachable");
  }

  // Offline. The watcher arms here; setup() raises the hub right after.
  wwState  = WW_WAIT;
  wwNextMs = millis() + 3000;
}

// ── Runtime WiFi watcher (see the state block up top) ────
// Serviced every loop() pass; all waiting is non-blocking so the UI, encoder
// and web remote never notice a scan in flight.
void wifiWatcherRetryNow() {           // the NETWORK card's click-to-search
  if (wwState == WW_WAIT) wwNextMs = millis();
}

// First successful link of the session may happen here rather than in setup():
// bring up whatever a boot-time connect would have brought up, then resume the
// stream on the SAME station — a reconnect is recovery, not a new identity.
static void onWifiRegained() {
  Serial.printf("[wifi] online: '%s' ip=%s rssi=%d\n", WiFi.SSID().c_str(),
                WiFi.localIP().toString().c_str(), WiFi.RSSI());
  stopWifiHub();                       // AP + captive DNS down, pure STA
  wwState = WW_ONLINE;
  wwLastResult[0] = '\0';
  rememberNetwork(WiFi.SSID(), WiFi.psk());
  WiFi.setSleep(WIFI_PS_NONE);         // streaming hates modem-sleep (setup())
  if (!g_ntpUp) { configTzTime(TZ_INFO, NTP_SERVER); g_ntpUp = true; }
  if (!g_webUp) {
    webSetup();
    g_webUp = true;
    WiFi.setSleep(WIFI_PS_NONE);       // mDNS init can re-enable PS
  }
  // The hub (or the old network card) graduates to the radio — listening
  // resumes below.
  if (uiMode == MODE_WIFI_HUB || uiMode == MODE_INFO_NET) enterMode(MODE_RADIO);
  // The radio went idle when the link died (validateStream's "no WiFi"
  // branch). If it should be playing, pick the stream back up. A stream
  // that's still nominally running recovers on its own via the EOF handler
  // or the lag watchdog, so only the idle case needs a push here.
  if (!radioOff && !alarmSilenced && !alarmFadeOut && volumeLevel > 0 &&
      !audio.isRunning() && !g_loading) {
    Serial.println("[wifi] resuming the stream");
    retryAttempts = 0;
    g_loading = true;
    connectStartMs = millis();
    g_connectRequest = true;
  }
}

void serviceWifiWatcher() {
  uint32_t now = millis();
  if (WiFi.status() == WL_CONNECTED) {
    if (wwState != WW_ONLINE) onWifiRegained();
    return;
  }
  switch (wwState) {
    case WW_ONLINE:   // the link just dropped
      Serial.println("[wifi] connection lost — watcher armed");
      snprintf(wwLastResult, sizeof(wwLastResult), "connection lost");
      wwState = WW_WAIT;
      wwNextMs = now + WW_DROP_GRACE_MS;
      break;
    case WW_WAIT:
      // A credential fresh from the setup page jumps the queue — join it right
      // now, even with the phone still attached to the AP (a normal router
      // accepts; a paused hotspot fails and falls back into the cycle).
      if (wwForcePending) {
        wwForcePending = false;
        Serial.printf("[wifi] watcher: joining '%s' (from setup page)\n", wwForceSsid.c_str());
        WiFi.begin(wwForceSsid.c_str(), wwForcePsk.c_str());
        wwSsid   = wwForceSsid;
        wwJoinT0 = now;
        wwState  = WW_JOINING;
        break;
      }
      if ((int32_t)(now - wwNextMs) < 0) break;
      // While a phone sits on the setup AP the radio belongs to the portal —
      // a scan would hiccup it. The hunt resumes the moment it lets go, which
      // is exactly when a just-configured hotspot comes back on air.
      if (g_hubUp && WiFi.softAPgetStationNum() > 0) { wwNextMs = now + 3000; break; }
      // With the hub up we scan even with nothing saved — the setup page's
      // network list feeds from these results.
      if (g_savedNetCount <= 0 && !g_hubUp) { wwNextMs = now + WW_SCAN_EVERY_MS; break; }
      // Same fast async scan as boot. disconnect() first: the core may still
      // be quietly re-associating, and a scan under that fails or lies.
      WiFi.disconnect(false, false);
      WiFi.scanNetworks(true, false, false, 150);
      wwState = WW_SCANNING;
      break;
    case WW_SCANNING: {
      int found = WiFi.scanComplete();
      if (found == WIFI_SCAN_RUNNING) break;
      String bestSsid, bestPsk;
      int32_t bestRssi = INT32_MIN;
      if (found > 0) {
        auto saved = loadSavedNetworks();
        // Cache what's on the air for the setup page (/api/scan): dedup by
        // SSID keeping the strongest, mark the remembered ones.
        ScanHit tmp[15];
        int keep = 0;
        for (int j = 0; j < found && keep < 15; j++) {
          String s = WiFi.SSID(j);
          if (!s.length()) continue;
          bool dup = false;
          for (int k = 0; k < keep; k++)
            if (s.equals(tmp[k].ssid)) {
              if (WiFi.RSSI(j) > tmp[k].rssi) tmp[k].rssi = (int8_t)WiFi.RSSI(j);
              dup = true; break;
            }
          if (dup) continue;
          strncpy(tmp[keep].ssid, s.c_str(), 32);
          tmp[keep].ssid[32] = '\0';
          tmp[keep].rssi  = (int8_t)constrain(WiFi.RSSI(j), -127, 0);
          tmp[keep].known = false;
          for (auto& c : saved)
            if (c.ssid == s) { tmp[keep].known = true; break; }
          keep++;
        }
        portENTER_CRITICAL(&scanMux);
        memcpy((void*)g_scanHits, tmp, sizeof(tmp));
        g_scanHitCount = keep;
        portEXIT_CRITICAL(&scanMux);
        for (auto& c : saved)
          for (int j = 0; j < found; j++)
            if (WiFi.SSID(j) == c.ssid && WiFi.RSSI(j) > bestRssi) {
              bestRssi = WiFi.RSSI(j);
              bestSsid = c.ssid;
              bestPsk  = c.psk;
            }
      }
      WiFi.scanDelete();
      if (bestSsid.length()) {
        Serial.printf("[wifi] watcher: '%s' in range (%ddBm) — joining\n",
                      bestSsid.c_str(), (int)bestRssi);
        WiFi.begin(bestSsid.c_str(), bestPsk.c_str());
        wwSsid   = bestSsid;
        wwJoinT0 = now;
        wwState  = WW_JOINING;
      } else {
        snprintf(wwLastResult, sizeof(wwLastResult), "no known network nearby");
        wwState  = WW_WAIT;
        wwNextMs = now + WW_SCAN_EVERY_MS;
      }
      break;
    }
    case WW_JOINING:
      if (now - wwJoinT0 < WW_JOIN_TIMEOUT_MS) break;
      Serial.printf("[wifi] watcher: joining '%s' timed out\n", wwSsid.c_str());
      snprintf(wwLastResult, sizeof(wwLastResult), "couldn't join %s", wwSsid.c_str());
      WiFi.disconnect(false, false);
      wwState  = WW_WAIT;
      // Retry sooner than a full period — hotspots often accept on the second
      // knock (the first one only woke them up).
      wwNextMs = now + WW_SCAN_EVERY_MS / 2;
      break;
  }
}

// ── The offline hub itself: AP + captive DNS + setup page ─
// AP_STA is the whole point: the Globo-Setup network stays joinable WHILE
// the watcher scans and joins — configuring and searching are not modes to
// choose between. The setup page is served by the same WebServer as the
// remote ("/" flips by state); DNS hijack makes every hostname land there,
// which is what pops the phone's captive-portal sheet.
void startWifiHub() {
  if (g_hubUp) return;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(WIFI_AP_NAME);
  g_dns.start(53, "*", WiFi.softAPIP());
  if (!g_webUp) { webSetup(); g_webUp = true; }
  hubTab = g_savedNetCount > 0 ? 0 : 1;   // travellers land on SEARCH, virgins on SETUP
  g_hubUp = true;
  Serial.printf("[wifi] hub up: AP=%s ip=%s\n",
                WIFI_AP_NAME, WiFi.softAPIP().toString().c_str());
}

void stopWifiHub() {
  if (!g_hubUp) return;
  g_dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  g_hubUp = false;
  Serial.println("[wifi] hub down");
}

// ── Web remote: http://globo.local ───────────────────────
// JSON API + a single-page control app (web_page.h). Handlers run in loop()
// context — the same context as the encoder handlers — so device functions
// are reused directly. Anything that changes state also wakes the screen, so
// the device visibly reacts when you drive it from the phone.
#include "web_page.h"
#include "setup_page.h"

// Minimal JSON string escape: quotes, backslashes, control chars.
String jsonEscape(const char* s) {
  String out;
  out.reserve(strlen(s) + 8);
  for (const char* p = s; *p; p++) {
    char c = *p;
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if ((uint8_t)c < 0x20) { out += ' '; }
    else out += c;
  }
  return out;
}

// Telemetry follows whoever talks to us: every JSON request records its
// client as the heartbeat target (stored as a raw u32 so the cross-task
// write is atomic). No more hardcoded home-Mac IP — dead on the road.
volatile uint32_t g_telemTarget = 0;

void webSendJson(const String& body) {
  g_webHits++;
  g_telemTarget = (uint32_t)server.client().remoteIP();
  server.sendHeader("Cache-Control", "no-store");
  // Keep-alive on a one-client-at-a-time server is how it goes deaf: an idle
  // kept-alive client (curl, a Bonjour prober, a phone) parks in the single
  // slot and orphaned sockets pile up until lwIP is out. Close after every
  // response — reconnect cost on a LAN is nothing.
  server.sendHeader("Connection", "close");
  server.send(200, "application/json", body);
  server.client().stop();
}

void handleApiStatus() {
  char title[sizeof(g_streamTitle)];
  portENTER_CRITICAL(&titleMux);
  memcpy(title, g_streamTitle, sizeof(title));
  portEXIT_CRITICAL(&titleMux);

  const Station& st = STATIONS[currentStation];
  bool usb = g_batMvEma >= 4300;
  int sleepRemain = 0;
  if (sleepAtMs) {
    long ms = (long)(sleepAtMs - millis());
    sleepRemain = ms > 0 ? (int)((ms + 59999) / 60000) : 0;
  }
  String j; j.reserve(512);
  j += "{\"ok\":true,\"playing\":"; j += audio.isRunning() ? "true" : "false";
  j += ",\"loading\":"; j += g_loading ? "true" : "false";
  j += ",\"station\":{\"i\":"; j += currentStation;
  j += ",\"name\":\""; j += jsonEscape(st.name);
  j += "\",\"country\":\""; j += jsonEscape(st.country);
  j += "\",\"city\":\""; j += jsonEscape(st.city);
  j += "\"},\"title\":\""; j += jsonEscape(title);
  j += "\",\"volume\":"; j += volumeLevel;
  j += ",\"volumeMax\":21";
  const PosterCombo& sc = POSTER_COMBOS[posterIdx];   // web mirrors the combo
  j += ",\"combo\":{\"bg\":[";  j += sc.bg[0];  j += ','; j += sc.bg[1];  j += ','; j += sc.bg[2];
  j += "],\"ink\":[";           j += sc.ink[0]; j += ','; j += sc.ink[1]; j += ','; j += sc.ink[2];
  j += "],\"ink2\":[";          j += sc.ink2[0];j += ','; j += sc.ink2[1];j += ','; j += sc.ink2[2];
  j += "]}";
  j += ",\"eqName\":\""; j += "Dusk";
  j += "\",\"loudness\":"; j += loudnessComp ? "true" : "false";
  j += ",\"theme\":"; j += theme;
  j += ",\"themeName\":\"Poster";
  j += "\",\"radioOff\":"; j += radioOff ? "true" : "false";
  j += ",\"scapeName\":\""; j += radioOff && mixAutoSel > 0 ? MIXES[mixAutoSel - 1].name : "";
  j += "\",\"mix\":"; j += mixAutoSel;
  j += ",\"mixName\":\"";
  j += mixAutoSel > 0 ? MIXES[mixAutoSel - 1].name : "none";
  j += "\",\"agc\":"; j += agcOn ? "true" : "false";
  j += ",\"vu\":"; j += (int)audio.getVUlevel();   // post-hook loudness proof
  j += ",\"alarm\":{\"h\":"; j += alarmHour;
  j += ",\"m\":"; j += alarmMinute;
  j += ",\"armed\":"; j += alarmArmed ? "true" : "false";
  j += "},\"sleep\":{\"active\":"; j += sleepAtMs ? "true" : "false";
  j += ",\"remainMin\":"; j += sleepRemain;
  j += "},\"battery\":{\"pct\":"; j += g_batPct;
  j += ",\"usb\":"; j += usb ? "true" : "false";
  j += "},\"rssi\":"; j += WiFi.RSSI();
  j += "}";
  webSendJson(j);
}

void handleApiStations() {
  // The list is constant — build once, reuse forever.
  static String cached;
  if (cached.length() == 0) {
    cached.reserve(STATION_COUNT * 64);
    cached = "{\"ok\":true,\"stations\":[";
    for (int i = 0; i < STATION_COUNT; i++) {
      if (i) cached += ',';
      cached += "{\"name\":\"";    cached += jsonEscape(STATIONS[i].name);
      cached += "\",\"country\":\""; cached += jsonEscape(STATIONS[i].country);
      cached += "\",\"city\":\"";  cached += jsonEscape(STATIONS[i].city);
      cached += "\"}";
    }
    cached += "]}";
  }
  webSendJson(cached);
}

void handleApiVolume() {
  if (!server.hasArg("v")) { webSendJson("{\"ok\":false}"); return; }
  int v = constrain(server.arg("v").toInt(), 0L, 21L);
  wakeScreen();
  changeVolume(v - volumeLevel);   // reuses stop-at-0/restart, tone, overlay, save
  webSendJson(String("{\"ok\":true,\"volume\":") + volumeLevel + "}");
}

void handleApiStation() {
  if (alarmSilenced) { webSendJson("{\"ok\":false,\"reason\":\"alarm armed\"}"); return; }
  wakeScreen();
  if (server.hasArg("shuffle")) {
    retryAttempts = 0;
    shuffleStation();
  } else if (server.hasArg("i")) {
    int i = server.arg("i").toInt();
    if (i < 0 || i >= STATION_COUNT) { webSendJson("{\"ok\":false}"); return; }
    retryAttempts = 0;
    requestStation(i);
  } else { webSendJson("{\"ok\":false}"); return; }
  webSendJson(String("{\"ok\":true,\"station\":") + currentStation + "}");
}


void handleApiAlarm() {
  wakeScreen();
  if (server.hasArg("h")) alarmHour   = constrain((int)server.arg("h").toInt(), 0, 23);
  if (server.hasArg("m")) alarmMinute = constrain((int)server.arg("m").toInt(), 0, 59);
  prefs.putInt("alarmH", alarmHour);
  prefs.putInt("alarmM", alarmMinute);
  lastAlarmChange = millis();
  if (server.hasArg("armed")) {
    bool want = server.arg("armed").toInt() != 0;
    if (want && !alarmArmed)      armAlarm();
    else if (!want && alarmArmed) disarmAlarm();
  }
  webSendJson(String("{\"ok\":true,\"h\":") + alarmHour + ",\"m\":" + alarmMinute +
              ",\"armed\":" + (alarmArmed ? "true" : "false") + "}");
}

void handleApiSleep() {
  int m = server.hasArg("min") ? (int)server.arg("min").toInt() : -1;
  if (m == 0) {                       // cancel
    if (sleepFading) audio.setVolume((uint8_t)volumeLevel);   // undo the fade
    sleepAtMs = 0;
    sleepFading = false;
  } else if (m > 0 && m <= 480) {
    sleepAtMs = millis() + (uint32_t)m * 60000UL;
    sleepFading = false;
    Serial.printf("[sleep-timer] set: %d min\n", m);
  }
  webSendJson(String("{\"ok\":true,\"active\":") + (sleepAtMs ? "true" : "false") + "}");
}

// The web server gets its own task (core 0, above the UI task) instead of
// riding loop(): serviced from the flat-out loop() task the accept path
// starved and inbound connections timed out — audio (device-initiated
// sockets) never had the problem. Handlers now run cross-core from the
// encoder path; the shared state they touch is ints/volatile bools consumed
// by the audio/loop tasks (the same handshake pattern loop() already uses),
// and NVS writes are internally mutexed — tolerated by design.
void webTask(void*) {
  for (;;) {
    server.handleClient();
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

void webSetup() {
  // While the hub is up "/" IS the setup page — a phone that joins the AP and
  // browses anywhere should land on WiFi setup, not on a remote that can't
  // control anything yet.
  server.on("/", []() {
    server.sendHeader("Cache-Control", "no-cache");
    server.sendHeader("Connection", "close");
    server.send_P(200, "text/html", g_hubUp ? SETUP_HTML : INDEX_HTML);
    server.client().stop();
  });
  server.on("/setup", []() {
    server.sendHeader("Cache-Control", "no-cache");
    server.sendHeader("Connection", "close");
    server.send_P(200, "text/html", SETUP_HTML);
    server.client().stop();
  });
  server.on("/api/scan", []() {
    ScanHit hits[15];
    portENTER_CRITICAL(&scanMux);
    int n = g_scanHitCount;
    memcpy(hits, (const void*)g_scanHits, sizeof(hits));
    portEXIT_CRITICAL(&scanMux);
    String j = "{\"ok\":true,\"hits\":[";
    for (int i = 0; i < n; i++) {
      if (i) j += ',';
      j += "{\"ssid\":\""; j += jsonEscape(hits[i].ssid);
      j += "\",\"rssi\":"; j += (int)hits[i].rssi;
      j += ",\"known\":"; j += hits[i].known ? "true" : "false"; j += "}";
    }
    j += "]}";
    webSendJson(j);
  });
  server.on("/api/wifisave", []() {
    String s = server.arg("s"), p = server.arg("p");
    if (!s.length()) { webSendJson("{\"ok\":false}"); return; }
    Serial.printf("[wifi] setup page: creds for '%s'\n", s.c_str());
    rememberNetwork(s, p);
    wwForceSsid = s;                   // watcher joins it on its next pass
    wwForcePsk  = p;
    wwForcePending = true;
    webSendJson("{\"ok\":true}");
  });
  server.on("/api/status",   handleApiStatus);
  server.on("/api/stations", handleApiStations);
  server.on("/api/volume",   handleApiVolume);
  server.on("/api/station",  handleApiStation);
  // Soundscape layer: set the bed directly (0 = none). radio=0|1 turns the
  // radio itself off/on — off plays the bed solo over the silent carrier.
  server.on("/api/bed",      []() {
    wakeScreen();
    if (server.hasArg("i")) {
      mixAutoSel = constrain((int)server.arg("i").toInt(), 0, MIX_COUNT);
      // If the silent carrier died (e.g. during an upload), revive it so a
      // solo bed always sounds when asked for.
      if (radioOff && mixAutoSel > 0 && !audio.isRunning()) g_carrierRequest = true;
    }
    if (server.hasArg("radio")) {
      bool on = server.arg("radio").toInt() != 0;
      if (!on && !radioOff)     requestRadioOff();
      else if (on && radioOff) { retryAttempts = 0; requestStation(currentStation);
                                 mixAutoSel = constrain((int)server.arg("i").toInt(), 0, MIX_COUNT);
                                 g_radioGateMs = mixAutoSel > 0 ? millis() + 4000 : 0; }
    }
    webSendJson(String("{\"ok\":true,\"bed\":") + mixAutoSel +
                ",\"radioOff\":" + (radioOff ? "true" : "false") + "}");
  });
  server.on("/api/mix",      []() {   // agc toggle + a dev reroll knob
    if (server.hasArg("agc")) {
      agcOn = server.arg("agc").toInt() != 0;
      prefs.putBool("agc", agcOn);
      g_agcReset = true;
    }
    if (server.hasArg("roll")) rollMixCombo();
    webSendJson(String("{\"ok\":true,\"bed\":") + mixAutoSel +
                ",\"agc\":" + (agcOn ? "true" : "false") + "}");
  });
  server.on("/api/beds",     []() {
    static String cached;
    if (cached.length() == 0) {
      cached = "{\"ok\":true,\"beds\":[";
      for (int i = 0; i < MIX_COUNT; i++) {
        if (i) cached += ',';
        cached += "{\"name\":\""; cached += MIXES[i].name;
        cached += "\",\"place\":\""; cached += MIXES[i].place;
        cached += "\"}";
      }
      cached += "]}";
    }
    webSendJson(cached);
  });
  // Loop upload: curl -F "file=@waves.mp3" http://globo.local/api/upload
  static File upFile;
  server.on("/api/upload", HTTP_POST,
    []() { webSendJson("{\"ok\":true}"); },
    []() {
      HTTPUpload& up = server.upload();
      if (up.status == UPLOAD_FILE_START) {
        g_uploadActive = true;                    // pause the bed reader
        if (radioOff) g_stopRequest = true;            // and any FFat playback
        delay(30);                                // let in-flight reads finish
        String fn = up.filename;
        if (!fn.startsWith("/")) fn = "/" + fn;
        upFile = FFat.open(fn, FILE_WRITE);
        Serial.printf("[ffat] upload start: %s\n", fn.c_str());
      } else if (up.status == UPLOAD_FILE_WRITE && upFile) {
        upFile.write(up.buf, up.currentSize);
      } else if (up.status == UPLOAD_FILE_END) {
        if (upFile) upFile.close();
        g_uploadActive = false;
        Serial.printf("[ffat] upload done: %u bytes\n", (unsigned)up.totalSize);
      } else if (up.status == UPLOAD_FILE_ABORTED) {
        if (upFile) upFile.close();
        g_uploadActive = false;
      }
    });
  server.on("/api/fs", []() {
    if (server.hasArg("del")) {
      String fn = server.arg("del");
      if (!fn.startsWith("/")) fn = "/" + fn;
      FFat.remove(fn);
    }
    // Maintenance hatch: a brownout mid-write can corrupt the FAT root
    // directory — mount still succeeds, direct opens work, but enumeration
    // comes back empty and the orphaned clusters eat the volume. format=1
    // rebuilds the FS from scratch (caller re-uploads after; make sure the
    // device is in QUIET first so nothing is reading a bed).
    if (server.hasArg("format")) {
      g_uploadActive = true;               // pause the bed reader
      if (radioOff) g_stopRequest = true;  // and any FFat playback
      delay(50);
      FFat.end();
      bool fmtOk = FFat.format();
      bool mntOk = FFat.begin(true);
      g_uploadActive = false;
      webSendJson(String("{\"ok\":") + (fmtOk && mntOk ? "true" : "false") +
                  ",\"formatted\":true,\"free\":" + String(FFat.freeBytes()) + "}");
      return;
    }
    String j = "{\"ok\":true,\"free\":" + String(FFat.freeBytes()) +
               ",\"used\":" + String(FFat.usedBytes()) + ",\"files\":[";
    File root = FFat.open("/");
    bool first = true;
    if (root && root.isDirectory()) {
      root.rewindDirectory();
      for (File f = root.openNextFile(); f; f = root.openNextFile()) {
        if (!first) j += ',';
        first = false;
        j += "{\"name\":\""; j += jsonEscape(f.name());
        j += "\",\"size\":"; j += (int)f.size(); j += "}";
      }
    }
    j += "]";
    if (!root) j += ",\"err\":\"root open failed\"";
    else if (!root.isDirectory()) j += ",\"err\":\"root is not a dir\"";
    j += "}";
    webSendJson(j);
  });
  server.on("/api/alarm",    handleApiAlarm);
  server.on("/api/sleep",    handleApiSleep);
  server.on("/api/reboot",   []() {   // maintenance hatch (also used to
    webSendJson("{\"ok\":true}");     // verify SW-reset RF health)
    delay(200);
    ESP.restart();
  });
  server.on("/api/mode", []() {       // dev hatch: jump to any screen so a
    if (server.hasArg("m")) {         // framebuffer shot can capture it
      int m = server.arg("m").toInt();
      wakeScreen();
      g_splashPreview = m == 98;      // 98 = boot splash preview
      if (!g_splashPreview) {
        menuIdx = 0;
        wifiResetYes = false;
        browseCol = 0;
        browseRadio = radioOff ? 0 : currentStation + 1;
        browseBed = mixAutoSel;
        enterMode((UiMode)constrain(m, 0, (int)MODE_WIFI_HUB));
      }
    }
    webSendJson(String("{\"ok\":true,\"mode\":") + (int)uiMode + "}");
  });
  server.on("/api/screen", []() {     // raw framebuffer: 320x170 RGB565,
    wakeScreen();                     // byte-swapped (panel byte order)
    g_shotReady = false;
    g_shotRequest = true;
    uint32_t t0 = millis();
    while (!g_shotReady && millis() - t0 < 1500) delay(10);   // ≤ ~2 frames
    if (!g_shotReady) { webSendJson("{\"ok\":false}"); return; }
    server.sendHeader("Connection", "close");
    server.setContentLength(SW * SH * 2);
    server.send(200, "application/octet-stream", "");
    server.client().write((const uint8_t*)g_shotBuf, SW * SH * 2);
    server.client().stop();
  });
  server.onNotFound([]() {
    // Hub captive catch: the DNS hijack points every hostname here, and the
    // 302 to our own IP is what pops the phone's captive-portal sheet.
    if (g_hubUp) {
      server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
      server.sendHeader("Connection", "close");
      server.send(302, "text/plain", "");
      server.client().stop();
      return;
    }
    server.sendHeader("Connection", "close");
    server.send(404, "application/json", "{\"ok\":false}");
    server.client().stop();
  });
  server.begin();
  if (MDNS.begin("globo")) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("[web] http://globo.local up");
  } else {
    Serial.printf("[web] mDNS failed; http://%s\n", WiFi.localIP().toString().c_str());
  }
  xTaskCreatePinnedToCore(webTask, "web", 8192, NULL, 3, NULL, 0);
}

// ── Setup ────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);   // don't block if no host is reading the CDC
#endif
  delay(1500);                // give USB CDC time to enumerate
  Serial.printf("\n[boot] GLOBO starting... (reset reason %d)\n", (int)esp_reset_reason());
  Serial.printf("[boot] PSRAM: %u free, heap: %u free\n",
                (unsigned)ESP.getFreePsram(), (unsigned)ESP.getFreeHeap());

  // A timer wake means we're a few minutes before the alarm — stay up so
  // checkAlarm() can fire on the exact minute instead of sleeping again.
  esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();
  if (wakeCause == ESP_SLEEP_WAKEUP_TIMER) {
    alarmPending = true;
    Serial.println("[boot] woke from deep sleep for the alarm");
  } else if (wakeCause == ESP_SLEEP_WAKEUP_EXT1) {
    Serial.println("[boot] woke from deep sleep (button)");
  }

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
  loudnessComp = prefs.getBool("loud", true);
  rollMixCombo();   // first radio×soundscape combo of the day
  agcOn        = prefs.getBool("agc", true);
  Serial.printf("[prefs] vol=%d/21 alarm=%02d:%02d %s\n",
                volumeLevel, alarmHour, alarmMinute, alarmArmed ? "armed" : "off");

  // Ambience mixer ring buffer (see audio_process_raw_samples).
  ambRing = (int16_t*)heap_caps_malloc(AMB_RING * sizeof(int16_t), MALLOC_CAP_SPIRAM);

  // Soundscape storage: the 9MB fat partition. Format on first mount.
  if (FFat.begin(true)) {
    Serial.printf("[ffat] mounted: %u KB free of %u KB\n",
                  (unsigned)(FFat.freeBytes() / 1024), (unsigned)(FFat.totalBytes() / 1024));
  } else {
    Serial.println("[ffat] mount FAILED — soundscapes unavailable");
  }

  analogReadResolution(12);
  for (int i = 0; i < 5; i++) updateBattery();   // burn off EMA warm-up
  Serial.printf("[bat] vbat=%.0fmV pct=%d\n", g_batMvEma, g_batPct);

  randomSeed(esp_random());
  initBlobs(0);

  // Animated branded splash — a brief intro, then kept alive inside ensureWiFi()
  // so a slow WiFi connect never looks like a frozen "GLOBO".
  for (uint32_t s0 = millis(); millis() - s0 < 700; ) { splashTick("starting"); delay(60); }

  ensureWiFi();

  // Audio streaming hates WiFi modem-sleep: the default WIFI_PS_MIN_MODEM
  // adds 100-200ms packet latency cycles that drain the stream buffer on
  // marginal links. Costs ~80mA extra; worth it.
  WiFi.setSleep(WIFI_PS_NONE);

  bool online = WiFi.status() == WL_CONNECTED;
  if (online) {
    configTzTime(TZ_INFO, NTP_SERVER);
    g_ntpUp = true;
    webSetup();   // http://globo.local — web remote + JSON API
    g_webUp = true;
    WiFi.setSleep(WIFI_PS_NONE);   // re-assert: mDNS/service init can re-enable PS
  }
  // (offline: ensureWiFi armed the watcher; NTP/web start on first connect)

  // I2S to the MAX98357 (a single mono speaker). forceMono sums L+R so nothing
  // panned hard to one side is lost. Tone is a selectable 3-band EQ tuned for
  // the small driver (Dusk curve, see applyTone), applied here from the saved
  // preset + loudness compensation. The library self-allocates ~655KB of PSRAM
  // stream buffer — plenty of headroom for AAC to ride out WiFi hiccups.
  registerAudioCallbacks();
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  audio.forceMono(true);
  audio.setVolume((uint8_t)volumeLevel);
  applyTone();

  // Shuffle: a random country every power-on. The actual connect happens on
  // the audio task so a slow host can't freeze setup().
  currentStation = (int)(esp_random() % (uint32_t)STATION_COUNT);
  shuffleGradient();
  if (alarmArmed) {
    // Armed across a reboot: stay silent (stopped) until the alarm time.
    alarmSilenced = true;
    g_loading = false;
    Serial.println("[alarm] armed at boot — silent until alarm time");
  } else if (volumeLevel > 0 && online) {
    g_loading = true;
    connectStartMs = millis();
    g_connectRequest = true;
  } else if (!online) {
    g_loading = false;
    // Offline is a WiFi problem, not a listening mode: raise the hub (AP +
    // captive setup page + watcher) and show its SEARCH | SETUP tabs.
    startWifiHub();
    enterMode(MODE_WIFI_HUB);
    Serial.println("[boot] offline — hub up, watcher searching");
  } else {
    g_loading = false;   // muted at boot → stay stopped until turned up
    Serial.println("[boot] volume 0 — radio stopped until turned up");
  }
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
  serviceWifiWatcher();   // travel: rejoin remembered networks as they appear

  // WiFi down (2.5s debounce so a sub-second blip doesn't yank the screen) →
  // the hub owns the UI: AP + captive page come up and menu/radio become
  // unreachable until the link is back. TURN OFF stays reachable via hold.
  static uint32_t offSinceMs = 0;
  if (WiFi.status() != WL_CONNECTED) {
    if (!offSinceMs) offSinceMs = now;
    if (now - offSinceMs > 2500) {
      if (!g_hubUp) startWifiHub();
      if (uiMode != MODE_WIFI_HUB && uiMode != MODE_OFF_CONFIRM) enterMode(MODE_WIFI_HUB);
    }
  } else offSinceMs = 0;
  if (g_hubUp) g_dns.processNextRequest();

  // Sleep timer: at T-0 begin a 15s fade, then stop the stream and restore
  // the volume so the next play (knob or alarm) starts at the set level.
  if (sleepAtMs) {
    if (!sleepFading && (int32_t)(now - sleepAtMs) >= 0) {
      sleepFading = true;
      sleepFadeStartMs = now;
      Serial.println("[sleep-timer] fading out");
    }
    if (sleepFading) {
      uint32_t el = now - sleepFadeStartMs;
      if (el >= SLEEP_FADE_MS) {
        sleepAtMs = 0;
        sleepFading = false;
        g_stopRequest = true;
        audio.setVolume((uint8_t)volumeLevel);
        Serial.println("[sleep-timer] done — stream stopped");
      } else {
        audio.setVolume((uint8_t)((uint32_t)volumeLevel * (SLEEP_FADE_MS - el) / SLEEP_FADE_MS));
      }
    }
  }

  // Browse: the radio column applies once the knob rests. Hand-picked combos
  // skip the random reroll AND the bed-first gate — you're at the controls.
  if (browseConnectAtMs && now >= browseConnectAtMs) {
    browseConnectAtMs = 0;
    retryAttempts = 0;
    if (browseRadio == 0) {
      if (!radioOff) requestRadioOff();
    } else {
      requestStation(browseRadio - 1);
      mixAutoSel = browseBed;     // keep the hand-picked bed (undo the roll)
      g_radioGateMs = browseBed > 0 ? millis() + 4000 : 0;   // choreography always
    }
  }

  // No auto-return timeout: menu/demo/info screens are left only by a button
  // (short press = back/select, 3s hold = exit). The screen may still black out.

  if (volSaveAtMs && now >= volSaveAtMs) {
    volSaveAtMs = 0;
    prefs.putInt("volume", volumeLevel);
  }


  if (alarmSaveAtMs && now >= alarmSaveAtMs) {
    alarmSaveAtMs = 0;
    prefs.putInt("alarmH", alarmHour);
    prefs.putInt("alarmM", alarmMinute);
  }

  if (now - lastSecond >= 1000) {
    lastSecond = now;
    updateBattery();
    checkAlarm();

    // Lag watchdog: a marginal long-haul stream drains the input buffer and
    // limps ("lags after a few minutes, sometimes recovers"). If the buffer
    // sits nearly empty for 12s while playing, a clean reconnect gets a fresh
    // server/edge and recovers in seconds instead of minutes. Rate-limited.
    static int lowBufSecs = 0;
    static uint32_t lastLagReconnect = 0;
    if (!radioOff && !g_loading && audio.isRunning() &&
        audio.inBufferFilled() < 8192) {
      lowBufSecs++;
    } else {
      lowBufSecs = 0;
    }
    if (lowBufSecs >= 12 && now - lastLagReconnect > 60000) {
      lastLagReconnect = now;
      lowBufSecs = 0;
      Serial.println("[audio] buffer starved 12s — reconnecting for recovery");
      int keepBed = mixAutoSel;         // recovery, not a new combo
      requestStation(currentStation);
      mixAutoSel = keepBed;
      g_radioGateMs = 0;                // and no re-entrance theatre
    }

    // A LiPo that's really empty shouldn't die mid-song (and shouldn't be
    // over-discharged): 10 consecutive seconds under ~3% on battery power →
    // deliberate shutdown into deep sleep. Waking is one button press away.
    static int lowBatSecs = 0;
    bool critical = batteryPresent() && g_batPct <= 3 && g_batMvEma < 3400.0f;
    lowBatSecs = critical ? lowBatSecs + 1 : 0;
    if (lowBatSecs >= 10) {
      Serial.println("[bat] critically low — sleeping to protect the cell");
      enterDeepSleep();   // never returns
    }
  }

  if (now - lastHeartbeat >= 3000) {
    lastHeartbeat = now;
    // WiFi power save silently creeping back on breaks inbound connections
    // (the web remote goes deaf). Detect and re-disable, loudly.
    wifi_ps_type_t ps = WIFI_PS_NONE;
    esp_wifi_get_ps(&ps);
    if (ps != WIFI_PS_NONE) {
      esp_wifi_set_ps(WIFI_PS_NONE);
      Serial.println("[wifi] power-save crept back on — re-disabled");
    }
    Serial.printf("[hb] ticks=%u run=%d kbps=%u heap=%u psram=%u ps=%d web=%u\n",
                  (unsigned)audioTaskTicks, (int)audio.isRunning(),
                  (unsigned)audio.getBitRate(), (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getFreePsram(), (int)ps, (unsigned)g_webHits);
    if (WiFi.status() == WL_CONNECTED) {
      // Unicast the vitals to the last web client (the mesh drops
      // client-to-client broadcast, so unicast is the reliable path; the
      // remote's 2.5s status poll keeps the target fresh). Before anyone
      // has talked to us: broadcast, which at least works on hotspots.
      IPAddress dst = g_telemTarget ? IPAddress(g_telemTarget) : WiFi.broadcastIP();
      telem.beginPacket(dst, 9909);
      telem.printf("[hb] up=%lus run=%d kbps=%u buf=%u heap=%u ps=%d web=%u rssi=%d ip=%s",
                   millis() / 1000, (int)audio.isRunning(),
                   (unsigned)audio.getBitRate(), (unsigned)audio.inBufferFilled(),
                   (unsigned)ESP.getFreeHeap(),
                   (int)ps, (unsigned)g_webHits, WiFi.RSSI(),
                   WiFi.localIP().toString().c_str());
      telem.endPacket();
    }
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

  // Rendering happens in uiTask on core 0 — nothing to do here. The 1ms
  // yield keeps the idle task fed (watchdog, lwIP timers) — encoder rotation
  // is ISR-driven, so nothing is lost.
  delay(1);
}
