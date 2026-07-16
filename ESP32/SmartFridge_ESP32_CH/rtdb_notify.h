#pragma once

// ============================================================================
// Realtime Database "inventory changed" doorbell
// ============================================================================
// All inventory data still lives in Firestore (see touch.h / gm65.h /
// inventory_merge.h). RTDB holds nothing but a single server-timestamped
// value at fridges/{FRIDGE_ID}/inventory_meta/updated_at, bumped after every
// write that changes inventory — rtdb_stream.h keeps an SSE stream open on
// that path so this board can react the instant something changes instead
// of polling on a timer.
//
// Best-effort with a couple of retries: a transient TLS/connect failure here
// (HTTPClient error codes are negative) otherwise gets silently dropped, and
// since this is the only thing that wakes rtdb_stream.h's SSE listener, a
// dropped notify means nothing re-fetches Firestore until some unrelated
// change happens to bump the same value. If every retry still fails, this
// is genuinely non-fatal: a later write's bump (or the next unrelated
// inventory change) will eventually wake the stream up anyway.
// ============================================================================

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "parameters.h"
#include "SECRETS.h"

#define RTDB_NOTIFY_MAX_ATTEMPTS 3
#define RTDB_NOTIFY_RETRY_DELAY_MS 300

inline void rtdbNotifyInventoryChanged() {
  if (WiFi.status() != WL_CONNECTED) return;

  String url = String(FIREBASE_DATABASE_URL) +
               "/fridges/" + String(FRIDGE_ID) + "/inventory_meta/updated_at.json";

  for (int attempt = 1; attempt <= RTDB_NOTIFY_MAX_ATTEMPTS; attempt++) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(5000);
    if (!http.begin(client, url)) {
      Serial.println("[RTDB] notify -> begin() failed");
    } else {
      http.addHeader("Content-Type", "application/json");
      int code = http.PUT("{\".sv\": \"timestamp\"}");
      http.end();
      Serial.printf("[RTDB] notify -> %d (attempt %d/%d, heap %u)\n",
                    code, attempt, RTDB_NOTIFY_MAX_ATTEMPTS, (unsigned)ESP.getFreeHeap());
      if (code == 200) return;
    }
    if (attempt < RTDB_NOTIFY_MAX_ATTEMPTS) delay(RTDB_NOTIFY_RETRY_DELAY_MS);
  }
}
