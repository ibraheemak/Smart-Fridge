/*
 * SmartFridge — ILI9488 TFT + XPT2046 Touch unit test (isolated)
 *
 * Validates the display + touch panel on the ESP32 devkit (CH9102), in
 * isolation from WiFiManager/Firestore/DHT11 logic.
 *
 * NO WiFi, NO Firestore, NO sensors.
 *
 * Requires: TFT_eSPI library configured for this hardware (see tft_setup.h
 * in this folder — same config as SmartFridge_ESP32_CH/tft_setup.h).
 *
 * What it does on boot:
 *   - Fills the screen with color bars to check wiring/orientation
 *   - Draws text in a few sizes/fonts
 *   - Draws a border rectangle at the screen edges
 *
 * Then in loop():
 *   - Polls the touch panel; on a touch, draws a small crosshair at the
 *     touched point and prints raw + mapped coordinates over serial
 *     (115200 baud) — useful for verifying touch calibration/orientation.
 */

#include <TFT_eSPI.h>
#include "tft_setup.h"

#define DISPLAY_ROTATION 1   // 1/3 = landscape (480x320), matches SmartFridge_ESP32_CH

TFT_eSPI tft = TFT_eSPI();

void drawColorBars() {
  int w = tft.width();
  int h = tft.height();
  uint32_t colors[] = { TFT_RED, TFT_GREEN, TFT_BLUE, TFT_YELLOW,
                         TFT_CYAN, TFT_MAGENTA, TFT_WHITE, TFT_BLACK };
  int n = sizeof(colors) / sizeof(colors[0]);
  int bar_w = w / n;
  for (int i = 0; i < n; i++) {
    tft.fillRect(i * bar_w, 0, bar_w, h, colors[i]);
  }
}

void drawTextSamples() {
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  tft.setTextSize(1);
  tft.drawString("SmartFridge Display Test", 10, 10);

  tft.setTextSize(2);
  tft.drawString("Size 2", 10, 30);

  tft.setTextSize(3);
  tft.drawString("Size 3", 10, 60);

  tft.setTextSize(1);
  char buf[64];
  snprintf(buf, sizeof(buf), "Resolution: %dx%d", tft.width(), tft.height());
  tft.drawString(buf, 10, 100);

  tft.drawString("Touch the screen...", 10, 120);
}

void drawBorder() {
  tft.drawRect(0, 0, tft.width(), tft.height(), TFT_RED);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[BOOT] ILI9488 + XPT2046 display test");

  tft.init();
  tft.setRotation(DISPLAY_ROTATION);

  Serial.printf("[TFT] Resolution: %dx%d\n", tft.width(), tft.height());

  tft.fillScreen(TFT_BLACK);
  drawColorBars();
  delay(2000);

  tft.fillScreen(TFT_BLACK);
  drawTextSamples();
  drawBorder();

  Serial.println("[READY] Printing raw XPT2046 readings every 200ms.");
  Serial.println("[READY] Press the screen and watch for 'z' to spike —");
  Serial.println("[READY] if z never changes, the touch chip isn't responding (check wiring).");
}

// 'z' (getTouchRawZ) is too noisy to threshold reliably on this board —
// idle and pressed values overlap. Instead, use rawX: with no touch the
// XPT2046 floats to x=0 (rail); a real touch gives a nonzero, stable x.
#define TOUCH_X_RAIL_MAX        20
#define TOUCH_DEBOUNCE_COUNT     2

// Raw XPT2046 range -> screen mapping. TUNE THESE: touch each corner of the
// screen, read the printed raw x/y, and adjust min/max + swap/invert below
// until the crosshair lands where you actually pressed.
#define RAW_X_MIN 100
#define RAW_X_MAX 3950
#define RAW_Y_MIN 100
#define RAW_Y_MAX 3950
#define SWAP_XY   0   // set to 1 if moving along the screen's X axis changes raw 'y'
#define INVERT_X  0   // set to 1 if the crosshair X is mirrored
#define INVERT_Y  0   // set to 1 if the crosshair Y is mirrored

int mapTouchToScreen(uint16_t raw, uint16_t rawMin, uint16_t rawMax, int screenSize, bool invert) {
  long v = (long)(raw - rawMin) * screenSize / (rawMax - rawMin);
  v = constrain(v, 0, screenSize - 1);
  return invert ? (screenSize - 1 - v) : v;
}

static int pressCount = 0;

void loop() {
  // Raw, unscaled touch chip read — bypasses calibration entirely.
  uint16_t rawX, rawY;
  uint8_t z = tft.getTouchRawZ();
  tft.getTouchRaw(&rawX, &rawY);

  if (rawX > TOUCH_X_RAIL_MAX) pressCount++;
  else pressCount = 0;
  bool touched = (pressCount >= TOUCH_DEBOUNCE_COUNT);

  Serial.printf("[RAW] x=%u y=%u z=%u pressed=%d\n", rawX, rawY, z, touched);

  if (touched) {
    uint16_t srcX = SWAP_XY ? rawY : rawX;
    uint16_t srcY = SWAP_XY ? rawX : rawY;
    int x = mapTouchToScreen(srcX, RAW_X_MIN, RAW_X_MAX, tft.width(),  INVERT_X);
    int y = mapTouchToScreen(srcY, RAW_Y_MIN, RAW_Y_MAX, tft.height(), INVERT_Y);

    Serial.printf("[TOUCH] screen x=%d y=%d\n", x, y);

    // crosshair at the mapped point
    tft.drawLine(x - 6, y, x + 6, y, TFT_YELLOW);
    tft.drawLine(x, y - 6, x, y + 6, TFT_YELLOW);

    // echo coordinates in the bottom-left corner
    tft.fillRect(0, tft.height() - 20, 200, 20, TFT_BLACK);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setTextSize(1);
    char buf[48];
    snprintf(buf, sizeof(buf), "x=%d y=%d (raw %u,%u)", x, y, rawX, rawY);
    tft.drawString(buf, 5, tft.height() - 18);
  }

  delay(150);
}
