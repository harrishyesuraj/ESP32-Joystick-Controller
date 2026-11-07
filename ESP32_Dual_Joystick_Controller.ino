/*
 * ESP32 Dual-Joystick Game Controller  v1.0
 * Developer: HARRISH YESURAJ P
 *
 * Hardware : ESP32 DevKit (classic WROOM-32), 2 analog joysticks, 5 buttons,
 *            0.96" SSD1306 128x64 OLED on 4-wire hardware SPI, 1 green LED.
 * Modes    : Bluetooth LE gamepad | USB serial stream | standalone Dino game
 *
 * LIBRARIES (Library Manager / ZIP):
 *   - Adafruit GFX Library
 *   - Adafruit SSD1306
 *   - NimBLE-Arduino            (by h2zero)
 *   - ESP32-BLE-Gamepad         (by lemmingDev, v0.8.0 or newer)
 *   Board: "ESP32 Dev Module" (esp32 by Espressif core).
 *
 * PINS (from the project document)
 *   J1: VRx 34, VRy 35, SW 13      J2: VRx 36, VRy 39, SW 14
 *   B1 25 (Jump)  B2 26 (Duck)  B3 27 (Start/OK)  B4 32 (Pause/Back)  B5 33 (Mode)
 *   OLED: SCK 18, MOSI 23, RES 17, DC 16, CS 5        LED: GPIO2 via 1k
 */

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>
#include <BleGamepad.h>
#include <stdarg.h>
#include <pgmspace.h>

// ============================== CONFIG ====================================
#define FW_VERSION      "v1.0"
#define SETTINGS_VER    1
#define DEV_NAME        "ESP32 Gamepad"

// If a stick feels reversed (up acts as down, left as right) flip these.
// "false" = pushing UP / LEFT gives a LOW ADC value (most modules).
const bool AX_INVERT[4] = { false, false, false, false };   // J1X, J1Y, J2X, J2Y

#define DEADZONE        0.08f     // 8 % of half-travel
#define NAV_ON          0.60f     // stick as cursor: step threshold
#define NAV_OFF         0.40f     // must fall below this before next step
#define NAV_REPEAT_1    400       // ms before repeat
#define NAV_REPEAT_N    150       // ms between repeats
#define DEBOUNCE_MS     30
#define FRAME_MS        59        // ~17 FPS drawing
#define HOST_MS         10        // gamepad report period
#define BT_SEARCH_MS    60000UL
#define USB_TIMEOUT_MS  3000UL
#define MODE_MENU_MS    1000      // hold bar appears
#define MODE_HOLD_MS    3000      // Settings opens

// ============================== PINS ======================================
#define SCREEN_W   128
#define SCREEN_H   64
#define OLED_SCK   18
#define OLED_MOSI  23
#define OLED_RESET 17
#define OLED_DC    16
#define OLED_CS     5
#define PIN_LED     2

const uint8_t ADC_PIN[4] = { 34, 35, 36, 39 };               // J1X J1Y J2X J2Y
// index: 0..4 = B1..B5, 5 = J1 switch, 6 = J2 switch
const uint8_t BTN_PIN[7] = { 25, 26, 27, 32, 33, 13, 14 };
#define B_JUMP 0
#define B_DUCK 1
#define B_OK   2
#define B_BACK 3
#define B_MODE 4

// ============================== LOGO ======================================
// "PRESENTED BY PHY" logo, 128x64, 1 bit/pixel (from Refine.h, array renamed phy_logo)
const unsigned char phy_logo[] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x0E, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x3F, 0xF8, 0x00, 0x1C, 0xE7, 0xBD, 0xE8, 0xBE, 0xF7, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0xF6, 0x00, 0x12, 0x94, 0x21, 0x0C, 0x88, 0x84, 0x80, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1C, 0xE7, 0x19, 0xCA, 0x88, 0xE4, 0x80, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x70, 0x10, 0xA4, 0x05, 0x09, 0x88, 0x84, 0x80, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x97, 0xBD, 0xE8, 0x88, 0xF7, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x04, 0x00, 0x00, 0x0A, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x0C, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x71, 0x20, 0x00, 0x00,
  0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x49, 0x20, 0x00, 0x00,
  0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x48, 0xE0, 0x00, 0x00,
  0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x70, 0x20, 0x00, 0x00,
  0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x00,
  0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x3F, 0xFF, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x20, 0x00, 0x00, 0x06, 0x90, 0x3F, 0xFF, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x21, 0xC0, 0x00, 0xFF, 0xF8, 0x3F, 0xFF, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x23, 0xF0, 0x07, 0xFF, 0xF8, 0x3F, 0xFF, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x23, 0x50, 0x1F, 0xFF, 0xF0, 0x3F, 0xFF, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x23, 0x3C, 0x3F, 0xFF, 0xF8, 0x00, 0x00, 0x0F, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x23, 0x1C, 0x3F, 0xFF, 0xF8, 0x00, 0x00, 0x0F, 0xBF, 0xFB, 0xFF, 0xBF, 0xEF, 0xFC,
  0x00, 0x00, 0x13, 0x0C, 0x7F, 0xE0, 0x7C, 0x00, 0x00, 0x0F, 0xBF, 0xFB, 0xFF, 0xBF, 0xEF, 0xFC,
  0x00, 0x00, 0x11, 0x5E, 0x5F, 0xC0, 0x38, 0x3F, 0xFF, 0x8F, 0xBF, 0xFB, 0xFF, 0xBF, 0xEF, 0xFC,
  0x00, 0x00, 0x13, 0x06, 0x5E, 0xC0, 0xF0, 0x3F, 0xFF, 0x8F, 0xBF, 0xFB, 0xFF, 0xBF, 0xEF, 0xFC,
  0x00, 0x00, 0x11, 0x0E, 0x5F, 0xE1, 0xF0, 0x3F, 0xFF, 0x8F, 0xBF, 0xFB, 0xFF, 0xBF, 0xEF, 0xFC,
  0x00, 0x00, 0x11, 0xA6, 0x4F, 0xFB, 0xC0, 0x3F, 0xFF, 0x8F, 0xBF, 0xFB, 0xFE, 0x1F, 0x81, 0xF0,
  0x00, 0x00, 0x19, 0x86, 0x4F, 0xFF, 0xC8, 0x3F, 0xFF, 0x8F, 0x87, 0xC0, 0x3E, 0x1F, 0xC3, 0xF0,
  0x00, 0x00, 0x08, 0xC2, 0x4F, 0xFF, 0xC0, 0x00, 0xF8, 0x0F, 0x87, 0xC0, 0x3E, 0x0F, 0xC7, 0xE0,
  0x00, 0x00, 0x09, 0xC3, 0x6F, 0xFF, 0xE0, 0x00, 0xF8, 0x0F, 0x87, 0xC0, 0x3E, 0x07, 0xE7, 0xC0,
  0x00, 0x00, 0x08, 0xCE, 0xFF, 0xFF, 0xE0, 0x00, 0xFF, 0xFF, 0x87, 0xFF, 0xFE, 0x03, 0xFF, 0xC0,
  0x00, 0x00, 0x08, 0x8E, 0x7F, 0xFF, 0xE0, 0x00, 0xFF, 0xFF, 0x87, 0xFF, 0xFE, 0x01, 0xFF, 0x80,
  0x00, 0x00, 0x08, 0xA0, 0xDF, 0xFF, 0xF0, 0x00, 0xFF, 0xFF, 0x87, 0xFF, 0xFE, 0x00, 0xFF, 0x00,
  0x00, 0x00, 0x00, 0x20, 0x7F, 0xFD, 0xF0, 0x00, 0xFF, 0xFF, 0x87, 0xFF, 0xFE, 0x00, 0xFE, 0x00,
  0x00, 0x00, 0x09, 0x20, 0x7F, 0xFE, 0x20, 0x00, 0xFF, 0xFF, 0x87, 0xFF, 0xFE, 0x00, 0x7C, 0x00,
  0x00, 0x00, 0x0D, 0x20, 0x3F, 0xC0, 0x20, 0x00, 0xFC, 0x00, 0x07, 0xC0, 0x3E, 0x00, 0x7C, 0x00,
  0x00, 0x00, 0x3D, 0x30, 0x1F, 0x80, 0x20, 0x00, 0xFC, 0x00, 0x07, 0xC0, 0x3E, 0x00, 0x7E, 0x00,
  0x00, 0x00, 0x1E, 0x30, 0x0F, 0x8C, 0x00, 0x00, 0xFC, 0x00, 0x07, 0xC0, 0x3E, 0x00, 0xFF, 0x00,
  0x00, 0x00, 0x0B, 0x30, 0x07, 0xE7, 0xC0, 0x00, 0xFC, 0x00, 0x07, 0xC0, 0x3E, 0x01, 0xFF, 0x80,
  0x00, 0x00, 0x0B, 0x38, 0x01, 0xB1, 0xC0, 0x0F, 0xFF, 0xF0, 0x3F, 0xFD, 0xFF, 0xC7, 0xFF, 0xE0,
  0x00, 0x00, 0x05, 0xF8, 0x01, 0xF3, 0x00, 0x0F, 0xFF, 0xF0, 0x3F, 0xFD, 0xFF, 0xC7, 0xFF, 0xE0,
  0x00, 0x00, 0x01, 0x20, 0x00, 0xE2, 0x00, 0x0F, 0xFF, 0xF0, 0x3F, 0xFD, 0xFF, 0xC7, 0xFF, 0xE0,
  0x00, 0x00, 0x00, 0x18, 0x00, 0x0C, 0x00, 0x0F, 0xFF, 0xF0, 0x3F, 0xFD, 0xFF, 0xC7, 0xFF, 0xE0,
  0x00, 0x00, 0x00, 0x0C, 0x00, 0x08, 0x00, 0x0F, 0xFF, 0xF0, 0x3F, 0xFD, 0xFF, 0xC7, 0xFF, 0xE0,
  0x00, 0x00, 0x00, 0x07, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x07, 0x81, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x03, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x01, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};


// ============================== TYPES =====================================
enum Mode : uint8_t { MODE_BT = 0, MODE_USB = 1, MODE_DINO = 2 };
enum BtState : uint8_t { BT_WAIT, BT_CONN, BT_NOHOST };

enum Scr : uint8_t {
  SC_HOME_BT, SC_HOME_USB, SC_MODESEL, SC_SETTINGS,
  SC_CAL1, SC_CAL2, SC_CAL3,
  SC_MAPLIST, SC_MAPEDIT, SC_MONITOR, SC_DISPLAY, SC_LED,
  SC_SYSTEM, SC_ABOUT, SC_CONFIRM, SC_WARN,
  SC_DINO_START, SC_DINO_PLAY, SC_DINO_PAUSE, SC_DINO_OVER
};

enum Ev : uint8_t { EV_UP = 1, EV_DOWN, EV_LEFT, EV_RIGHT, EV_OK, EV_BACK, EV_MODE_SHORT, EV_MODE_HOLD };

struct Cal { int16_t mn[4]; int16_t ct[4]; int16_t mx[4]; };
struct Config {
  uint8_t  mode;       // Mode
  uint8_t  map[6];     // target per control (0 None .. 10 Select)
  uint8_t  bri;        // 1..5
  uint8_t  tmo;        // 0 Off, 1 30s, 2 1m, 3 5m
  uint8_t  led;        // 0 Off, 1 On, 2 Status, 3 Activity
  uint32_t hi;         // best score
};
struct Warn { char l[4][22]; };

// ============================== GLOBALS ===================================
Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &SPI, OLED_DC, OLED_RESET, OLED_CS);
Preferences prefs;
BleGamepad bleGamepad(DEV_NAME, "ESP32", 100);
bool bleStarted = false;

Config cfg;
Cal    cal;
bool   calValid = false;

const uint8_t  CONTRAST[5]  = { 26, 64, 128, 191, 255 };
const uint32_t TIMEOUT_MS[4] = { 0, 30000UL, 60000UL, 300000UL };
const char* const TMO_NAME[4] = { "Off", "30s", "1m", "5m" };
const uint8_t  TMO_CELLS[4]  = { 0, 3, 6, 10 };
const char* const LED_NAME[4] = { "Off", "On", "Status", "Activity" };
const char* const MODE_NAME[3] = { "Bluetooth gamepad", "USB mode", "Dino game" };
const char* const TGT_NAME[11] = { "None", "A", "B", "X", "Y", "LB", "RB", "L3", "R3", "Start", "Select" };
const char* const CTL_NAME[6] = { "B1 Jump/UP", "B2 Duck/DOWN", "B3 Start", "B4 Back", "J1 click", "J2 click" };
const char* const AX_NAME[4]  = { "J1X", "J1Y", "J2X", "J2Y" };
const uint8_t DEF_MAP[6] = { 1, 2, 9, 10, 7, 8 };   // A B Start Select L3 R3

// inputs
int   rawAx[4];
float ax[4];
bool  btn[7], btnPrevRaw[7];
uint32_t btnT[7];
bool  pressEdge[7], relEdge[7];

// nav / events
uint8_t evq[10]; uint8_t evn = 0;
bool     navAct[4]; uint32_t navNext[4];
bool     modeDown = false, modeFired = false, modeSwallow = false;
uint32_t modeT = 0;

// display / screen
Scr  scr = SC_HOME_BT, retScr = SC_HOME_BT;
bool displayOn = true, needRedraw = true;
uint32_t lastAct = 0, lastFrame = 0, ledActUntil = 0;

// link state
BtState  btState = BT_WAIT;
uint32_t btWaitT = 0, usbLastMsg = 0;
bool     usbConn = false;
uint16_t lastMask = 0; int16_t lastHid[4] = { 16383, 16383, 16383, 16383 };
uint32_t lastHostT = 0, lastUsbT = 0;

// menu vars
uint8_t selMode, selSet = 0, selMap, selSys, selConf, dispRow, mapTmp, confKind;
uint32_t okHoldT = 0;

// calibration wizard
Cal  tcal; uint32_t calStartT = 0; bool calFirst = false, calPass = false;
char calFailMsg[22];

// warnings
Warn wq[4]; uint8_t wn = 0, wi = 0;
bool settingsWereReset = false;

// ----- Dino -----
#define GROUND_Y 60
#define DINO_X   10
#define GRAV     900.0f
#define JUMP_V   230.0f
struct Obs { bool on; uint8_t type; float x; bool counted; };
Obs   obs[3];
const uint8_t OW[4] = { 6, 10, 16, 12 };
const uint8_t OH[4] = { 12, 16, 12, 8 };
#define BIRD_BOT 11
float    dinoH = 0, dinoV = 0, gameSpeed = 70, nextGap = 60;
bool     ducking = false, newHi = false;
uint32_t score = 0, passed = 0, overT = 0, animMs = 0;

// ============================== HELPERS ===================================
void P(uint8_t r, uint8_t c, const char* f, ...) {
  char b[40]; va_list a; va_start(a, f); vsnprintf(b, sizeof b, f, a); va_end(a);
  display.setCursor(c * 6, r * 8); display.print(b);
}
void PR(uint8_t r, const char* s) {                 // right aligned
  display.setCursor((21 - strlen(s)) * 6, r * 8); display.print(s);
}
void PCtr(int y, const char* s, uint8_t sz) {       // centred, pixel y
  int w = strlen(s) * 6 * sz;
  display.setTextSize(sz); display.setCursor((SCREEN_W - w) / 2, y); display.print(s);
  display.setTextSize(1);
}
void header(const char* t, const char* right) {
  display.fillRect(0, 0, 128, 8, WHITE); display.setTextColor(BLACK);
  P(0, 0, "%s", t); if (right) PR(0, right);
  display.setTextColor(WHITE);
}
void footer(const char* s) { P(7, 0, "%s", s); }
void item(uint8_t r, const char* s, const char* val, bool sel) {
  if (sel) { display.fillRect(0, r * 8, 128, 8, WHITE); display.setTextColor(BLACK); }
  P(r, 0, "%c %s", sel ? '>' : ' ', s);
  if (val) PR(r, val);
  display.setTextColor(WHITE);
}
void cellBar(int x, int y, int filled, int total, int cw, int ch) {
  for (int i = 0; i < total; i++) {
    if (i < filled) display.fillRect(x + i * (cw + 1), y, cw, ch, WHITE);
    else            display.drawRect(x + i * (cw + 1), y, cw, ch, WHITE);
  }
}
void setContrast(uint8_t v) { display.ssd1306_command(SSD1306_SETCONTRAST); display.ssd1306_command(v); }
void applyBrightness() { setContrast(CONTRAST[constrain(cfg.bri, 1, 5) - 1]); }
void setLed(bool on) { static int8_t last = -1; if (last != (int8_t)on) { digitalWrite(PIN_LED, on); last = on; } }

// ============================== SETTINGS ==================================
void defaultsConfig() {
  cfg.mode = MODE_BT; memcpy(cfg.map, DEF_MAP, 6);
  cfg.bri = 4; cfg.tmo = 2; cfg.led = 2; cfg.hi = 0;
}
void defaultsCal() {
  for (int i = 0; i < 4; i++) { cal.mn[i] = 0; cal.ct[i] = 2048; cal.mx[i] = 4095; }
}
void saveMode()    { prefs.putUChar("mode", cfg.mode); }
void saveMap()     { prefs.putBytes("map", cfg.map, 6); }
void saveDisplay() { prefs.putUChar("bri", cfg.bri); prefs.putUChar("tmo", cfg.tmo); }
void saveLedCfg()  { prefs.putUChar("led", cfg.led); }
void saveHi()      { prefs.putUInt("hi", cfg.hi); }
void saveCal()     { prefs.putBytes("cal", &cal, sizeof(cal)); }

void addWarn(const char* a, const char* b, const char* c, const char* d) {
  if (wn >= 4) return;
  snprintf(wq[wn].l[0], 22, "%s", a); snprintf(wq[wn].l[1], 22, "%s", b);
  snprintf(wq[wn].l[2], 22, "%s", c); snprintf(wq[wn].l[3], 22, "%s", d); wn++;
}

void loadSettings() {
  prefs.begin("ctrl", false);
  defaultsConfig(); defaultsCal(); calValid = false;
  if (prefs.getUChar("ver", 0) != SETTINGS_VER) {
    prefs.clear(); prefs.putUChar("ver", SETTINGS_VER);
    saveMode(); saveMap(); saveDisplay(); saveLedCfg(); saveHi();
    settingsWereReset = true;
    return;
  }
  cfg.mode = prefs.getUChar("mode", MODE_BT);
  if (prefs.getBytesLength("map") == 6) prefs.getBytes("map", cfg.map, 6);
  cfg.bri = prefs.getUChar("bri", 4);
  cfg.tmo = prefs.getUChar("tmo", 2);
  cfg.led = prefs.getUChar("led", 2);
  cfg.hi  = prefs.getUInt("hi", 0);
  if (prefs.getBytesLength("cal") == sizeof(cal)) { prefs.getBytes("cal", &cal, sizeof(cal)); calValid = true; }
  // sanity
  if (cfg.mode > 2) cfg.mode = MODE_BT;
  for (int i = 0; i < 6; i++) if (cfg.map[i] > 10) cfg.map[i] = DEF_MAP[i];
  cfg.bri = constrain(cfg.bri, 1, 5); if (cfg.tmo > 3) cfg.tmo = 2; if (cfg.led > 3) cfg.led = 2;
}

// ============================== INPUT =====================================
int readAdc(uint8_t pin) { int s = 0; for (int i = 0; i < 4; i++) s += analogRead(pin); return s >> 2; }

float normAxis(uint8_t i, int raw) {
  float v;
  if (raw >= cal.ct[i]) { float sp = cal.mx[i] - cal.ct[i]; v = sp > 50 ? (raw - cal.ct[i]) / sp : 0; }
  else                  { float sp = cal.ct[i] - cal.mn[i]; v = sp > 50 ? -(float)(cal.ct[i] - raw) / sp : 0; }
  v = constrain(v, -1.0f, 1.0f);
  if (AX_INVERT[i]) v = -v;
  float a = fabsf(v);
  if (a < DEADZONE) return 0;
  a = (a - DEADZONE) / (1.0f - DEADZONE);
  return v < 0 ? -a : a;
}

void pushEv(uint8_t e) { if (evn < sizeof(evq)) evq[evn++] = e; }

bool modeAllowed() {
  return scr == SC_HOME_BT || scr == SC_HOME_USB || scr == SC_DINO_START ||
         scr == SC_DINO_PAUSE || scr == SC_DINO_OVER;
}

void navStep(uint8_t d, float val, uint8_t ev, uint32_t now) {
  if (!navAct[d]) {
    if (val > NAV_ON) { navAct[d] = true; navNext[d] = now + NAV_REPEAT_1; pushEv(ev); }
  } else {
    if (val < NAV_OFF) navAct[d] = false;
    else if (now >= navNext[d]) { navNext[d] = now + NAV_REPEAT_N; pushEv(ev); }
  }
}

// returns true if any user activity happened this pass
bool readInputs(uint32_t now) {
  bool act = false;
  for (int i = 0; i < 4; i++) { rawAx[i] = readAdc(ADC_PIN[i]); ax[i] = normAxis(i, rawAx[i]); if (ax[i] != 0) act = true; }
  for (int i = 0; i < 7; i++) {
    pressEdge[i] = relEdge[i] = false;
    bool raw = (digitalRead(BTN_PIN[i]) == LOW);
    if (raw != btnPrevRaw[i]) { btnPrevRaw[i] = raw; btnT[i] = now; }
    if (raw != btn[i] && now - btnT[i] >= DEBOUNCE_MS) {
      btn[i] = raw;
      if (raw) { pressEdge[i] = true; act = true; } else relEdge[i] = true;
    }
  }
  return act;
}

void genEvents(uint32_t now) {
  evn = 0;
  navStep(0, -ax[1], EV_UP,    now);
  navStep(1,  ax[1], EV_DOWN,  now);
  navStep(2, -ax[0], EV_LEFT,  now);
  navStep(3,  ax[0], EV_RIGHT, now);
  if (pressEdge[B_OK])   pushEv(EV_OK);
  if (pressEdge[B_BACK]) pushEv(EV_BACK);

  // Button 5: short press / hold 3 s
  if (pressEdge[B_MODE]) { modeDown = true; modeFired = false; modeT = now; modeSwallow = !displayOn; }
  if (modeDown) {
    uint32_t held = now - modeT;
    if (!modeFired && held >= MODE_HOLD_MS) {
      modeFired = true;
      if (modeAllowed() && !modeSwallow) pushEv(EV_MODE_HOLD);
    }
    if (relEdge[B_MODE] || !btn[B_MODE]) {
      if (!modeFired && held < MODE_MENU_MS && modeAllowed() && !modeSwallow) pushEv(EV_MODE_SHORT);
      modeDown = false;
    }
  }
}

// ============================== BLUETOOTH / USB ===========================
void startBle() {
  if (bleStarted) return;
  BleGamepadConfiguration c;
  c.setAutoReport(false);
  c.setButtonCount(10);
  c.setHatSwitchCount(0);
  c.setWhichAxes(true, true, true, false, false, true, false, false);  // X Y Z RZ
  c.setAxesMin(0);
  c.setAxesMax(32767);
  bleGamepad.begin(&c);
  bleGamepad.setLeftThumb(16383, 16383);
  bleGamepad.setRightThumb(16383, 16383);
  bleStarted = true;
}

// 11-bit mask of gamepad buttons (bit n-1 = gamepad button n) from the mapping
uint16_t padMask() {
  const bool st[6] = { btn[0], btn[1], btn[2], btn[3], btn[5], btn[6] };
  uint16_t m = 0;
  for (int c = 0; c < 6; c++) if (st[c] && cfg.map[c]) m |= (1u << (cfg.map[c] - 1));
  return m;
}
int16_t toHid(float v) { return (int16_t)((v + 1.0f) * 16383.5f); }

void btSend(uint16_t mask, const int16_t h[4]) {
  for (int b = 1; b <= 10; b++) {
    bool want = mask & (1u << (b - 1)), had = lastMask & (1u << (b - 1));
    if (want && !had) bleGamepad.press(b);
    if (!want && had) bleGamepad.release(b);
  }
  bleGamepad.setLeftThumb(h[0], h[1]);
  bleGamepad.setRightThumb(h[2], h[3]);
  bleGamepad.sendReport();
}

void sendHost(uint32_t now) {
  bool onHome = (scr == SC_HOME_BT || scr == SC_HOME_USB);
  uint16_t mask = 0; int16_t h[4] = { 16383, 16383, 16383, 16383 };
  if (onHome) { mask = padMask(); for (int i = 0; i < 4; i++) h[i] = toHid(ax[i]); }
  bool changed = (mask != lastMask) || memcmp(h, lastHid, sizeof h) != 0;
  if (!onHome && !changed) return;                              // already neutral

  if (cfg.mode == MODE_BT && bleStarted && btState == BT_CONN) {
    if (now - lastHostT >= HOST_MS && changed) {
      btSend(mask, h); lastMask = mask; memcpy(lastHid, h, sizeof h); lastHostT = now;
    }
  } else if (cfg.mode == MODE_USB && (onHome || changed)) {
    if ((changed && now - lastUsbT >= HOST_MS) || (onHome && now - lastUsbT >= 500)) {
      float a[4] = { 0, 0, 0, 0 }; if (onHome) for (int i = 0; i < 4; i++) a[i] = ax[i];
      Serial.printf("$,%d,%d,%d,%d,%u\n", (int)(a[0] * 1000), (int)(a[1] * 1000),
                    (int)(a[2] * 1000), (int)(a[3] * 1000), (unsigned)mask);
      lastMask = mask; memcpy(lastHid, h, sizeof h); lastUsbT = now;
    }
  } else if (!onHome) {                                         // left Home: drop everything
    lastMask = 0; for (int i = 0; i < 4; i++) lastHid[i] = 16383;
  }
}

void updateLink(uint32_t now) {
  if (cfg.mode == MODE_BT && bleStarted) {
    bool c = bleGamepad.isConnected();
    if (c) btState = BT_CONN;
    else if (btState == BT_CONN) { btState = BT_WAIT; btWaitT = now; lastMask = 0; }
    else if (btState == BT_WAIT && now - btWaitT > BT_SEARCH_MS) btState = BT_NOHOST;
  }
  if (cfg.mode == MODE_USB) {
    while (Serial.available()) { Serial.read(); usbLastMsg = now; }
    usbConn = usbLastMsg && (now - usbLastMsg < USB_TIMEOUT_MS);
  }
}

// ============================== SCREEN CONTROL ============================
void gotoScr(Scr s) {
  scr = s; needRedraw = true; lastAct = millis();
  if (s == SC_CONFIRM) { selConf = 0; okHoldT = 0; }
  if (s == SC_DISPLAY) dispRow = 0;
  if (s == SC_SYSTEM)  { selSys = 0; }
  if (s == SC_MAPLIST) { if (selMap > 5) selMap = 0; }
}

Scr homeFor(uint8_t m) { return m == MODE_BT ? SC_HOME_BT : (m == MODE_USB ? SC_HOME_USB : SC_DINO_START); }

void enterMode() {
  switch (cfg.mode) {
    case MODE_BT:
      startBle();
      btState = bleGamepad.isConnected() ? BT_CONN : BT_WAIT; btWaitT = millis();
      break;
    case MODE_USB: usbLastMsg = 0; usbConn = false; break;
    default: break;
  }
  gotoScr(homeFor(cfg.mode));
}

void afterBoot() {                      // after wizard / warnings
  wi = 0;
  if (wn > 0) gotoScr(SC_WARN); else enterMode();
}

void openModeSelect() { retScr = scr; selMode = cfg.mode; gotoScr(SC_MODESEL); }
void openSettings()   { retScr = scr; gotoScr(SC_SETTINGS); }

void sleepDisplay() { display.ssd1306_command(SSD1306_DISPLAYOFF); displayOn = false; }
void wakeDisplay()  { display.ssd1306_command(SSD1306_DISPLAYON);  displayOn = true; needRedraw = true; }

// ============================== CALIBRATION ===============================
void calMeasureCentre() {
  long sum[4] = { 0, 0, 0, 0 };
  for (int k = 0; k < 64; k++) { for (int i = 0; i < 4; i++) sum[i] += analogRead(ADC_PIN[i]); delay(7); }
  for (int i = 0; i < 4; i++) { tcal.ct[i] = sum[i] / 64; tcal.mn[i] = tcal.ct[i]; tcal.mx[i] = tcal.ct[i]; }
}
void calEvaluate() {
  calPass = true; calFailMsg[0] = 0;
  for (int i = 0; i < 4; i++) {
    if (tcal.ct[i] < 1500 || tcal.ct[i] > 2600) { calPass = false; snprintf(calFailMsg, 22, "%s centre off", AX_NAME[i]); break; }
    if (tcal.mx[i] - tcal.mn[i] < 3000)         { calPass = false; snprintf(calFailMsg, 22, "%s span too small", AX_NAME[i]); break; }
  }
}
void calCancel() {
  if (calFirst) { calFirst = false; defaultsCal(); afterBoot(); }
  else gotoScr(SC_SETTINGS);
}
void calTick(uint32_t now) {
  for (int i = 0; i < 4; i++) {
    if (rawAx[i] < tcal.mn[i]) tcal.mn[i] = rawAx[i];
    if (rawAx[i] > tcal.mx[i]) tcal.mx[i] = rawAx[i];
  }
  if (now - calStartT >= 5000) { calEvaluate(); gotoScr(SC_CAL3); }
}

// ============================== DINO GAME =================================
void startGame() {
  for (auto& o : obs) o.on = false;
  dinoH = dinoV = 0; ducking = false; score = 0; passed = 0; newHi = false;
  gameSpeed = 70; nextGap = 50; animMs = 0;
  gotoScr(SC_DINO_PLAY);
}
void gameOver() {
  newHi = score > cfg.hi;
  if (newHi) { cfg.hi = score; saveHi(); }
  overT = millis(); gotoScr(SC_DINO_OVER);
}
void gameInput() {                      // every loop pass: low-latency jump/duck
  bool up = btn[B_JUMP] || ax[1] < -0.5f, dn = btn[B_DUCK] || ax[1] > 0.5f;
  bool ground = (dinoH <= 0.01f && dinoV <= 0);
  if (up && ground) { dinoV = JUMP_V; dinoH = 0.5f; }
  ducking = dn && ground;
}
void gameStep(float dt) {
  if (dt > 0.1f) dt = 0.1f;
  animMs += (uint32_t)(dt * 1000);
  if (dinoH > 0 || dinoV > 0) {
    bool dn = btn[B_DUCK] || ax[1] > 0.5f;
    dinoV -= GRAV * (dn ? 2.0f : 1.0f) * dt; dinoH += dinoV * dt;
    if (dinoH <= 0) { dinoH = 0; dinoV = 0; }
  }
  uint32_t lvl = passed / 10; if (lvl > 8) lvl = 8;
  gameSpeed = 70 + 10 * lvl;

  float rightmost = -1000; bool any = false;
  for (auto& o : obs) if (o.on) {
    o.x -= gameSpeed * dt;
    if (!o.counted && o.x + OW[o.type] < DINO_X) { o.counted = true; score += 10; passed++; }
    if (o.x + OW[o.type] < 0) o.on = false;
    else { any = true; float re = o.x + OW[o.type]; if (re > rightmost) rightmost = re; }
  }
  if (!any || (128 - rightmost) >= nextGap) {
    for (auto& o : obs) if (!o.on) {
      o.on = true; o.counted = false; o.x = 128;
      o.type = (passed >= 5 && random(100) < 30) ? 3 : random(3);
      nextGap = random(55, 110) + gameSpeed * 0.3f;
      break;
    }
  }
  // collision
  float dx1, dx2, dB = dinoH, dT;
  if (ducking) { dx1 = DINO_X + 1; dx2 = DINO_X + 15; dT = dB + 9; }
  else         { dx1 = DINO_X + 3; dx2 = DINO_X + 12; dT = dB + 14; }
  for (auto& o : obs) if (o.on) {
    float ox1 = o.x + 1, ox2 = o.x + OW[o.type] - 1;
    float oB = (o.type == 3) ? BIRD_BOT : 0, oT = oB + OH[o.type];
    if (dx1 < ox2 && dx2 > ox1 && dB < oT && dT > oB) { gameOver(); return; }
  }
}

// ---- Dino drawing ----
void drawDino(int x, int yBottom, bool duck, bool run) {
  bool f = run && ((animMs / 120) % 2);
  if (!duck) {
    int y = yBottom - 16;
    display.fillRect(x + 6, y, 8, 6, WHITE);              // head
    display.drawPixel(x + 11, y + 1, BLACK);              // eye
    display.fillRect(x + 11, y + 4, 3, 1, BLACK);         // mouth
    display.fillRect(x + 3, y + 6, 9, 6, WHITE);          // body
    display.fillRect(x, y + 6, 3, 3, WHITE);              // tail
    display.fillRect(x + 12, y + 9, 2, 1, WHITE);         // arm
    display.fillRect(x + 4, y + 12, 2, f ? 4 : 2, WHITE); // legs
    display.fillRect(x + 9, y + 12, 2, f ? 2 : 4, WHITE);
  } else {
    int y = yBottom - 9;
    display.fillRect(x + 10, y, 7, 5, WHITE);             // head
    display.drawPixel(x + 14, y + 1, BLACK);
    display.fillRect(x, y + 3, 12, 4, WHITE);             // body
    display.fillRect(x + 2, y + 7, 2, f ? 2 : 1, WHITE);
    display.fillRect(x + 8, y + 7, 2, f ? 1 : 2, WHITE);
  }
}
void drawCactus(int x, int w, int h) {
  int y = GROUND_Y - h;
  display.fillRect(x + w / 2 - 1, y, 3, h, WHITE);
  display.fillRect(x, y + 4, 2, 5, WHITE);  display.fillRect(x, y + 8, w / 2, 1, WHITE);
  if (w >= 10) { display.fillRect(x + w - 2, y + 3, 2, 5, WHITE); display.fillRect(x + w / 2 + 1, y + 7, w / 2 - 1, 1, WHITE); }
}
void drawBird(int x, int bottomH) {
  int y = GROUND_Y - bottomH - 8;
  display.fillRect(x + 2, y + 3, 8, 3, WHITE);
  display.fillRect(x + 9, y + 2, 3, 3, WHITE); display.drawPixel(x + 12, y + 3, WHITE);
  display.drawPixel(x, y + 4, WHITE);
  if ((animMs / 150) % 2) display.fillRect(x + 4, y, 3, 3, WHITE); else display.fillRect(x + 4, y + 6, 3, 2, WHITE);
}
void drawField() {
  display.drawFastHLine(0, GROUND_Y, 128, WHITE);
  for (int i = 0; i < 6; i++) display.drawPixel((i * 23 + (int)(animMs / 20) * 0) % 128, GROUND_Y + 2 + (i % 2) * 1, WHITE);
  for (auto& o : obs) if (o.on) {
    int x = (int)o.x;
    if (o.type == 0) drawCactus(x, 6, 12);
    else if (o.type == 1) drawCactus(x, 10, 16);
    else if (o.type == 2) { drawCactus(x, 6, 12); drawCactus(x + 9, 6, 12); }
    else drawBird(x, BIRD_BOT);
  }
  drawDino(DINO_X, GROUND_Y - (int)dinoH, ducking, dinoH <= 0.01f && scr == SC_DINO_PLAY);
  P(0, 0, "HI %05lu", (unsigned long)cfg.hi);
  char b[10]; snprintf(b, sizeof b, "%05lu", (unsigned long)score); PR(0, b);
}

// ============================== DRAWING ===================================
void drawHome(bool bt) {
  header(bt ? "GAMEPAD" : "USB MODE", bt ? "[BT]" : "[USB]");
  P(1, 0, "Mode:   %s", bt ? "Bluetooth" : "USB (serial)");
  bool conn = bt ? (btState == BT_CONN) : usbConn;
  if (conn) {
    P(2, 0, "Status: CONNECTED");
    P(4, 0, "J1 %5.2f %5.2f", ax[0], ax[1]);
    P(5, 0, "J2 %5.2f %5.2f", ax[2], ax[3]);
    char b[16]; int n = 0; b[n++] = 'B'; b[n++] = 'T'; b[n++] = 'N'; b[n++] = ' ';
    for (int i = 0; i < 5; i++) { b[n++] = btn[i] ? ('1' + i) : '.'; b[n++] = ' '; }
    b[n] = 0; P(6, 0, "%s", b);
  } else if (bt && btState == BT_NOHOST) {
    P(2, 0, "Status: No host found");
    P(4, 0, "Is Bluetooth on at"); P(5, 0, "the phone or PC?"); P(6, 0, "OK:Retry");
  } else {
    P(2, 0, "Status: Waiting for"); P(3, 0, "        connection...");
    if (bt) { P(4, 0, "Name: %s", DEV_NAME); P(5, 0, "Pair from phone or PC"); }
    else    { P(4, 0, "Serial link 115200"); P(5, 0, "Open the port on a"); P(6, 0, "PC or phone app."); }
    if (bt) { int d = (millis() / 400) % 4; display.setCursor(12 * 6, 48); for (int i = 0; i < d; i++) display.print(". "); }
  }
  footer("MODE:mode   HOLD:menu");
}

void drawModeSel() {
  header("SELECT MODE", NULL);
  for (int i = 0; i < 3; i++) item(2 + i, MODE_NAME[i], NULL, i == selMode);
  P(6, 0, "Current: %s", cfg.mode == MODE_BT ? "Bluetooth" : (cfg.mode == MODE_USB ? "USB" : "Dino game"));
  footer("OK:Select BACK:Cancel");
}

void drawSettings() {
  static const char* const L[6] = { "Calibration", "Button mapping", "Input monitor", "Display", "LED", "System" };
  char r[8]; snprintf(r, sizeof r, "%u/6", selSet + 1);
  header("SETTINGS", r);
  for (int i = 0; i < 6; i++) item(1 + i, L[i], NULL, i == selSet);
  footer("OK:Open  BACK:Exit");
}

void drawCal() {
  if (scr == SC_CAL1) {
    header("CALIBRATION", "1/3");
    P(2, 0, "Release both sticks"); P(3, 0, "and keep still.");
    P(5, 0, "Then press OK.");
    footer("OK:Start BACK:Cancel");
  } else if (scr == SC_CAL2) {
    header("CALIBRATION", "2/3");
    P(1, 0, "Rotate both sticks"); P(2, 0, "to every edge.");
    for (int j = 0; j < 2; j++) {
      int a = j * 2;
      float cov = ((tcal.mx[a] - tcal.mn[a]) + (tcal.mx[a + 1] - tcal.mn[a + 1])) / 2.0f / 3500.0f;
      P(4 + j, 0, "J%d", j + 1);
      cellBar(24, (4 + j) * 8 + 1, constrain((int)(cov * 10), 0, 10), 10, 5, 6);
    }
    uint32_t el = millis() - calStartT; if (el > 5000) el = 5000;
    P(6, 0, "Time left: %lu s", (unsigned long)((5000 - el) / 1000 + 1));
  } else {
    header("CALIBRATION", "3/3");
    P(1, 0, "Result:"); PR(1, calPass ? "PASS" : "FAIL");
    for (int i = 0; i < 4; i++) P(2 + i, 0, "%s  %d/%d/%d", AX_NAME[i], tcal.mn[i], tcal.ct[i], tcal.mx[i]);
    if (calPass) P(6, 0, "min/centre/max"); else P(6, 0, "%s", calFailMsg);
    footer(calPass ? "OK:Save BACK:Discard" : "OK:Retry BACK:Cancel");
  }
}

void drawMapList() {
  header("BUTTON MAPPING", NULL);
  for (int i = 0; i < 6; i++) item(1 + i, CTL_NAME[i], TGT_NAME[cfg.map[i]], i == selMap);
  footer("OK:Edit  BACK:Exit");
}
void drawMapEdit() {
  char t[16]; snprintf(t, sizeof t, "MAP: %.2s", CTL_NAME[selMap]);
  header(t, NULL);
  P(2, 0, "Now sends:");
  char b[24]; snprintf(b, sizeof b, "<  %s  >", TGT_NAME[mapTmp]);
  PCtr(36, b, 1);
  char c[16]; snprintf(c, sizeof c, "%u of 11", mapTmp + 1); PCtr(48, c, 1);
  footer("OK:Save BACK:Cancel");
}

void drawMonitor() {
  header("INPUT MONITOR", NULL);
  display.drawRect(0, 10, 32, 32, WHITE); display.drawRect(96, 10, 32, 32, WHITE);
  display.fillRect(16 + (int)(ax[0] * 13) - 1, 26 + (int)(ax[1] * 13) - 1, 3, 3, WHITE);
  display.fillRect(112 + (int)(ax[2] * 13) - 1, 26 + (int)(ax[3] * 13) - 1, 3, 3, WHITE);
  display.setCursor(38, 12); display.print("1 2 3 4 5");
  char b[12]; int n = 0;
  for (int i = 0; i < 5; i++) { b[n++] = btn[i] ? '#' : '.'; b[n++] = ' '; } b[n - 1] = 0;
  display.setCursor(38, 22); display.print(b);
  display.setCursor(38, 34); display.printf("S1:%c S2:%c", btn[5] ? '#' : '-', btn[6] ? '#' : '-');
  P(6, 0, "%4d,%4d %4d,%4d", rawAx[0], rawAx[1], rawAx[2], rawAx[3]);
  footer("BACK:Exit");
}

void drawDisplay() {
  header("DISPLAY", NULL);
  item(2, "Brightness", NULL, dispRow == 0);
  display.setCursor(8, 28); display.print("<");
  cellBar(18, 28, cfg.bri * 2, 10, 5, 6);
  display.setCursor(84, 28); display.printf("%u", cfg.bri); display.setCursor(116, 28); display.print(">");
  item(5, "Timeout", NULL, dispRow == 1);
  display.setCursor(8, 48); display.print("<");
  cellBar(18, 48, TMO_CELLS[cfg.tmo], 10, 5, 6);
  display.setCursor(84, 48); display.print(TMO_NAME[cfg.tmo]); display.setCursor(116, 48); display.print(">");
  footer("<>:Change BACK:Save");
}

void drawLedScr() {
  header("LED", NULL);
  item(2, "Mode", NULL, true);
  char b[20]; snprintf(b, sizeof b, "< %s >", LED_NAME[cfg.led]);
  display.setTextColor(BLACK); PR(2, b); display.setTextColor(WHITE);
  static const char* const H1[4] = { "LED always off.", "LED always on.", "Blinks while pairing,", "Flashes on every" };
  static const char* const H2[4] = { "", "", "solid when connected.", "button or stick move." };
  P(4, 0, "%s", H1[cfg.led]); P(5, 0, "%s", H2[cfg.led]);
  footer("<>:Change BACK:Save");
}

void drawSystem() {
  static const char* const L[4] = { "About", "Reset mapping", "Reset high score", "Factory reset" };
  header("SYSTEM", NULL);
  for (int i = 0; i < 4; i++) item(1 + i, L[i], NULL, i == selSys);
  footer("OK:Open  BACK:Back");
}
void drawAbout() {
  header("ABOUT", NULL);
  P(1, 0, "ESP32 Controller %s", FW_VERSION);
  P(2, 0, "Chip   %s", ESP.getChipModel());
  P(3, 0, "Heap   %lu KB free", (unsigned long)(ESP.getFreeHeap() / 1024));
  uint32_t s = millis() / 1000;
  P(4, 0, "Uptime %02lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)((s / 60) % 60), (unsigned long)(s % 60));
  P(5, 0, "Developer:"); P(6, 0, "HARRISH YESURAJ P");
  footer("BACK:Exit");
}
void drawConfirm() {
  static const char* const CT[4] = { "", "RESET MAPPING", "RESET HIGH SCORE", "FACTORY RESET" };
  static const char* const CA[4] = { "", "Restore default", "Reset best score", "Erase ALL settings" };
  static const char* const CB[4] = { "", "button mapping?", "to zero?", "and restart?" };
  header(CT[confKind], NULL);
  P(2, 0, "%s", CA[confKind]); P(3, 0, "%s", CB[confKind]);
  item(5, "No", NULL, selConf == 0); item(6, "Yes", NULL, selConf == 1);
  if (confKind == 3 && selConf == 1) {
    footer("Hold OK 2 s");
    if (okHoldT) { int w = constrain((int)((millis() - okHoldT) * 40 / 2000), 0, 40); display.drawRect(80, 56, 42, 8, WHITE); display.fillRect(81, 57, w, 6, WHITE); }
  } else footer("OK:Select BACK:Cancel");
}
void drawWarn() {
  header("! WARNING", NULL);
  for (int i = 0; i < 4; i++) P(2 + i, 0, "%s", wq[wi].l[i]);
  footer("OK:Continue");
}

void drawDinoScr() {
  if (scr == SC_DINO_START) {
    P(0, 0, "HI %05lu", (unsigned long)cfg.hi);
    PCtr(14, "DINO RUN", 2); PCtr(36, "PRESS START", 1);
    for (auto& o : obs) o.on = false;
    display.drawFastHLine(0, GROUND_Y, 128, WHITE);
    drawDino(DINO_X, GROUND_Y, false, false);
    drawCactus(70, 6, 12); drawBird(100, 11);
  } else if (scr == SC_DINO_PLAY) {
    drawField();
  } else if (scr == SC_DINO_PAUSE) {
    drawField();
    display.fillRect(20, 14, 88, 34, BLACK); display.drawRect(20, 14, 88, 34, WHITE);
    PCtr(18, "PAUSED", 1); PCtr(30, "START: resume", 1); PCtr(39, "BACK: quit", 1);
  } else {
    PCtr(8, "GAME OVER", 2);
    P(4, 0, "Score  %05lu", (unsigned long)score);
    if (newHi) P(5, 0, "NEW HI! %05lu", (unsigned long)cfg.hi); else P(5, 0, "HI     %05lu", (unsigned long)cfg.hi);
    P(6, 0, "START: retry"); P(7, 0, "BACK: exit");
  }
}

void render(uint32_t now) {
  display.clearDisplay(); display.setTextColor(WHITE); display.setTextSize(1); display.setTextWrap(false);
  switch (scr) {
    case SC_HOME_BT: drawHome(true); break;
    case SC_HOME_USB: drawHome(false); break;
    case SC_MODESEL: drawModeSel(); break;
    case SC_SETTINGS: drawSettings(); break;
    case SC_CAL1: case SC_CAL2: case SC_CAL3: drawCal(); break;
    case SC_MAPLIST: drawMapList(); break;
    case SC_MAPEDIT: drawMapEdit(); break;
    case SC_MONITOR: drawMonitor(); break;
    case SC_DISPLAY: drawDisplay(); break;
    case SC_LED: drawLedScr(); break;
    case SC_SYSTEM: drawSystem(); break;
    case SC_ABOUT: drawAbout(); break;
    case SC_CONFIRM: drawConfirm(); break;
    case SC_WARN: drawWarn(); break;
    default: drawDinoScr(); break;
  }
  // menu-hold progress overlay (replaces footer row)
  if (modeDown && !modeFired && modeAllowed() && !modeSwallow && now - modeT >= MODE_MENU_MS) {
    display.fillRect(0, 56, 128, 8, BLACK);
    P(7, 0, "Menu");
    int cells = constrain((int)((now - modeT - MODE_MENU_MS) * 10 / (MODE_HOLD_MS - MODE_MENU_MS)), 0, 10);
    cellBar(32, 57, cells, 10, 7, 6);
  }
  display.display();
}

bool dynamicScreen(uint32_t now) {
  switch (scr) {
    case SC_HOME_BT: case SC_HOME_USB: case SC_CAL2: case SC_MONITOR:
    case SC_DINO_PLAY: case SC_ABOUT: case SC_CONFIRM: return true;
    default: break;
  }
  return modeDown && now - modeT >= MODE_MENU_MS;
}

// ============================== EVENT HANDLING ============================
void modeCommon(uint8_t ev) {
  if (ev == EV_MODE_SHORT) openModeSelect();
  else if (ev == EV_MODE_HOLD) openSettings();
}

void doConfirm() {
  if (confKind == 1) { memcpy(cfg.map, DEF_MAP, 6); saveMap(); }
  else if (confKind == 2) { cfg.hi = 0; saveHi(); }
  else if (confKind == 3) { prefs.clear(); display.clearDisplay(); display.display(); delay(100); ESP.restart(); }
  gotoScr(SC_SYSTEM);
}

void handleEvent(uint8_t ev, uint32_t now) {
  switch (scr) {
    case SC_HOME_BT:
      if (ev == EV_OK && btState == BT_NOHOST) { btState = BT_WAIT; btWaitT = now; }
      else modeCommon(ev);
      break;
    case SC_HOME_USB: modeCommon(ev); break;

    case SC_MODESEL:
      if (ev == EV_UP)   selMode = (selMode + 2) % 3;
      if (ev == EV_DOWN) selMode = (selMode + 1) % 3;
      if (ev == EV_BACK) gotoScr(retScr);
      if (ev == EV_OK) { if (selMode != cfg.mode) { cfg.mode = selMode; saveMode(); } enterMode(); }
      break;

    case SC_SETTINGS:
      if (ev == EV_UP)   selSet = (selSet + 5) % 6;
      if (ev == EV_DOWN) selSet = (selSet + 1) % 6;
      if (ev == EV_BACK) gotoScr(retScr);
      if (ev == EV_OK) {
        static const Scr tgt[6] = { SC_CAL1, SC_MAPLIST, SC_MONITOR, SC_DISPLAY, SC_LED, SC_SYSTEM };
        calFirst = false; gotoScr(tgt[selSet]);
      }
      break;

    case SC_CAL1:
      if (ev == EV_BACK) calCancel();
      if (ev == EV_OK) { calMeasureCentre(); calStartT = millis(); gotoScr(SC_CAL2); }
      break;
    case SC_CAL2: if (ev == EV_BACK) calCancel(); break;
    case SC_CAL3:
      if (ev == EV_BACK) calCancel();
      if (ev == EV_OK) {
        if (calPass) {
          cal = tcal; saveCal(); calValid = true;
          if (calFirst) { calFirst = false; afterBoot(); } else gotoScr(SC_SETTINGS);
        } else gotoScr(SC_CAL1);
      }
      break;

    case SC_MAPLIST:
      if (ev == EV_UP)   selMap = (selMap + 5) % 6;
      if (ev == EV_DOWN) selMap = (selMap + 1) % 6;
      if (ev == EV_BACK) gotoScr(SC_SETTINGS);
      if (ev == EV_OK)   { mapTmp = cfg.map[selMap]; gotoScr(SC_MAPEDIT); }
      break;
    case SC_MAPEDIT:
      if (ev == EV_LEFT)  mapTmp = (mapTmp + 10) % 11;
      if (ev == EV_RIGHT) mapTmp = (mapTmp + 1) % 11;
      if (ev == EV_BACK)  gotoScr(SC_MAPLIST);
      if (ev == EV_OK)    { cfg.map[selMap] = mapTmp; saveMap(); gotoScr(SC_MAPLIST); }
      break;

    case SC_MONITOR: if (ev == EV_BACK) gotoScr(SC_SETTINGS); break;

    case SC_DISPLAY:
      if (ev == EV_UP || ev == EV_DOWN) dispRow ^= 1;
      if (ev == EV_LEFT || ev == EV_RIGHT) {
        int d = (ev == EV_RIGHT) ? 1 : -1;
        if (dispRow == 0) { cfg.bri = constrain((int)cfg.bri + d, 1, 5); applyBrightness(); }
        else cfg.tmo = constrain((int)cfg.tmo + d, 0, 3);
      }
      if (ev == EV_BACK) { saveDisplay(); gotoScr(SC_SETTINGS); }
      break;

    case SC_LED:
      if (ev == EV_LEFT)  cfg.led = constrain((int)cfg.led - 1, 0, 3);
      if (ev == EV_RIGHT) cfg.led = constrain((int)cfg.led + 1, 0, 3);
      if (ev == EV_BACK)  { saveLedCfg(); gotoScr(SC_SETTINGS); }
      break;

    case SC_SYSTEM:
      if (ev == EV_UP)   selSys = (selSys + 3) % 4;
      if (ev == EV_DOWN) selSys = (selSys + 1) % 4;
      if (ev == EV_BACK) gotoScr(SC_SETTINGS);
      if (ev == EV_OK)   { if (selSys == 0) gotoScr(SC_ABOUT); else { confKind = selSys; gotoScr(SC_CONFIRM); } }
      break;
    case SC_ABOUT: if (ev == EV_BACK) gotoScr(SC_SYSTEM); break;
    case SC_CONFIRM:
      if (ev == EV_UP || ev == EV_DOWN) { selConf ^= 1; okHoldT = 0; }
      if (ev == EV_BACK) gotoScr(SC_SYSTEM);
      if (ev == EV_OK) {
        if (selConf == 0) gotoScr(SC_SYSTEM);
        else if (confKind != 3) doConfirm();
      }
      break;

    case SC_WARN:
      if (ev == EV_OK) { wi++; if (wi >= wn) { wn = 0; wi = 0; enterMode(); } else needRedraw = true; }
      break;

    case SC_DINO_START:
      if (ev == EV_OK) startGame();
      else if (ev == EV_BACK) openModeSelect();
      else modeCommon(ev);
      break;
    case SC_DINO_PLAY:
      if (ev == EV_BACK) gotoScr(SC_DINO_PAUSE);
      break;
    case SC_DINO_PAUSE:
      if (ev == EV_OK) { lastFrame = now; gotoScr(SC_DINO_PLAY); }
      else if (ev == EV_BACK) openModeSelect();
      else modeCommon(ev);
      break;
    case SC_DINO_OVER:
      if (ev == EV_OK) startGame();
      else if (ev == EV_BACK) openModeSelect();
      else modeCommon(ev);
      break;
  }
}

// ============================== LED =======================================
void updateLed(uint32_t now) {
  bool on = false;
  switch (cfg.led) {
    case 0: on = false; break;
    case 1: on = true;  break;
    case 3: on = now < ledActUntil; break;
    default:
      if (cfg.mode == MODE_BT)
        on = (btState == BT_CONN) ? true : (btState == BT_WAIT ? ((now / 500) % 2 == 0) : false);
      else if (cfg.mode == MODE_USB)
        on = usbConn ? true : ((now / 500) % 2 == 0);
      else if (scr == SC_DINO_OVER) { uint32_t t = now - overT; on = t < 300 && ((t / 100) % 2 == 0); }
  }
  setLed(on);
}

// ============================== BOOT ======================================
void bootWait(uint32_t ms) { uint32_t t = millis(); while (millis() - t < ms) yield(); }

void showLogo() {
  display.clearDisplay();
  display.drawBitmap(0, 0, phy_logo, 128, 64, SSD1306_WHITE);
  display.display();
}

void bootProgress(int pct, const char* st[4]) {
  display.clearDisplay(); display.setTextColor(WHITE); display.setTextSize(1);
  P(0, 0, "ESP32 CONTROLLER %s", FW_VERSION);
  P(2, 0, "Loading...");
  display.drawRect(0, 28, 90, 8, WHITE); display.fillRect(2, 30, (86 * pct) / 100, 4, WHITE);
  display.setCursor(96, 28); display.printf("%3d%%", pct);
  static const char* const N[4] = { "Display", "Settings", "Inputs", "Radio" };
  for (int i = 0; i < 4; i++) {
    char line[24]; int nl = strlen(N[i]), sl = strlen(st[i]);
    int dots = 21 - nl - sl - 2; if (dots < 1) dots = 1;
    int n = snprintf(line, sizeof line, "%s ", N[i]);
    for (int d = 0; d < dots; d++) line[n++] = '.';
    n += snprintf(line + n, sizeof line - n, " %s", st[i]);
    P(4 + i, 0, "%s", line);
  }
  display.display();
}

void bootSequence() {
  // Stage 1: power-up black (display already begun, contrast 0, settings loaded)
  bootWait(150);
  // Stage 2: logo, contrast fades up over 0.4 s, held 1 s total
  showLogo();
  uint32_t t0 = millis(); uint8_t target = CONTRAST[cfg.bri - 1];
  while (millis() - t0 < 1000) {
    uint32_t e = millis() - t0;
    setContrast(e < 400 ? (uint8_t)(target * e / 400) : target);
    delay(10);
  }
  applyBrightness();
  // Stage 3: splash 1.15 s
  display.clearDisplay(); display.setTextColor(WHITE);
  PCtr(8, "ESP32", 3); PCtr(34, "DUAL-JOYSTICK", 1); PCtr(44, "GAME CONTROLLER", 1);
  display.setTextSize(1); display.setCursor(128 - 4 * 6, 56); display.print("v1.0");
  display.display();
  bootWait(1150);
  // Stage 4: loading, four real checks
  if (cfg.led != 0) setLed(true);
  const char* st[4] = { "..", "..", "..", ".." };
  bootProgress(0, st);
  st[0] = "OK"; bootProgress(25, st); bootWait(350);                         // Display
  st[1] = "OK"; bootProgress(50, st); bootWait(350);                         // Settings (reset warning queued below)
  if (settingsWereReset) addWarn("Settings were reset", "to defaults.", "", "Calibration needed.");
  // Inputs: 32 samples per axis, 7 digital reads
  for (int i = 0; i < 4; i++) {
    long s = 0; for (int k = 0; k < 32; k++) { s += analogRead(ADC_PIN[i]); delay(1); }
    int avg = s / 32;
    if (avg <= 5 || avg >= 4090) {
      char l2[22]; snprintf(l2, 22, "%s reads %d", AX_NAME[i], avg);
      addWarn("Joystick axis stuck:", l2, "Check VRx/VRy wire", "and 3V3 / GND.");
    }
  }
  for (int i = 0; i < 7; i++) (void)digitalRead(BTN_PIN[i]);
  st[2] = "OK"; bootProgress(75, st); bootWait(350);
  // Radio: only when saved mode is Bluetooth
  if (cfg.mode == MODE_BT) { startBle(); st[3] = "OK"; } else st[3] = "SKIP";
  bootProgress(100, st); bootWait(350);
  // Stage 5: READY (LED blinks once)
  display.clearDisplay(); display.setTextColor(WHITE);
  P(0, 0, "ESP32 CONTROLLER %s", FW_VERSION);
  PCtr(24, "READY", 2);
  display.drawRect(0, 46, 90, 8, WHITE); display.fillRect(2, 48, 86, 4, WHITE);
  display.setCursor(96, 46); display.print("100%");
  display.display();
  if (cfg.led != 0) { setLed(false); bootWait(200); setLed(true); bootWait(200); }
  else bootWait(400);
}

// ============================== SETUP / LOOP ==============================
void setup() {
  pinMode(PIN_LED, OUTPUT); digitalWrite(PIN_LED, LOW);   // LED driven only after reset finished
  Serial.begin(115200);
  for (int i = 0; i < 7; i++) pinMode(BTN_PIN[i], INPUT_PULLUP);
  for (int i = 0; i < 4; i++) pinMode(ADC_PIN[i], INPUT);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  SPI.begin(OLED_SCK, -1, OLED_MOSI, OLED_CS);
  if (!display.begin(SSD1306_SWITCHCAPVCC)) {
    Serial.println("ERROR: SSD1306 OLED init failed - check SPI wiring (D0=18 D1=23 RES=17 DC=16 CS=5)");
    while (true) { digitalWrite(PIN_LED, !digitalRead(PIN_LED)); delay(100); }   // 5 Hz
  }
  display.clearDisplay(); display.display();
  setContrast(0);
  loadSettings();
  randomSeed(esp_random());

  bootSequence();

  for (int i = 0; i < 7; i++) { btn[i] = btnPrevRaw[i] = (digitalRead(BTN_PIN[i]) == LOW); }
  lastAct = lastFrame = millis();

  // where boot goes next: calibration wizard -> warnings -> home
  if (!calValid) { calFirst = true; defaultsCal(); gotoScr(SC_CAL1); }
  else afterBoot();
}

void loop() {
  uint32_t now = millis();

  bool act = readInputs(now);
  genEvents(now);
  updateLink(now);

  // sleeping screen: the first input only wakes it
  if (act) {
    lastAct = now; ledActUntil = now + 50;
    if (!displayOn) { wakeDisplay(); evn = 0; if (pressEdge[B_MODE]) modeSwallow = true; }
  }

  for (uint8_t i = 0; i < evn; i++) { handleEvent(evq[i], now); needRedraw = true; }

  // per-screen continuous work
  if (scr == SC_DINO_PLAY) gameInput();
  if (scr == SC_CAL2) calTick(now);
  if (scr == SC_CONFIRM && confKind == 3 && selConf == 1) {
    if (btn[B_OK]) { if (!okHoldT) okHoldT = now; else if (now - okHoldT >= 2000) doConfirm(); }
    else okHoldT = 0;
  }

  sendHost(now);
  updateLed(now);

  // screen timeout (never during Dino play, Calibration, Input monitor)
  bool noSleep = (scr == SC_DINO_PLAY || scr == SC_CAL1 || scr == SC_CAL2 || scr == SC_CAL3 || scr == SC_MONITOR);
  if (displayOn && !noSleep && TIMEOUT_MS[cfg.tmo] && now - lastAct > TIMEOUT_MS[cfg.tmo]) sleepDisplay();

  // frame tick: game physics + drawing at ~17 FPS
  if (now - lastFrame >= FRAME_MS) {
    float dt = (now - lastFrame) / 1000.0f; lastFrame = now;
    if (scr == SC_DINO_PLAY) gameStep(dt);
    if (displayOn && (needRedraw || dynamicScreen(now))) { render(now); needRedraw = false; }
  }
  delay(1);
}
