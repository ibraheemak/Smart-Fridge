#pragma once

// ============================================================================
// Settings screen + environment (temperature/humidity) alert.
//
// A "Settings" button lives top-left on the home header (see display.h's
// drawHomeSettingsButton()). Tapping it opens VIEW_SETTINGS, where the user
// can:
//   - turn the door-open buzzer sound on/off        -> g_buzzer_enabled
//   - change how long the door may stay open before  -> g_door_open_alert_ms
//     the buzzer starts (seconds)
//   - set the acceptable temperature range (min/max, C)
//   - set the acceptable humidity range (min/max, %)
//
// If a temperature/humidity reading (pushed from the roof1 CAM over ESP-NOW,
// see espnow_link.h) falls outside the configured range, a full-screen ALERT
// (VIEW_ALERT) is raised. It stays up until the user taps CLOSE — a later
// in-range reading does NOT auto-dismiss it. The alert only changes what's
// drawn: loop() keeps servicing the door sensor, scan trigger, buzzer, and
// barcode scanner regardless of view, so raising it never blocks anything.
//
// Settings persist in NVS (Preferences namespace "fsettings") across reboots.
//
// Include order: this header must come AFTER touch.h (ViewState/g_view/BtnRect/
// drawBtn/inBtn/btnBack), display.h (tft/g_temp_c/g_humidity/renderHomeScreen),
// door.h (g_door_open_alert_ms) and buzzer.h (g_buzzer_enabled).
// ============================================================================

#include <Preferences.h>
#include "display.h"
#include "parameters.h"

// ----------------------------------------------------------------------------
// Persisted settings
// ----------------------------------------------------------------------------
struct FridgeSettings {
  bool buzzer_enabled;  // false = door-open buzzer stays silent
  int  door_alert_s;    // seconds door may stay open before the buzzer fires
  int  temp_min;        // acceptable temperature range (C)
  int  temp_max;
  int  hum_min;         // acceptable humidity range (%)
  int  hum_max;
};

FridgeSettings g_settings;

// Push the runtime-tunable values into the globals the rest of the sketch
// already reads (door.h / buzzer.h), so nothing else needs to know settings
// exist.
void applySettings() {
  g_buzzer_enabled     = g_settings.buzzer_enabled;
  g_door_open_alert_ms = (unsigned long)g_settings.door_alert_s * 1000UL;
}

void loadSettings() {
  Preferences p;
  p.begin("fsettings", true);   // read-only
  g_settings.buzzer_enabled = p.getBool("buzz", true);
  g_settings.door_alert_s   = p.getInt("dooralert", DOOR_OPEN_ALERT_MS / 1000);
  g_settings.temp_min       = p.getInt("tmin", 2);
  g_settings.temp_max       = p.getInt("tmax", 8);
  g_settings.hum_min        = p.getInt("hmin", 30);
  g_settings.hum_max        = p.getInt("hmax", 70);
  p.end();
}

void saveSettings() {
  Preferences p;
  p.begin("fsettings", false);  // read/write
  p.putBool("buzz",      g_settings.buzzer_enabled);
  p.putInt("dooralert",  g_settings.door_alert_s);
  p.putInt("tmin",       g_settings.temp_min);
  p.putInt("tmax",       g_settings.temp_max);
  p.putInt("hmin",       g_settings.hum_min);
  p.putInt("hmax",       g_settings.hum_max);
  p.end();
}

void initSettings() {
  loadSettings();
  applySettings();
}

// ----------------------------------------------------------------------------
// Environment alert state
// ----------------------------------------------------------------------------
bool   g_alert_active    = false;  // an unacknowledged out-of-range condition exists
bool   g_alert_dismissed = false;  // user tapped CLOSE — suppress until reading normalizes
String g_alert_msg       = "";

BtnRect btnAlertClose;

// Build the human-readable "what's wrong" string; returns true if any reading
// is currently outside its configured range. Readings of -1 mean "no data yet"
// and are ignored so the alert never fires before the first ESP-NOW push.
bool readingOutOfRange(String& msg) {
  bool oor = false;
  msg = "";
  if (g_temp_c >= 0) {
    if (g_temp_c < g_settings.temp_min) { oor = true; msg = "Temp LOW " + String(g_temp_c, 1) + "C"; }
    else if (g_temp_c > g_settings.temp_max) { oor = true; msg = "Temp HIGH " + String(g_temp_c, 1) + "C"; }
  }
  if (g_humidity >= 0) {
    if (g_humidity < g_settings.hum_min) { if (oor) msg += "  "; oor = true; msg += "Hum LOW " + String(g_humidity, 0) + "%"; }
    else if (g_humidity > g_settings.hum_max) { if (oor) msg += "  "; oor = true; msg += "Hum HIGH " + String(g_humidity, 0) + "%"; }
  }
  return oor;
}

void renderAlertScreen() {
  int w = tft.width(), h = tft.height();
  tft.fillScreen(TFT_RED);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft.setTextSize(3);
  tft.drawString("! ALERT !", w / 2, 44);

  tft.setTextSize(2);
  tft.drawString("Out of safe range", w / 2, 92);
  tft.setTextColor(TFT_YELLOW, TFT_RED);
  tft.drawString(g_alert_msg, w / 2, 132);

  String rng = "OK: " + String(g_settings.temp_min) + "-" + String(g_settings.temp_max) + "C   " +
               String(g_settings.hum_min) + "-" + String(g_settings.hum_max) + "%";
  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft.setTextSize(1);
  tft.drawString(rng, w / 2, 168);

  btnAlertClose = {(w - 160) / 2, h - 72, 160, 52};
  drawBtn(btnAlertClose, "CLOSE", TFT_DARKGREY);
}

// Called after every new temperature/humidity reading. Raises the alert when a
// reading goes out of range (unless already acknowledged), and clears the
// acknowledgement once readings return to normal. Never touches the screen
// while the alert is already up — it stays until the user taps CLOSE.
void checkEnvironmentAlert() {
  if (g_view == VIEW_ALERT) return;

  String msg;
  bool oor = readingOutOfRange(msg);
  if (!oor) { g_alert_active = false; g_alert_dismissed = false; return; }
  if (g_alert_dismissed) return;   // already CLOSEd this episode

  g_alert_active = true;
  g_alert_msg    = msg;

  // Only take over an "idle" screen — never interrupt an in-progress task
  // (expiry entry, barcode scan, or editing Settings). serviceEnvironmentAlert()
  // shows it later once the user returns to an idle screen; tapping Back on
  // Settings re-checks explicitly.
  if (g_view == VIEW_HOME || g_view == VIEW_LIST || g_view == VIEW_STATS) {
    g_view = VIEW_ALERT;
    renderAlertScreen();
  }
}

// Called every loop() — shows a pending alert the moment the user is back on an
// idle screen (it was deferred while they were mid-task).
void serviceEnvironmentAlert() {
  if (g_alert_active && g_view != VIEW_ALERT &&
      (g_view == VIEW_HOME || g_view == VIEW_LIST || g_view == VIEW_STATS)) {
    g_view = VIEW_ALERT;
    renderAlertScreen();
  }
}

void handleAlertTouch(int x, int y) {
  if (inBtn(btnAlertClose, x, y)) {
    g_alert_active    = false;
    g_alert_dismissed = true;     // don't re-nag until a reading is back in range
    g_view = VIEW_HOME;
    renderHomeScreen();
  }
}

// ----------------------------------------------------------------------------
// Settings screen layout
// ----------------------------------------------------------------------------
#define SET_ROW_H  44
#define SET_BTN_W  42
#define SET_BTN_H  38
#define SET_MINUS_X 250
#define SET_PLUS_X  358
#define SET_VAL_X0  292
#define SET_VAL_W   66

static inline int setRowY(int i) { return HEADER_HEIGHT_PX + 6 + i * SET_ROW_H; }

BtnRect btnSetBuzzer;
BtnRect btnDoorM, btnDoorP;
BtnRect btnTMinM, btnTMinP, btnTMaxM, btnTMaxP;
BtnRect btnHMinM, btnHMinP, btnHMaxM, btnHMaxP;

void layoutSettings() {
  int y0 = setRowY(0) + 2, y1 = setRowY(1) + 2, y2 = setRowY(2) + 2;
  int y3 = setRowY(3) + 2, y4 = setRowY(4) + 2, y5 = setRowY(5) + 2;

  btnSetBuzzer = {SET_MINUS_X, y0, 150, SET_BTN_H};

  btnDoorM = {SET_MINUS_X, y1, SET_BTN_W, SET_BTN_H};
  btnDoorP = {SET_PLUS_X,  y1, SET_BTN_W, SET_BTN_H};
  btnTMinM = {SET_MINUS_X, y2, SET_BTN_W, SET_BTN_H};
  btnTMinP = {SET_PLUS_X,  y2, SET_BTN_W, SET_BTN_H};
  btnTMaxM = {SET_MINUS_X, y3, SET_BTN_W, SET_BTN_H};
  btnTMaxP = {SET_PLUS_X,  y3, SET_BTN_W, SET_BTN_H};
  btnHMinM = {SET_MINUS_X, y4, SET_BTN_W, SET_BTN_H};
  btnHMinP = {SET_PLUS_X,  y4, SET_BTN_W, SET_BTN_H};
  btnHMaxM = {SET_MINUS_X, y5, SET_BTN_W, SET_BTN_H};
  btnHMaxP = {SET_PLUS_X,  y5, SET_BTN_W, SET_BTN_H};
}

void drawSetValue(int row, const String& s) {
  int y = setRowY(row) + 2 + SET_BTN_H / 2;
  tft.fillRect(SET_VAL_X0, setRowY(row) + 2, SET_VAL_W, SET_BTN_H, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(s, SET_VAL_X0 + SET_VAL_W / 2, y);
}

// Redraws only the mutable value fields (and the buzzer toggle) — used after a
// +/- tap so the labels and fixed buttons don't flash.
void drawSettingsValues() {
  drawBtn(btnSetBuzzer, g_settings.buzzer_enabled ? "ON" : "OFF",
          g_settings.buzzer_enabled ? TFT_DARKGREEN : 0x6000 /* dark red */);
  drawSetValue(1, String(g_settings.door_alert_s) + "s");
  drawSetValue(2, String(g_settings.temp_min) + "C");
  drawSetValue(3, String(g_settings.temp_max) + "C");
  drawSetValue(4, String(g_settings.hum_min) + "%");
  drawSetValue(5, String(g_settings.hum_max) + "%");
}

void renderSettingsScreen() {
  tft.fillScreen(TFT_BLACK);
  int w = tft.width();

  tft.fillRect(0, 0, w, HEADER_HEIGHT_PX, TFT_NAVY);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextSize(2);
  tft.drawString("Settings", w / 2, HEADER_HEIGHT_PX / 2);

  // Reuse the shared header back button (same as detail/stats screens).
  layoutDetailButtons();
  drawBtn(btnBack, "< Back", TFT_DARKGREY);

  layoutSettings();

  const char* labels[6] = {
    "Buzzer sound", "Door alert", "Temp min", "Temp max", "Humidity min", "Humidity max"
  };
  tft.setTextDatum(ML_DATUM);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  for (int i = 0; i < 6; i++)
    tft.drawString(labels[i], SIDE_PADDING_PX, setRowY(i) + 2 + SET_BTN_H / 2);

  drawBtn(btnDoorM, "-", TFT_DARKGREY); drawBtn(btnDoorP, "+", TFT_DARKGREY);
  drawBtn(btnTMinM, "-", TFT_DARKGREY); drawBtn(btnTMinP, "+", TFT_DARKGREY);
  drawBtn(btnTMaxM, "-", TFT_DARKGREY); drawBtn(btnTMaxP, "+", TFT_DARKGREY);
  drawBtn(btnHMinM, "-", TFT_DARKGREY); drawBtn(btnHMinP, "+", TFT_DARKGREY);
  drawBtn(btnHMaxM, "-", TFT_DARKGREY); drawBtn(btnHMaxP, "+", TFT_DARKGREY);

  drawSettingsValues();
}

void openSettingsScreen() {
  g_view = VIEW_SETTINGS;
  renderSettingsScreen();
}

void handleSettingsTouch(int x, int y) {
  // Back: persist and return home, then re-evaluate the alert against the new
  // thresholds (in case a reading is now out of range).
  if (inBtn(btnBackHit, x, y)) {
    saveSettings();
    g_view = VIEW_HOME;
    renderHomeScreen();
    checkEnvironmentAlert();
    return;
  }

  bool changed = false;
  if      (inBtn(btnSetBuzzer, x, y)) { g_settings.buzzer_enabled = !g_settings.buzzer_enabled; changed = true; }
  else if (inBtn(btnDoorM, x, y)) { g_settings.door_alert_s = max(5,   g_settings.door_alert_s - 5); changed = true; }
  else if (inBtn(btnDoorP, x, y)) { g_settings.door_alert_s = min(300, g_settings.door_alert_s + 5); changed = true; }
  else if (inBtn(btnTMinM, x, y)) { g_settings.temp_min = max(-20, g_settings.temp_min - 1); changed = true; }
  else if (inBtn(btnTMinP, x, y)) { g_settings.temp_min = min(g_settings.temp_max - 1, g_settings.temp_min + 1); changed = true; }
  else if (inBtn(btnTMaxM, x, y)) { g_settings.temp_max = max(g_settings.temp_min + 1, g_settings.temp_max - 1); changed = true; }
  else if (inBtn(btnTMaxP, x, y)) { g_settings.temp_max = min(30, g_settings.temp_max + 1); changed = true; }
  else if (inBtn(btnHMinM, x, y)) { g_settings.hum_min = max(0, g_settings.hum_min - 5); changed = true; }
  else if (inBtn(btnHMinP, x, y)) { g_settings.hum_min = min(g_settings.hum_max - 5, g_settings.hum_min + 5); changed = true; }
  else if (inBtn(btnHMaxM, x, y)) { g_settings.hum_max = max(g_settings.hum_min + 5, g_settings.hum_max - 5); changed = true; }
  else if (inBtn(btnHMaxP, x, y)) { g_settings.hum_max = min(100, g_settings.hum_max + 5); changed = true; }

  if (changed) {
    applySettings();           // live-apply buzzer/door values; NVS write deferred to Back
    drawSettingsValues();
  }
}
