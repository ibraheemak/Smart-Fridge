#pragma once

// ============================================================================
// Live View screen (VIEW_LIVE) — PLACEHOLDER
//
// Reached from the home "LIVE VIEW" tile. This is intentionally a stub: a
// teammate is implementing the real camera stream here. For now it just shows
// a "coming soon" message with a "< Back" button.
//
// To build it out, replace renderLiveViewScreen() with the actual live-feed
// rendering and keep handleLiveViewTouch() returning to the home screen on
// "< Back". No other file needs to change — the home tile, the VIEW_LIVE view
// state (touch.h) and the touch dispatch are already wired up.
//
// Include order: this header must come AFTER touch.h (BtnRect/drawBtn/inBtn/
// btnBack/btnBackHit/layoutDetailButtons/g_view/VIEW_LIVE) and display.h
// (tft / HEADER_HEIGHT_PX / renderHomeScreen).
// ============================================================================

#include "display.h"

void renderLiveViewScreen() {
  tft.fillScreen(TFT_BLACK);
  int w = tft.width(), h = tft.height();

  tft.fillRect(0, 0, w, HEADER_HEIGHT_PX, TFT_NAVY);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextSize(2);
  tft.drawString("Live View", w / 2, HEADER_HEIGHT_PX / 2);

  layoutDetailButtons();
  drawBtn(btnBack, "< Back", TFT_DARKGREY);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("Camera stream", w / 2, h / 2 - 12);
  tft.drawString("coming soon", w / 2, h / 2 + 16);
}

void openLiveViewScreen() {
  g_view = VIEW_LIVE;
  renderLiveViewScreen();
}

void handleLiveViewTouch(int x, int y) {
  if (inBtn(btnBackHit, x, y)) {
    g_view = VIEW_HOME;
    renderHomeScreen();
  }
}
