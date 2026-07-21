#pragma once

// ============================================================================
// Settings sync — mirror all screen settings to/from Realtime Database
// ============================================================================
// The Settings/Alerts screens persist locally in NVS (settings.h "fsettings",
// notifications.h "notif"). That's great for surviving reboots but invisible
// to the Flutter app. This module keeps a single shared copy of every tunable
// setting in RTDB at:
//
//     fridges/{FRIDGE_ID}/settings   (one flat JSON object)
//
// so the screen and the app always agree:
//   * Screen changed -> pushSettingsToRTDB() writes the whole object up (called
//     when the user leaves a settings sub-screen, mirroring how saveSettings()
//     is called there).
//   * App changed     -> settingsSyncPoll() (run from loop()) periodically GETs
//     the object and, if it differs from our own last-pushed copy, applies it
//     to the live globals + NVS + re-renders any open settings view.
//
// Echo-loop guard: every write tags the object with "updated_by". The screen
// writes "screen"; the app writes "app". A poll that reads back our own
// "screen" write is skipped, so a screen push can't bounce back and re-apply
// itself.
//
// Poll, not a second SSE stream: rtdb_stream.h already keeps one persistent
// TLS connection open (inventory). This board only has enough contiguous heap
// for one such connection at a time — a second permanent stream here was
// tried first and left the heap fragmented enough that unrelated one-shot
// HTTPS calls (inventory merge, GM65 barcode lookups) started intermittently
// failing to even open a TLS connection. Settings changes are human-driven
// and infrequent, so a short poll interval (SETSYNC_POLL_MS) trades a few
// seconds of latency for not holding a second connection open at all.
//
// Include order: AFTER settings.h (g_settings/applySettings/saveSettings/
// renderSettingsScreen/renderBuzzerScreen/checkEnvironmentAlert) and
// notifications.h (alert-type globals / saveNotifications / renderNotifSettings
// Screen). Both are included before this file in the .ino.
// ============================================================================

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "parameters.h"
#include "SECRETS.h"

#define SETSYNC_POLL_MS  15000  // how often to check RTDB for an app-side change
#define SETSYNC_TOUCH_QUIET_MS 4000  // ...but never while the user is tapping, see below

static unsigned long g_set_last_poll_ms = 0;
static String        g_set_last_applied;  // last body we applied/pushed, to skip no-op polls

// ----------------------------------------------------------------------------
// Serialize every synced setting into one flat JSON object.
//   serverTs=true adds an updated_at server timestamp (used on writes).
// ----------------------------------------------------------------------------
String buildSettingsJson(bool serverTs) {
  DynamicJsonDocument d(1024);
  d["buzzer_enabled"]    = g_settings.buzzer_enabled;
  d["door_alert_s"]      = g_settings.door_alert_s;
  d["temp_min"]          = g_settings.temp_min;
  d["temp_max"]          = g_settings.temp_max;
  d["hum_min"]           = g_settings.hum_min;
  d["hum_max"]           = g_settings.hum_max;
  d["buzzer_volume"]     = g_settings.buzzer_volume;
  d["buzzer_freq"]       = g_settings.buzzer_freq;
  d["buzzer_duration_s"] = g_settings.buzzer_duration_s;
  d["buzzer_melody"]     = g_settings.buzzer_melody;
  d["notif_retention_d"] = g_notif_retention_d;
  d["expiry_warn_days"]  = g_expiry_warn_days;
  d["expiry_alert_on"]   = g_expiry_alert_on;
  d["expired_alert_on"]  = g_expired_alert_on;
  d["newitem_alert_on"]  = g_newitem_alert_on;
  d["env_alert_on"]      = g_env_alert_on;
  d["door_alert_on"]     = g_door_alert_on;
  d["updated_by"]        = "screen";
  if (serverTs) d["updated_at"][".sv"] = "timestamp";
  String out;
  serializeJson(d, out);
  return out;
}

// ----------------------------------------------------------------------------
// Push the whole settings object up to RTDB. Blocking HTTPS PUT with a couple
// of retries — called from a touch handler when the user leaves a settings
// screen (same spot saveSettings() runs), so it's momentary and infrequent.
// ----------------------------------------------------------------------------
void pushSettingsToRTDB() {
  if (WiFi.status() != WL_CONNECTED) return;

  String url = String(FIREBASE_DATABASE_URL) +
               "/fridges/" + String(FRIDGE_ID) + "/settings.json";
  String body = buildSettingsJson(true);

  for (int attempt = 1; attempt <= 3; attempt++) {
    WiFiClientSecure client;
    prepSecureClient(client);
    HTTPClient http;
    http.setTimeout(5000);
    if (http.begin(client, url)) {
      http.addHeader("Content-Type", "application/json");
      int code = http.PUT(body);
      http.end();
      Serial.printf("[SETSYNC] push -> %d (attempt %d/3)\n", code, attempt);
      if (code == 200) return;
    }
    if (attempt < 3) delay(300);
  }
}

// ----------------------------------------------------------------------------
// Apply an incoming settings JSON object to the live globals. Missing fields
// keep their current value (so a partial write never wipes anything). Returns
// true if it was applied (false = malformed, or our own "screen" echo).
// Runs on loop()'s core, so it's safe to touch NVS and redraw the screen here.
// ----------------------------------------------------------------------------
bool applySettingsJson(const String& body) {
  DynamicJsonDocument d(1536);
  if (deserializeJson(d, body)) return false;
  if (!d.is<JsonObject>()) return false;
  JsonObject o = d.as<JsonObject>();

  // Ignore our own writes bouncing back through the next poll.
  String by = o["updated_by"] | "";
  if (by == "screen") return false;

  g_settings.buzzer_enabled    = o["buzzer_enabled"]    | g_settings.buzzer_enabled;
  g_settings.door_alert_s      = o["door_alert_s"]      | g_settings.door_alert_s;
  g_settings.temp_min          = o["temp_min"]          | g_settings.temp_min;
  g_settings.temp_max          = o["temp_max"]          | g_settings.temp_max;
  g_settings.hum_min           = o["hum_min"]           | g_settings.hum_min;
  g_settings.hum_max           = o["hum_max"]           | g_settings.hum_max;
  g_settings.buzzer_volume     = o["buzzer_volume"]     | g_settings.buzzer_volume;
  g_settings.buzzer_freq       = o["buzzer_freq"]       | g_settings.buzzer_freq;
  g_settings.buzzer_duration_s = o["buzzer_duration_s"] | g_settings.buzzer_duration_s;
  g_settings.buzzer_melody     = o["buzzer_melody"]     | g_settings.buzzer_melody;

  g_notif_retention_d = o["notif_retention_d"] | g_notif_retention_d;
  g_expiry_warn_days  = o["expiry_warn_days"]  | g_expiry_warn_days;
  g_expiry_alert_on   = o["expiry_alert_on"]   | g_expiry_alert_on;
  g_expired_alert_on  = o["expired_alert_on"]  | g_expired_alert_on;
  g_newitem_alert_on  = o["newitem_alert_on"]  | g_newitem_alert_on;
  g_env_alert_on      = o["env_alert_on"]      | g_env_alert_on;
  g_door_alert_on     = o["door_alert_on"]     | g_door_alert_on;

  // Keep values inside the same bounds the +/- buttons enforce.
  if (g_settings.temp_max <= g_settings.temp_min) g_settings.temp_max = g_settings.temp_min + 1;
  if (g_settings.hum_max  <= g_settings.hum_min)  g_settings.hum_max  = g_settings.hum_min  + 5;
  if (g_notif_retention_d < NOTIF_RETENTION_MIN_D) g_notif_retention_d = NOTIF_RETENTION_MIN_D;
  if (g_notif_retention_d > NOTIF_RETENTION_MAX_D) g_notif_retention_d = NOTIF_RETENTION_MAX_D;
  if (g_expiry_warn_days  < EXPIRY_WARN_MIN_D)     g_expiry_warn_days  = EXPIRY_WARN_MIN_D;
  if (g_expiry_warn_days  > EXPIRY_WARN_MAX_D)     g_expiry_warn_days  = EXPIRY_WARN_MAX_D;

  applySettings();       // push buzzer/door values into their live globals
  saveSettings();        // persist to NVS "fsettings"
  saveNotifications();   // persist alert-type toggles to NVS "notif"

  // React to the new alert config just like the on-screen toggles do.
  scanExpiries();
  if (g_env_alert_on) checkEnvironmentAlert();
  else                g_alert_active = false;

  // If a settings screen is open, redraw it so the new values show at once.
  if      (g_view == VIEW_SETTINGS)       renderSettingsScreen();
  else if (g_view == VIEW_BUZZER)         renderBuzzerScreen();
  else if (g_view == VIEW_NOTIF_SETTINGS) renderNotifSettingsScreen();

  Serial.println("[SETSYNC] applied settings from app");
  return true;
}

// ----------------------------------------------------------------------------
// One-shot GET of the current RTDB settings object into `out`. Returns false on
// any HTTP/connection error or if the node is empty ("null").
// ----------------------------------------------------------------------------
bool fetchSettingsFromRTDB(String& out) {
  if (WiFi.status() != WL_CONNECTED) return false;

  String url = String(FIREBASE_DATABASE_URL) +
               "/fridges/" + String(FRIDGE_ID) + "/settings.json";

  WiFiClientSecure client;
  prepSecureClient(client);
  HTTPClient http;
  http.setTimeout(5000);
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  if (code != 200) { http.end(); return false; }
  out = http.getString();
  http.end();
  out.trim();
  return out.length() > 0 && out != "null";
}

// ----------------------------------------------------------------------------
// Init: adopt whatever the cloud already holds (so the app is the shared source
// of truth on boot); if the node is empty, seed it from our NVS settings.
// ----------------------------------------------------------------------------
void initSettingsSync() {
  String body;
  if (fetchSettingsFromRTDB(body)) {
    // Force-apply on boot even if it was last written by the screen: on a fresh
    // boot our in-RAM globals just came from NVS and should match anyway, but
    // adopting the cloud copy keeps a reflashed board in sync too.
    DynamicJsonDocument d(1536);
    if (!deserializeJson(d, body) && d.is<JsonObject>()) {
      JsonObject o = d.as<JsonObject>();
      String by = o["updated_by"] | "";
      // Skip applySettingsJson's "screen" echo guard on boot so we still adopt
      // a screen-authored cloud copy; but if it was app-authored, apply fully.
      if (by == "app") applySettingsJson(body);
    }
    g_set_last_applied = body;
  } else {
    pushSettingsToRTDB();   // seed an empty node from our current settings
  }
}

// ----------------------------------------------------------------------------
// Called every loop(): every SETSYNC_POLL_MS, GET the settings object and — if
// it's different from the last body we saw — apply it. The equality check is
// what keeps this idle: without it, an app-authored value would get re-applied
// (NVS write + screen re-render) on every single poll instead of once.
// ----------------------------------------------------------------------------
void settingsSyncPoll() {
  if (millis() - g_set_last_poll_ms < SETSYNC_POLL_MS) return;

  // fetchSettingsFromRTDB() is a blocking HTTPS GET (fresh TLS handshake) —
  // loop() doesn't reach handleTouch() for as long as it runs, so a tap that
  // lands inside that window is simply never sampled. On a screen the user is
  // actively poking at, that reads as "the buttons randomly don't work".
  // Hold the poll off while taps are still coming in; settings changes are
  // human-driven and infrequent, so a few extra seconds of latency costs
  // nothing. Don't stamp g_set_last_poll_ms here — otherwise each deferral
  // would restart the interval and a steady stream of taps could starve the
  // sync entirely.
  if (millis() - g_last_touch_ms < SETSYNC_TOUCH_QUIET_MS) return;

  g_set_last_poll_ms = millis();

  String body;
  if (!fetchSettingsFromRTDB(body)) return;
  if (body == g_set_last_applied) return;
  g_set_last_applied = body;
  applySettingsJson(body);
}
