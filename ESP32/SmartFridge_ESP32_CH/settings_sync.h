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
//   * App changed     -> a long-lived SSE "settings changed" listener (its own
//     FreeRTOS task on core 0, exactly like rtdb_stream.h) flags a change;
//     settingsSyncPoll() (run from loop() on core 1) then GETs the object and
//     applies it to the live globals + NVS + re-renders any open settings view.
//
// Echo-loop guard: every write tags the object with "updated_by". The screen
// writes "screen"; the app writes "app". When the listener wakes us for our own
// write we read updated_by=="screen" and skip it, so a screen push can't bounce
// back and re-apply itself.
//
// Why a second SSE stream (not just extend rtdb_stream.h): an SSE connection
// watches exactly one path. rtdb_stream.h watches inventory_meta/updated_at;
// this one watches settings. It follows the same proven pattern — its own task
// so a blocking TLS connect never stalls touch — and the same short connect/
// handshake timeouts, so it can't freeze the UI even when RTDB is unreachable.
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

#define SETSTREAM_RETRY_MS    5000    // how often to retry connecting while down
#define SETSTREAM_TIMEOUT_MS  60000   // no bytes at all this long -> assume dead

// ---- listener (SSE doorbell) state, all touched only by the stream task -----
static WiFiClientSecure g_set_client;
static bool             g_set_connected     = false;
static unsigned long    g_set_last_retry_ms = 0;
static unsigned long    g_set_last_rx_ms    = 0;
static String           g_set_line_buf;
static String           g_set_last_event;
static volatile bool    g_set_change_flag   = false;  // set by task, drained in loop()
static TaskHandle_t     g_set_task_handle   = nullptr;

// ----------------------------------------------------------------------------
// Split "https://<host>/..." into just the host for a raw TLS connect.
// ----------------------------------------------------------------------------
static String settingsRtdbHost() {
  String url = String(FIREBASE_DATABASE_URL);
  int host_start = url.indexOf("://") + 3;
  int path_start = url.indexOf('/', host_start);
  return (path_start < 0) ? url.substring(host_start)
                          : url.substring(host_start, path_start);
}

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
    client.setInsecure();
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

  // Ignore our own writes bouncing back through the listener.
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
  client.setInsecure();
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
// SSE listener task — same pattern as rtdb_stream.h. Watches the settings node
// and, on any real "put"/"patch", raises g_set_change_flag. It never parses
// values itself (settingsSyncPoll() does the GET+apply on loop()'s core); it's
// purely a doorbell.
// ----------------------------------------------------------------------------
static void settingsStreamDisconnect() {
  g_set_client.stop();
  g_set_connected = false;
  g_set_line_buf = "";
  g_set_last_event = "";
}

static bool settingsStreamConnect() {
  String host = settingsRtdbHost();
  g_set_client.setInsecure();
  g_set_client.setConnectionTimeout(5000);
  g_set_client.setHandshakeTimeout(5);   // seconds
  if (!g_set_client.connect(host.c_str(), 443)) {
    Serial.println("[SETSYNC] stream connect failed");
    return false;
  }
  String path = "/fridges/" + String(FRIDGE_ID) + "/settings.json";
  g_set_client.print(
    "GET " + path + " HTTP/1.1\r\n" +
    "Host: " + host + "\r\n" +
    "Accept: text/event-stream\r\n" +
    "Connection: keep-alive\r\n\r\n"
  );
  Serial.println("[SETSYNC] stream connecting...");
  return true;
}

// One SSE line. Any non-keep-alive "put"/"patch" event with a data payload is a
// real change -> ring the doorbell.
static bool settingsHandleLine(const String& line) {
  g_set_last_rx_ms = millis();
  if (line.startsWith("event: ")) {
    g_set_last_event = line.substring(7);
    return false;
  }
  if (!line.startsWith("data: ")) return false;
  if (g_set_last_event == "keep-alive") return false;
  if (g_set_last_event != "put" && g_set_last_event != "patch") return false;
  return true;
}

static void settingsStreamTask(void* pv) {
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      if (g_set_connected) settingsStreamDisconnect();
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }
    if (!g_set_connected) {
      if (millis() - g_set_last_retry_ms < SETSTREAM_RETRY_MS) {
        vTaskDelay(pdMS_TO_TICKS(100));
        continue;
      }
      g_set_last_retry_ms = millis();
      if (!settingsStreamConnect()) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
      g_set_connected  = true;
      g_set_last_rx_ms = millis();
      continue;
    }
    if (!g_set_client.connected()) {
      Serial.println("[SETSYNC] stream dropped — will reconnect");
      settingsStreamDisconnect();
      continue;
    }
    if (millis() - g_set_last_rx_ms > SETSTREAM_TIMEOUT_MS) {
      Serial.println("[SETSYNC] stream stalled — reconnecting");
      settingsStreamDisconnect();
      continue;
    }

    bool changed = false;
    while (g_set_client.available()) {
      char c = g_set_client.read();
      if (c == '\n') {
        String line = g_set_line_buf;
        g_set_line_buf = "";
        if (line.endsWith("\r")) line.remove(line.length() - 1);
        if (settingsHandleLine(line)) changed = true;
      } else {
        g_set_line_buf += c;
      }
    }
    if (changed) g_set_change_flag = true;

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// ----------------------------------------------------------------------------
// Init: adopt whatever the cloud already holds (so the app is the shared source
// of truth on boot); if the node is empty, seed it from our NVS settings. Then
// start the listener task.
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
  } else {
    pushSettingsToRTDB();   // seed an empty node from our current settings
  }

  xTaskCreatePinnedToCore(settingsStreamTask, "set_stream", 10240, nullptr, 1,
                          &g_set_task_handle, 0);
}

// ----------------------------------------------------------------------------
// Called every loop(): if the listener rang, GET the object and apply it.
// Non-blocking except for the momentary GET, which only runs on a real change.
// ----------------------------------------------------------------------------
void settingsSyncPoll() {
  if (!g_set_change_flag) return;
  g_set_change_flag = false;
  String body;
  if (fetchSettingsFromRTDB(body)) applySettingsJson(body);
}
