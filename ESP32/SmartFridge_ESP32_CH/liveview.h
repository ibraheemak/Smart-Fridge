#pragma once

// ============================================================================
// Live View screen (VIEW_LIVE) — US #11
//
// Reached from the home "LIVE VIEW" tile. Requests a single JPEG snapshot
// from one roof's ESP32-CAM board over ESP-NOW (see espnowSendLiveViewRequest()
// / liveViewFrameReady() / liveViewTransferTimedOut() in espnow_link.h — that
// file owns the chunked-transfer wire protocol; this file only owns the
// screen/state machine built on top of it), decodes it with the same
// TJpgDec + tftJpgOutput path drawIcon() already falls back to in display.h,
// and shows it full-size (QVGA, 320x240, fits the panel with no downscaling
// needed). One roof at a time — a "Switch Roof" button cycles through
// NUM_ROOFS — and snapshots are on-demand only (tap Live View / Refresh),
// not an auto-refreshing stream.
//
// Include order: this header must come AFTER touch.h (BtnRect/drawBtn/inBtn/
// btnBack/btnBackHit/layoutDetailButtons/g_view/VIEW_LIVE), display.h
// (tft/HEADER_HEIGHT_PX/tftJpgOutput/renderHomeScreen) and espnow_link.h
// (espnowSendLiveViewRequest/liveViewFrameReady/liveViewTransferTimedOut).
// ============================================================================

#include "display.h"

enum LiveViewState { LV_IDLE, LV_WAITING, LV_READY, LV_ERROR };

static LiveViewState g_lv_state = LV_IDLE;
static int           g_lv_roof  = 1;   // 1-based, currently selected/displayed roof

#define LIVEVIEW_IMG_X  80              // (480 - 320) / 2 — centers the QVGA frame
#define LIVEVIEW_IMG_Y  HEADER_HEIGHT_PX
#define LIVEVIEW_BAR_Y  284

BtnRect btnLvRefresh, btnLvSwitchRoof;

void requestCurrentRoofFrame() {
  espnowSendLiveViewRequest(g_lv_roof);
  g_lv_state = LV_WAITING;
}

void renderLiveViewScreen() {
  tft.fillScreen(TFT_BLACK);
  int w = tft.width();

  tft.fillRect(0, 0, w, HEADER_HEIGHT_PX, TFT_NAVY);
  layoutDetailButtons();
  drawBtn(btnBack, "< Back", TFT_DARKGREY);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextSize(2);
  char title[32];
  snprintf(title, sizeof(title), "Live View - Roof %d/%d", g_lv_roof, NUM_ROOFS);
  tft.drawString(title, w / 2, HEADER_HEIGHT_PX / 2);

  if (g_lv_state == LV_WAITING) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setTextSize(2);
    char msg[40];
    snprintf(msg, sizeof(msg), "Requesting photo from roof %d...", g_lv_roof);
    tft.drawString(msg, w / 2, LIVEVIEW_IMG_Y + (LIVEVIEW_BAR_Y - LIVEVIEW_IMG_Y) / 2);
  } else if (g_lv_state == LV_ERROR) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setTextSize(2);
    char msg[40];
    snprintf(msg, sizeof(msg), "No response from roof %d", g_lv_roof);
    tft.drawString(msg, w / 2, LIVEVIEW_IMG_Y + (LIVEVIEW_BAR_Y - LIVEVIEW_IMG_Y) / 2);
  }
  // LV_READY: the decoded JPEG is drawn directly onto the TFT by
  // liveViewOnFrameReceived() right after this chrome is drawn — not here,
  // since we'd otherwise need to keep the JPEG bytes around just to redraw
  // identical pixels on every re-render.

  btnLvRefresh    = {SIDE_PADDING_PX,          LIVEVIEW_BAR_Y, 140, 30};
  btnLvSwitchRoof = {w - 140 - SIDE_PADDING_PX, LIVEVIEW_BAR_Y, 140, 30};
  drawBtn(btnLvRefresh, "Refresh", TFT_DARKGREY);
  drawBtn(btnLvSwitchRoof, "Switch Roof", NUM_ROOFS > 1 ? TFT_DARKGREY : 0x2104);
}

void openLiveViewScreen() {
  g_view   = VIEW_LIVE;
  g_lv_roof = 1;
  requestCurrentRoofFrame();
  renderLiveViewScreen();
}

void handleLiveViewTouch(int x, int y) {
  if (inBtn(btnBackHit, x, y)) {
    g_view = VIEW_HOME;
    renderHomeScreen();
    return;
  }
  if (inBtn(btnLvRefresh, x, y)) {
    requestCurrentRoofFrame();
    renderLiveViewScreen();
    return;
  }
  if (NUM_ROOFS > 1 && inBtn(btnLvSwitchRoof, x, y)) {
    g_lv_roof = (g_lv_roof % NUM_ROOFS) + 1;
    requestCurrentRoofFrame();
    renderLiveViewScreen();
    return;
  }
}

// Called from loop() when espnow_link.h finishes reassembling a full JPEG.
// Takes ownership of jpeg (always frees it) regardless of whether it's still
// relevant — e.g. the user may have backed out or switched roofs while a
// slower transfer was still in flight.
void liveViewOnFrameReceived(uint8_t roof, uint8_t* jpeg, size_t len) {
  if (g_view != VIEW_LIVE || roof != g_lv_roof) { free(jpeg); return; }

  g_lv_state = LV_READY;
  renderLiveViewScreen();   // chrome first, so a failed decode doesn't leave stale pixels

  TJpgDec.setJpgScale(1);
  TJpgDec.setCallback(tftJpgOutput);
  int rc = TJpgDec.drawJpg(LIVEVIEW_IMG_X, LIVEVIEW_IMG_Y, jpeg, len);
  free(jpeg);

  if (rc != 0) {
    Serial.printf("[LIVEVIEW] drawJpg rc=%d (roof %d, %u bytes)\n", rc, roof, (unsigned)len);
    g_lv_state = LV_ERROR;
    renderLiveViewScreen();
  }
}

// Called from loop() when espnow_link.h gives up waiting on a stalled transfer.
void liveViewOnTimeout() {
  if (g_view != VIEW_LIVE) return;
  g_lv_state = LV_ERROR;
  renderLiveViewScreen();
}
