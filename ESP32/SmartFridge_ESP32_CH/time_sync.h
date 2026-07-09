#pragma once

// ============================================================================
// NTP time sync — tracks whether the clock is actually trustworthy yet
// ============================================================================
// configTzTime() kicks off SNTP in the background; the ESP32's clock reads
// back near epoch 0 ("1970-01-01") until the first reply arrives. The old
// configureTime() waited up to 10s for that in setup() and then proceeded
// unconditionally either way, so on a slow/flaky WiFi join the whole rest of
// the sketch (expiry dates, Firestore updatedAt timestamps, the home-screen
// clock, the monthly stats document path) would silently run off an unsynced
// epoch-0 clock and show/store 1970 dates. g_time_synced + isTimeSynced()
// let every one of those call sites tell the difference, and
// retryTimeSyncIfNeeded() keeps retrying from loop() (non-blocking) until it
// actually succeeds instead of giving up forever after one 10s window.
// ============================================================================

#include "time.h"
#include "parameters.h"

// 2024-01-01 00:00:00 UTC. Any clock reading below this is still unsynced
// (near boot epoch 0), never a legitimately old-but-synced date.
#define TIME_SYNC_SANITY_EPOCH 1704067200UL

bool g_time_synced = false;

bool isTimeSynced() {
  return time(nullptr) > (time_t)TIME_SYNC_SANITY_EPOCH;
}

// Blocks up to 10s — called once from setup(), same as before.
void configureTime() {
  configTzTime(TIMEZONE, "pool.ntp.org", "time.nist.gov");
  time_t now = time(nullptr);
  unsigned long t0 = millis();
  while (now < 24 * 3600 && millis() - t0 < 10000) { delay(250); now = time(nullptr); }
  g_time_synced = isTimeSynced();
  Serial.println(g_time_synced ? "[TIME] NTP synced" : "[TIME] NTP sync failed — will keep retrying in background");
}

// Non-blocking — call every loop() iteration. While unsynced, re-kicks SNTP
// every 30s so a boot-time NTP failure (e.g. router briefly down) recovers
// on its own once WiFi/internet comes back, instead of being stuck at 1970
// for the rest of the uptime.
void retryTimeSyncIfNeeded() {
  if (g_time_synced) return;
  if (isTimeSynced()) { g_time_synced = true; Serial.println("[TIME] NTP synced (background)"); return; }

  static unsigned long last_retry_ms = 0;
  const unsigned long RETRY_INTERVAL_MS = 30000;
  if (millis() - last_retry_ms < RETRY_INTERVAL_MS) return;
  last_retry_ms = millis();

  if (WiFi.status() != WL_CONNECTED) return;
  Serial.println("[TIME] Still unsynced — re-kicking NTP");
  configTzTime(TIMEZONE, "pool.ntp.org", "time.nist.gov");
}

// Formats "YYYY-MM-DD HH:MM:SS" (local time) into buf. Returns false (buf
// left untouched) if the clock hasn't synced yet, so callers can avoid
// persisting a bogus 1970 timestamp to Firestore.
bool formatTimestampIfSynced(char* buf, size_t len) {
  if (!isTimeSynced()) return false;
  time_t now = time(nullptr);
  strftime(buf, len, "%Y-%m-%d %H:%M:%S", localtime(&now));
  return true;
}
