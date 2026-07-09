#pragma once

// ============================================================================
// Notifications (alerts) — persistent log + list screen + settings sub-screen
//
// Every alert the fridge raises (temperature/humidity out of range, door left
// open, ...) is logged here via addNotification(). The home "ALERTS" tile opens
// VIEW_NOTIFICATIONS, a paged list where each entry can be deleted individually
// or all at once ("Clear All"). VIEW_NOTIF_SETTINGS lets the user pick a
// retention window (X days) — notifications older than that are removed
// automatically (on add, on open, and by a periodic sweep in loop()).
//
// The list persists in NVS (Preferences namespace "notif") so it survives
// reboots — which it must, since retention spans days.
//
// Include order: this header must come AFTER touch.h (BtnRect/drawBtn/inBtn/
// btnBack/btnBackHit/layoutDetailButtons/g_view/VIEW_*), display.h (tft /
// HEADER_HEIGHT_PX / SIDE_PADDING_PX / renderHomeScreen) and settings.h
// (setRowY / SET_* layout macros / drawSetValue).
// ============================================================================

#include <Preferences.h>
#include "time.h"
#include "display.h"

#define MAX_NOTIFICATIONS       30
#define NOTIF_RETENTION_MIN_D    1
#define NOTIF_RETENTION_MAX_D   90
#define NOTIF_DEFAULT_RET_DAYS   7
#define NOTIF_PER_PAGE           4    // rows shown per page on the list screen

struct Notification {
  String text;
  time_t created;   // epoch seconds (0 = unknown, e.g. logged before NTP sync)
};

Notification g_notifs[MAX_NOTIFICATIONS];  // index 0 = newest
int g_notif_count       = 0;
int g_notif_retention_d = NOTIF_DEFAULT_RET_DAYS;
int g_notif_page        = 0;

// ----------------------------------------------------------------------------
// Persistence (NVS namespace "notif")
// ----------------------------------------------------------------------------
void saveNotifications() {
  Preferences p;
  p.begin("notif", false);
  p.clear();
  p.putInt("ret", g_notif_retention_d);
  p.putInt("cnt", g_notif_count);
  for (int i = 0; i < g_notif_count; i++) {
    char k[8];
    snprintf(k, sizeof(k), "n%d", i);
    // Store "<epoch>|<text>" in one key; text may not contain '|' (our alert
    // messages never do), and we split on the first '|' when loading.
    p.putString(k, String((long) g_notifs[i].created) + "|" + g_notifs[i].text);
  }
  p.end();
}

void loadNotifications() {
  Preferences p;
  p.begin("notif", true);   // read-only
  g_notif_retention_d = p.getInt("ret", NOTIF_DEFAULT_RET_DAYS);
  g_notif_count       = p.getInt("cnt", 0);
  if (g_notif_count < 0) g_notif_count = 0;
  if (g_notif_count > MAX_NOTIFICATIONS) g_notif_count = MAX_NOTIFICATIONS;
  for (int i = 0; i < g_notif_count; i++) {
    char k[8];
    snprintf(k, sizeof(k), "n%d", i);
    String v = p.getString(k, "");
    int bar = v.indexOf('|');
    if (bar < 0) { g_notifs[i].created = 0; g_notifs[i].text = v; }
    else {
      g_notifs[i].created = (time_t) v.substring(0, bar).toInt();
      g_notifs[i].text    = v.substring(bar + 1);
    }
  }
  p.end();
  if (g_notif_retention_d < NOTIF_RETENTION_MIN_D) g_notif_retention_d = NOTIF_RETENTION_MIN_D;
  if (g_notif_retention_d > NOTIF_RETENTION_MAX_D) g_notif_retention_d = NOTIF_RETENTION_MAX_D;
}

// Drop notifications older than the retention window. Returns true if any were
// removed. Skipped until the clock is set (time < 1 day past epoch) so a
// not-yet-synced RTC can't wipe everything on boot. Entries with created==0
// (logged before NTP sync) are always kept.
bool purgeOldNotifications() {
  if (g_notif_count == 0) return false;
  time_t now = time(nullptr);
  if (now < 24 * 3600) return false;
  time_t cutoff = now - (time_t) g_notif_retention_d * 86400L;
  int w = 0;
  bool removed = false;
  for (int r = 0; r < g_notif_count; r++) {
    if (g_notifs[r].created == 0 || g_notifs[r].created >= cutoff) {
      if (w != r) g_notifs[w] = g_notifs[r];
      w++;
    } else {
      removed = true;
    }
  }
  g_notif_count = w;
  return removed;
}

// Prepend a new alert (newest first). Drops the oldest if the log is full.
// Call from anywhere an alert is raised.
void addNotification(const String& text) {
  purgeOldNotifications();
  int keep = g_notif_count;
  if (keep > MAX_NOTIFICATIONS - 1) keep = MAX_NOTIFICATIONS - 1;  // make room, drop oldest
  for (int i = keep; i > 0; i--) g_notifs[i] = g_notifs[i - 1];
  g_notifs[0].text    = text;
  g_notifs[0].created = time(nullptr);
  g_notif_count = keep + 1;
  saveNotifications();
  Serial.printf("[NOTIF] + \"%s\" (count=%d)\n", text.c_str(), g_notif_count);
}

void deleteNotification(int idx) {
  if (idx < 0 || idx >= g_notif_count) return;
  for (int i = idx; i < g_notif_count - 1; i++) g_notifs[i] = g_notifs[i + 1];
  g_notif_count--;
  saveNotifications();
}

void clearAllNotifications() {
  g_notif_count = 0;
  g_notif_page  = 0;
  saveNotifications();
}

// "3m ago" / "5h ago" / "2d ago" for the list rows.
String notifAgeStr(time_t created) {
  if (created == 0) return "";
  time_t now = time(nullptr);
  if (now <= created) return "now";
  long s = (long) (now - created);
  if (s < 60)    return "now";
  if (s < 3600)  return String(s / 60) + "m ago";
  if (s < 86400) return String(s / 3600) + "h ago";
  return String(s / 86400) + "d ago";
}

// ----------------------------------------------------------------------------
// List screen (VIEW_NOTIFICATIONS)
// ----------------------------------------------------------------------------
#define NOTIF_LIST_TOP  (HEADER_HEIGHT_PX + 6)
#define NOTIF_ROW_H      46
#define NOTIF_BAR_Y      276    // bottom button bar top

BtnRect btnNotifOptions;                 // header: open settings
BtnRect btnNotifPrev, btnNotifClear, btnNotifNext;   // bottom bar
BtnRect btnNotifDel[NOTIF_PER_PAGE];     // per-row delete buttons

void openNotifSettingsScreen();          // defined below, used by handler

void renderNotificationsScreen() {
  purgeOldNotifications();
  tft.fillScreen(TFT_BLACK);
  int w = tft.width(), h = tft.height();

  // Header: back (left) + title + Config (right).
  tft.fillRect(0, 0, w, HEADER_HEIGHT_PX, TFT_NAVY);
  layoutDetailButtons();
  drawBtn(btnBack, "< Back", TFT_DARKGREY);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextSize(2);
  tft.drawString("Alerts", w / 2, HEADER_HEIGHT_PX / 2);

  btnNotifOptions = {w - 92, 6, 84, HEADER_HEIGHT_PX - 12};
  drawBtn(btnNotifOptions, "Config", 0x2945);

  int pages = (g_notif_count + NOTIF_PER_PAGE - 1) / NOTIF_PER_PAGE;
  if (pages == 0) pages = 1;
  if (g_notif_page >= pages) g_notif_page = pages - 1;
  if (g_notif_page < 0) g_notif_page = 0;

  if (g_notif_count == 0) {
    for (int r = 0; r < NOTIF_PER_PAGE; r++) btnNotifDel[r] = {0, 0, 0, 0};
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setTextSize(2);
    tft.drawString("No notifications", w / 2, h / 2 - 20);
  } else {
    int start = g_notif_page * NOTIF_PER_PAGE;
    for (int r = 0; r < NOTIF_PER_PAGE; r++) {
      int idx = start + r;
      int ry  = NOTIF_LIST_TOP + r * NOTIF_ROW_H;
      if (idx >= g_notif_count) { btnNotifDel[r] = {0, 0, 0, 0}; continue; }

      tft.fillRoundRect(8, ry, w - 16, NOTIF_ROW_H - 6, 6, 0x18E3);
      tft.drawRoundRect(8, ry, w - 16, NOTIF_ROW_H - 6, 6, TFT_DARKGREY);

      // Delete button (right edge of the row).
      btnNotifDel[r] = {w - 58, ry + 4, 44, NOTIF_ROW_H - 14};
      drawBtn(btnNotifDel[r], "X", 0x6000);

      // Message (truncated to fit) + relative age below it.
      String msg = g_notifs[idx].text;
      if (msg.length() > 24) msg = msg.substring(0, 24) + "..";
      tft.setTextDatum(ML_DATUM);
      tft.setTextColor(TFT_WHITE, 0x18E3);
      tft.setTextSize(2);
      tft.drawString(msg, 16, ry + 14);

      tft.setTextSize(1);
      tft.setTextColor(TFT_LIGHTGREY, 0x18E3);
      tft.drawString(notifAgeStr(g_notifs[idx].created), 16, ry + 32);
    }
  }

  // Page indicator + bottom bar: Prev | Clear All | Next.
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setTextSize(1);
  char pg[28];
  snprintf(pg, sizeof(pg), "Page %d/%d  (%d total)", g_notif_page + 1, pages, g_notif_count);
  tft.drawString(pg, w / 2, NOTIF_BAR_Y - 10);

  btnNotifPrev  = {8,             NOTIF_BAR_Y, 92,  38};
  btnNotifClear = {w / 2 - 70,    NOTIF_BAR_Y, 140, 38};
  btnNotifNext  = {w - 100,       NOTIF_BAR_Y, 92,  38};
  drawBtn(btnNotifPrev,  "< Prev",    g_notif_page > 0         ? TFT_DARKGREY : 0x2104);
  drawBtn(btnNotifClear, "Clear All", g_notif_count > 0        ? 0x6000       : 0x2104);
  drawBtn(btnNotifNext,  "Next >",    g_notif_page < pages - 1 ? TFT_DARKGREY : 0x2104);
}

void openNotificationsScreen() {
  g_view = VIEW_NOTIFICATIONS;
  g_notif_page = 0;
  renderNotificationsScreen();
}

void handleNotificationsTouch(int x, int y) {
  if (inBtn(btnBackHit, x, y))      { g_view = VIEW_HOME; renderHomeScreen(); return; }
  if (inBtn(btnNotifOptions, x, y)) { openNotifSettingsScreen(); return; }

  int pages = (g_notif_count + NOTIF_PER_PAGE - 1) / NOTIF_PER_PAGE;
  if (pages == 0) pages = 1;

  if (inBtn(btnNotifPrev, x, y)) {
    if (g_notif_page > 0) { g_notif_page--; renderNotificationsScreen(); }
    return;
  }
  if (inBtn(btnNotifNext, x, y)) {
    if (g_notif_page < pages - 1) { g_notif_page++; renderNotificationsScreen(); }
    return;
  }
  if (inBtn(btnNotifClear, x, y)) {
    if (g_notif_count > 0) { clearAllNotifications(); renderNotificationsScreen(); }
    return;
  }

  // Per-row delete.
  int start = g_notif_page * NOTIF_PER_PAGE;
  for (int r = 0; r < NOTIF_PER_PAGE; r++) {
    int idx = start + r;
    if (idx >= g_notif_count) break;
    if (btnNotifDel[r].w > 0 && inBtn(btnNotifDel[r], x, y)) {
      deleteNotification(idx);
      int np = (g_notif_count + NOTIF_PER_PAGE - 1) / NOTIF_PER_PAGE;
      if (np == 0) np = 1;
      if (g_notif_page >= np) g_notif_page = np - 1;
      renderNotificationsScreen();
      return;
    }
  }
}

// ----------------------------------------------------------------------------
// Settings sub-screen (VIEW_NOTIF_SETTINGS) — retention window + clear-all
// ----------------------------------------------------------------------------
BtnRect btnNotifRetM, btnNotifRetP, btnNotifClearNow;

void drawNotifRetentionValue() {
  drawSetValue(1, String(g_notif_retention_d) + "d");   // reuses settings.h layout
}

void renderNotifSettingsScreen() {
  tft.fillScreen(TFT_BLACK);
  int w = tft.width();

  tft.fillRect(0, 0, w, HEADER_HEIGHT_PX, TFT_NAVY);
  layoutDetailButtons();
  drawBtn(btnBack, "< Back", TFT_DARKGREY);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextSize(2);
  tft.drawString("Alert Settings", w / 2, HEADER_HEIGHT_PX / 2);

  // Row 1: auto-delete after N days.
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("Auto-delete after", SIDE_PADDING_PX, setRowY(1) + 2 + SET_BTN_H / 2);

  btnNotifRetM = {SET_MINUS_X, setRowY(1) + 2, SET_BTN_W, SET_BTN_H};
  btnNotifRetP = {SET_PLUS_X,  setRowY(1) + 2, SET_BTN_W, SET_BTN_H};
  drawBtn(btnNotifRetM, "-", TFT_DARKGREY);
  drawBtn(btnNotifRetP, "+", TFT_DARKGREY);
  drawNotifRetentionValue();

  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setTextSize(1);
  tft.drawString("Alerts older than this are deleted automatically.",
                 SIDE_PADDING_PX, setRowY(2) + 10);

  btnNotifClearNow = {(w - 200) / 2, setRowY(4), 200, SET_BTN_H + 4};
  drawBtn(btnNotifClearNow, "Clear All Now", 0x6000);
}

void openNotifSettingsScreen() {
  g_view = VIEW_NOTIF_SETTINGS;
  renderNotifSettingsScreen();
}

void handleNotifSettingsTouch(int x, int y) {
  if (inBtn(btnBackHit, x, y)) {
    saveNotifications();          // persist the retention change
    purgeOldNotifications();      // apply it immediately
    g_view = VIEW_NOTIFICATIONS;
    renderNotificationsScreen();
    return;
  }
  if (inBtn(btnNotifRetM, x, y)) {
    g_notif_retention_d = max(NOTIF_RETENTION_MIN_D, g_notif_retention_d - 1);
    drawNotifRetentionValue();
    return;
  }
  if (inBtn(btnNotifRetP, x, y)) {
    g_notif_retention_d = min(NOTIF_RETENTION_MAX_D, g_notif_retention_d + 1);
    drawNotifRetentionValue();
    return;
  }
  if (inBtn(btnNotifClearNow, x, y)) {
    clearAllNotifications();
    renderNotifSettingsScreen();
    return;
  }
}
