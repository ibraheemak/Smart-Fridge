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

#define EXPIRY_WARN_MIN_D        0    // "warn N days before expiry" bounds
#define EXPIRY_WARN_MAX_D       30
#define EXPIRY_WARN_DEFAULT      3

struct Notification {
  String text;
  time_t created;   // epoch seconds (0 = unknown, e.g. logged before NTP sync)
};

Notification g_notifs[MAX_NOTIFICATIONS];  // index 0 = newest
int g_notif_count       = 0;
int g_notif_retention_d = NOTIF_DEFAULT_RET_DAYS;
int g_notif_page        = 0;

// Alert-type config (persisted alongside the list in NVS "notif"). Each toggle
// gates one kind of alert the fridge raises; scanExpiries()/checkEnvironmentAlert()
// /the door-open check all consult these before logging anything.
int  g_expiry_warn_days = EXPIRY_WARN_DEFAULT;  // days before expiry to warn
bool g_expiry_alert_on  = true;                 // "Expires soon" (about-to-expire)
bool g_expired_alert_on = true;                 // "Expired" (already past date)
bool g_newitem_alert_on = true;                 // log a notification per new item
bool g_env_alert_on     = true;                 // temperature/humidity out-of-range
bool g_door_alert_on    = true;                 // door left open too long

// ----------------------------------------------------------------------------
// Persistence (NVS namespace "notif")
// ----------------------------------------------------------------------------
void saveNotifications() {
  Preferences p;
  p.begin("notif", false);
  p.clear();
  p.putInt("ret", g_notif_retention_d);
  p.putInt("ewd", g_expiry_warn_days);
  p.putBool("eon", g_expiry_alert_on);
  p.putBool("exon", g_expired_alert_on);
  p.putBool("non", g_newitem_alert_on);
  p.putBool("envon", g_env_alert_on);
  p.putBool("dooron", g_door_alert_on);
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
  g_expiry_warn_days  = p.getInt("ewd", EXPIRY_WARN_DEFAULT);
  g_expiry_alert_on   = p.getBool("eon", true);
  g_expired_alert_on  = p.getBool("exon", true);
  g_newitem_alert_on  = p.getBool("non", true);
  g_env_alert_on      = p.getBool("envon", true);
  g_door_alert_on     = p.getBool("dooron", true);
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
  if (g_expiry_warn_days   < EXPIRY_WARN_MIN_D)    g_expiry_warn_days   = EXPIRY_WARN_MIN_D;
  if (g_expiry_warn_days   > EXPIRY_WARN_MAX_D)    g_expiry_warn_days   = EXPIRY_WARN_MAX_D;
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

// True if an identical alert message is already in the log. Used to dedupe
// expiry alerts so a given item+date isn't re-logged on every scan / reboot.
bool notificationExists(const String& text) {
  for (int i = 0; i < g_notif_count; i++)
    if (g_notifs[i].text == text) return true;
  return false;
}

// Add an alert only if the exact same text isn't already logged.
void addNotificationUnique(const String& text) {
  if (notificationExists(text)) return;
  addNotification(text);
}

// Convert a "YYYY-MM-DD" expiry string to a day number (epoch/86400). -1 if
// malformed. Noon avoids DST edge cases shifting the date.
static long expiryToDayNumber(const String& ymd) {
  if (ymd.length() != 10) return -1;
  struct tm t = {};
  t.tm_year = ymd.substring(0, 4).toInt() - 1900;
  t.tm_mon  = ymd.substring(5, 7).toInt() - 1;
  t.tm_mday = ymd.substring(8, 10).toInt();
  t.tm_hour = 12;
  time_t e = mktime(&t);
  if (e < 0) return -1;
  return (long) (e / 86400L);
}

// Scan the live inventory (g_items[], from display.h) for units that are
// expired or within g_expiry_warn_days of expiring, and log one alert each.
// Deduped by exact text, so repeat scans don't pile up copies. No-op until the
// clock is set or if expiry alerts are switched off.
void scanExpiries() {
  if (!g_expiry_alert_on && !g_expired_alert_on) return;   // both kinds off
  time_t now = time(nullptr);
  if (now < 24 * 3600) return;                 // clock not synced yet
  long today = (long) (now / 86400L);
  for (int i = 0; i < g_item_count; i++) {
    for (int j = 0; j < g_items[i].expiry_count; j++) {
      const String& e = g_items[i].expiries[j];
      long d = expiryToDayNumber(e);
      if (d < 0) continue;                     // empty/malformed slot
      long left = d - today;
      String msg;
      if (left < 0) {
        if (!g_expired_alert_on) continue;     // "already expired" alerts off
        msg = "Expired: " + g_items[i].name + " (" + e + ")";
      } else if (left <= g_expiry_warn_days) {
        if (!g_expiry_alert_on) continue;      // "about to expire" alerts off
        msg = "Expires soon: " + g_items[i].name + " (" + e + ")";
      } else continue;
      addNotificationUnique(msg);
    }
  }
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
  if (inBtn(btnNotifOptions, x, y)) { g_notif_settings_prev = VIEW_NOTIFICATIONS; openNotifSettingsScreen(); return; }

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
// Settings sub-screen (VIEW_NOTIF_SETTINGS) — one row per alert type + config.
// Reached from the alerts list ("Config") or the main Settings "Alerts" row.
//
// There are more setting rows than fit a 320px screen, so it's paged (5 rows +
// a Prev | Clear All | Next bar, mirroring the notifications list screen). Rows,
// in order (index = NS item id), reuse settings.h's setRowY / SET_* layout:
//   0  Auto-delete    [ -  Nd  + ]   retention window (old alerts auto-delete)
//   1  Warn before    [ -  Nd  + ]   days before expiry to warn
//   2  Expires soon   [ ON/OFF   ]   about-to-expire alerts   (g_expiry_alert_on)
//   3  Expired        [ ON/OFF   ]   already-past alerts      (g_expired_alert_on)
//   4  New-item       [ ON/OFF   ]   per new detected item    (g_newitem_alert_on)
//   5  Temp/Humid     [ ON/OFF   ]   out-of-range env alert   (g_env_alert_on)
//   6  Door-open      [ ON/OFF   ]   door left open too long  (g_door_alert_on)
// Rows 2-6 are ON/OFF toggles; rows 0-1 are +/- value rows.
// ----------------------------------------------------------------------------
#define NS_ITEM_COUNT   7
#define NS_PER_PAGE     5

int g_ns_page = 0;

// Per-visible-row hit rects. A value row uses Minus+Plus; a toggle row uses
// Toggle; the unused ones are zeroed so inBtn() never matches them.
BtnRect btnNSMinus[NS_PER_PAGE], btnNSPlus[NS_PER_PAGE], btnNSToggle[NS_PER_PAGE];
BtnRect btnNSPrev, btnNSClear, btnNSNext;

static bool nsItemIsToggle(int idx) { return idx >= 2; }   // 0,1 value; 2..6 toggle

static const char* nsItemLabel(int idx) {
  switch (idx) {
    case 0: return "Auto-delete";
    case 1: return "Warn before";
    case 2: return "Expires soon";
    case 3: return "Expired";
    case 4: return "New-item";
    case 5: return "Temp/Humid";
    case 6: return "Door-open";
  }
  return "";
}

static bool nsToggleGet(int idx) {
  switch (idx) {
    case 2: return g_expiry_alert_on;
    case 3: return g_expired_alert_on;
    case 4: return g_newitem_alert_on;
    case 5: return g_env_alert_on;
    case 6: return g_door_alert_on;
  }
  return false;
}

static void nsToggleSet(int idx, bool v) {
  switch (idx) {
    case 2: g_expiry_alert_on  = v; break;
    case 3: g_expired_alert_on = v; break;
    case 4: g_newitem_alert_on = v; break;
    case 5: g_env_alert_on     = v; break;
    case 6: g_door_alert_on    = v; break;
  }
}

// The +/- value rows (0,1). Returns the display string for the value box.
static String nsValueStr(int idx) {
  if (idx == 0) return String(g_notif_retention_d) + "d";
  return String(g_expiry_warn_days) + "d";
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

  int pages = (NS_ITEM_COUNT + NS_PER_PAGE - 1) / NS_PER_PAGE;
  if (g_ns_page >= pages) g_ns_page = pages - 1;
  if (g_ns_page < 0) g_ns_page = 0;
  int start = g_ns_page * NS_PER_PAGE;

  for (int r = 0; r < NS_PER_PAGE; r++) {
    btnNSMinus[r] = {0, 0, 0, 0};
    btnNSPlus[r]  = {0, 0, 0, 0};
    btnNSToggle[r]= {0, 0, 0, 0};
    int idx = start + r;
    if (idx >= NS_ITEM_COUNT) continue;

    int ry = setRowY(r) + 2;
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.drawString(nsItemLabel(idx), SIDE_PADDING_PX, setRowY(r) + 2 + SET_BTN_H / 2);

    if (nsItemIsToggle(idx)) {
      btnNSToggle[r] = {SET_MINUS_X, ry, 150, SET_BTN_H};
      bool on = nsToggleGet(idx);
      drawBtn(btnNSToggle[r], on ? "ON" : "OFF", on ? TFT_DARKGREEN : 0x6000);
    } else {
      btnNSMinus[r] = {SET_MINUS_X, ry, SET_BTN_W, SET_BTN_H};
      btnNSPlus[r]  = {SET_PLUS_X,  ry, SET_BTN_W, SET_BTN_H};
      drawBtn(btnNSMinus[r], "-", TFT_DARKGREY);
      drawBtn(btnNSPlus[r],  "+", TFT_DARKGREY);
      drawSetValue(r, nsValueStr(idx));
    }
  }

  // Bottom bar: page indicator + Prev | Clear All | Next.
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setTextSize(1);
  char pg[24];
  snprintf(pg, sizeof(pg), "Page %d/%d", g_ns_page + 1, pages);
  tft.drawString(pg, w / 2, NOTIF_BAR_Y - 10);

  btnNSPrev  = {8,          NOTIF_BAR_Y, 92,  SET_BTN_H};
  btnNSClear = {w / 2 - 70, NOTIF_BAR_Y, 140, SET_BTN_H};
  btnNSNext  = {w - 100,    NOTIF_BAR_Y, 92,  SET_BTN_H};
  drawBtn(btnNSPrev,  "< Prev",    g_ns_page > 0            ? TFT_DARKGREY : 0x2104);
  drawBtn(btnNSClear, "Clear All", g_notif_count > 0        ? 0x6000       : 0x2104);
  drawBtn(btnNSNext,  "Next >",    g_ns_page < pages - 1    ? TFT_DARKGREY : 0x2104);
}

void openNotifSettingsScreen() {
  g_view = VIEW_NOTIF_SETTINGS;
  g_ns_page = 0;
  renderNotifSettingsScreen();
}

void handleNotifSettingsTouch(int x, int y) {
  if (inBtn(btnBackHit, x, y)) {
    saveNotifications();          // persist retention + all alert-type toggles
    purgeOldNotifications();      // apply the retention change immediately
    if (g_notif_settings_prev == VIEW_SETTINGS) {
      g_view = VIEW_SETTINGS;
      renderSettingsScreen();     // defined in settings.h (included earlier)
    } else {
      g_view = VIEW_NOTIFICATIONS;
      renderNotificationsScreen();
    }
    return;
  }

  int pages = (NS_ITEM_COUNT + NS_PER_PAGE - 1) / NS_PER_PAGE;
  if (inBtn(btnNSPrev, x, y)) {
    if (g_ns_page > 0) { g_ns_page--; renderNotifSettingsScreen(); }
    return;
  }
  if (inBtn(btnNSNext, x, y)) {
    if (g_ns_page < pages - 1) { g_ns_page++; renderNotifSettingsScreen(); }
    return;
  }
  if (inBtn(btnNSClear, x, y)) {
    if (g_notif_count > 0) { clearAllNotifications(); renderNotifSettingsScreen(); }
    return;
  }

  int start = g_ns_page * NS_PER_PAGE;
  for (int r = 0; r < NS_PER_PAGE; r++) {
    int idx = start + r;
    if (idx >= NS_ITEM_COUNT) break;

    if (nsItemIsToggle(idx)) {
      if (btnNSToggle[r].w > 0 && inBtn(btnNSToggle[r], x, y)) {
        bool nv = !nsToggleGet(idx);
        nsToggleSet(idx, nv);
        drawBtn(btnNSToggle[r], nv ? "ON" : "OFF", nv ? TFT_DARKGREEN : 0x6000);
        // Side-effects: catch up / clear the alert this toggle governs.
        if ((idx == 2 || idx == 3) && nv) scanExpiries();  // expiry: log anything due
        if (idx == 5) {                                    // env: re-check or clear
          if (nv) checkEnvironmentAlert();
          else    g_alert_active = false;
        }
        return;
      }
    } else {
      if (btnNSMinus[r].w > 0 && inBtn(btnNSMinus[r], x, y)) {
        if (idx == 0) g_notif_retention_d = max(NOTIF_RETENTION_MIN_D, g_notif_retention_d - 1);
        else          g_expiry_warn_days  = max(EXPIRY_WARN_MIN_D,     g_expiry_warn_days  - 1);
        drawSetValue(r, nsValueStr(idx));
        return;
      }
      if (btnNSPlus[r].w > 0 && inBtn(btnNSPlus[r], x, y)) {
        if (idx == 0) g_notif_retention_d = min(NOTIF_RETENTION_MAX_D, g_notif_retention_d + 1);
        else          g_expiry_warn_days  = min(EXPIRY_WARN_MAX_D,     g_expiry_warn_days  + 1);
        drawSetValue(r, nsValueStr(idx));
        return;
      }
    }
  }
}
