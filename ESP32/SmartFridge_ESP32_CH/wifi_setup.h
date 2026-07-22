#pragma once

#include <Preferences.h>
#include <WiFiManager.h>
#include "display.h"
#include "espnow_link.h"
#include "parameters.h"

// ============================================================================
// Settings > WiFi — move any board onto a different router, from the screen.
//
// VIEW_WIFI lists every board in the system (this display + one row per roof
// camera). Picking one opens VIEW_WIFI_INFO, which shows the exact setup AP
// name and the http://192.168.4.1 link the user opens in a phone browser, and
// only actually starts the portal when they tap START — a board hosting its
// portal is off the router (and, for a camera, deaf to SCAN_TRIGGER) for up to
// WIFI_PORTAL_TIMEOUT_S, so it shouldn't happen on a stray tap.
//
// START does one of two things:
//   * Display  -> persists a one-shot NVS flag and reboots. initWiFi() reads
//                 the flag at boot and calls startConfigPortal() instead of
//                 autoConnect(), which would otherwise silently reconnect to
//                 the credentials already on file and never show a portal.
//   * Camera N -> unicasts "WIFI_PORTAL" over ESP-NOW to that roof; the CAM
//                 board does the same flag-and-reboot dance on its side (see
//                 wifi_portal.h there). ESP-NOW works board-to-board with no
//                 router involved, so this still works when the reason the
//                 user is here is "the camera can't reach the WiFi".
//
// The flag is one-shot — cleared as it's read — so a portal that times out
// with nothing entered just boots normally next time instead of trapping the
// board in setup mode.
//
// Include order: after touch.h (ViewState/BtnRect/drawBtn/inBtn/btnBack),
// display.h (tft/renderHomeScreen) and espnow_link.h (espnowSendWifiPortal).
// ============================================================================

// Which board VIEW_WIFI_INFO is about: 0 = this display, 1..NUM_ROOFS = camera.
int g_wifi_target = 0;

// ----------------------------------------------------------------------------
// One-shot "open my config portal on the next boot" flag (this board)
// ----------------------------------------------------------------------------
bool takeWifiPortalRequest() {
  Preferences p;
  p.begin("wifiportal", false);
  bool pending = p.getBool("pending", false);
  if (pending) p.putBool("pending", false);
  p.end();
  return pending;
}

void rebootIntoWifiPortal() {
  Preferences p;
  p.begin("wifiportal", false);
  p.putBool("pending", true);
  p.end();
  showStatus("WiFi setup", "Restarting...");
  Serial.println("[WIFI] Portal requested — restarting into setup mode");
  delay(600);
  ESP.restart();
}

// AP name a given board hosts while its portal is open. Must match what the
// board itself uses: WIFI_AP_NAME here, and the CAM board's WIFI_AP_NAME +
// roof number (wifiPortalApName() in the CAM's wifi_portal.h).
String wifiApNameFor(int target) {
  if (target == 0) return String(WIFI_AP_NAME);
  return String(CAM_WIFI_AP_PREFIX) + String(target);
}

String wifiTargetName(int target) {
  if (target == 0) return "Display";
  return "Camera " + String(target);
}

// ----------------------------------------------------------------------------
// VIEW_WIFI — one row per board
// ----------------------------------------------------------------------------
#define WIFI_ROW_COUNT (1 + NUM_ROOFS)

BtnRect btnWifiRow[WIFI_ROW_COUNT];

void layoutWifiScreen() {
  for (int i = 0; i < WIFI_ROW_COUNT; i++)
    btnWifiRow[i] = {SET_MINUS_X, setRowY(i) + 2, 150, SET_BTN_H};
}

void renderWifiScreen() {
  tft.fillScreen(TFT_BLACK);
  int w = tft.width();

  tft.fillRect(0, 0, w, HEADER_HEIGHT_PX, TFT_NAVY);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextSize(2);
  tft.drawString("WiFi", w / 2, HEADER_HEIGHT_PX / 2);

  layoutDetailButtons();
  drawBtn(btnBack, "< Back", TFT_DARKGREY);

  layoutWifiScreen();

  // Labels first, buttons second — drawBtn() leaves the text datum on MC_DATUM,
  // so interleaving the two would centre every label after the first on x=12
  // and clip its left half off the screen.
  tft.setTextDatum(ML_DATUM);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  for (int i = 0; i < WIFI_ROW_COUNT; i++)
    tft.drawString(wifiTargetName(i), SIDE_PADDING_PX, setRowY(i) + 2 + SET_BTN_H / 2);

  for (int i = 0; i < WIFI_ROW_COUNT; i++)
    drawBtn(btnWifiRow[i], "Change >", TFT_NAVY);

  // The display knows its own network; the cameras' is only knowable from
  // their own side, so just show ours.
  tft.setTextDatum(ML_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  String now = WiFi.status() == WL_CONNECTED
             ? "Display network: " + WiFi.SSID() + "  (" + WiFi.localIP().toString() + ")"
             : String("Display network: not connected");
  tft.drawString(now, SIDE_PADDING_PX, tft.height() - 20);
}

void openWifiScreen() {
  g_view = VIEW_WIFI;
  renderWifiScreen();
}

// ----------------------------------------------------------------------------
// VIEW_WIFI_INFO — instructions + the link, then START
// ----------------------------------------------------------------------------
BtnRect btnWifiStart;

void renderWifiInfoScreen() {
  tft.fillScreen(TFT_BLACK);
  int w = tft.width();

  tft.fillRect(0, 0, w, HEADER_HEIGHT_PX, TFT_NAVY);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextSize(2);
  tft.drawString(wifiTargetName(g_wifi_target) + " WiFi", w / 2, HEADER_HEIGHT_PX / 2);

  layoutDetailButtons();
  drawBtn(btnBack, "< Back", TFT_DARKGREY);

  int y = HEADER_HEIGHT_PX + 22;
  tft.setTextDatum(ML_DATUM);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("1. Tap START below", SIDE_PADDING_PX, y);       y += 30;
  tft.drawString("2. On your phone, join WiFi:", SIDE_PADDING_PX, y); y += 26;

  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString(wifiApNameFor(g_wifi_target), SIDE_PADDING_PX + 20, y); y += 30;

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("3. Open in a browser:", SIDE_PADDING_PX, y);    y += 26;
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("http://192.168.4.1", SIDE_PADDING_PX + 20, y);  y += 30;

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("4. Pick your network + password", SIDE_PADDING_PX, y);

  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  String note = g_wifi_target == 0
              ? String("The display restarts and is offline until setup finishes")
              : wifiTargetName(g_wifi_target) + " restarts and won't scan until setup finishes";
  tft.drawString(note + " (" + String(WIFI_PORTAL_TIMEOUT_S / 60) + " min max)",
                 SIDE_PADDING_PX, tft.height() - 66);

  btnWifiStart = {tft.width() - 170, tft.height() - 52, 150, 40};
  drawBtn(btnWifiStart, "START", TFT_DARKGREEN);
}

void openWifiInfoScreen(int target) {
  g_wifi_target = target;
  g_view = VIEW_WIFI_INFO;
  renderWifiInfoScreen();
}

void handleWifiTouch(int x, int y) {
  if (inBtn(btnBackHit, x, y)) { openSettingsScreen(); return; }

  for (int i = 0; i < WIFI_ROW_COUNT; i++) {
    if (inBtn(btnWifiRow[i], x, y)) { openWifiInfoScreen(i); return; }
  }
}

void handleWifiInfoTouch(int x, int y) {
  if (inBtn(btnBackHit, x, y)) { openWifiScreen(); return; }

  if (inBtn(btnWifiStart, x, y)) {
    if (g_wifi_target == 0) {
      rebootIntoWifiPortal();     // never returns
      return;
    }
    espnowSendWifiPortal(g_wifi_target);
    showStatus(wifiTargetName(g_wifi_target) + " restarting",
               "Join " + wifiApNameFor(g_wifi_target));
    delay(2000);
    openWifiScreen();
  }
}
