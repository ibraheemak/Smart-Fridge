#pragma once

// ============================================================================
// Realtime Database "inventory changed" doorbell
// ============================================================================
// All inventory data still lives in Firestore (see firebase.h). RTDB holds
// nothing but a single server-timestamped value at
// fridges/{FRIDGE_ID}/inventory_meta/updated_at, bumped after every write
// that changes inventory — the CH board keeps an SSE stream open on that
// path (see rtdb_stream.h on the CH board) so it can re-fetch Firestore the
// instant something changes instead of polling on a timer.
//
// Best-effort with a couple of retries: this call always lands right after
// the Gemini/GPT vision request, whose large base64 photo buffer leaves the
// heap fragmented enough that the TLS handshake to the RTDB host can fail
// outright (HTTPClient error -1, connection refused) even though the
// Firestore write just above it succeeded fine. A single short delay is
// usually enough for the heap/TLS session pool to recover. If every retry
// still fails, this is genuinely non-fatal: a later write's bump (or the
// next unrelated inventory change) will eventually wake the stream up
// anyway.
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
      char err_buf[128] = {0};
      if (code < 0) client.lastError(err_buf, sizeof(err_buf));
      http.end();
      Serial.printf("[RTDB] notify -> %d \"%s\" (attempt %d/%d, heap %u, maxAlloc %u)\n",
                    code, err_buf, attempt, RTDB_NOTIFY_MAX_ATTEMPTS,
                    (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
      if (code == 200) return;
    }
    if (attempt < RTDB_NOTIFY_MAX_ATTEMPTS) delay(RTDB_NOTIFY_RETRY_DELAY_MS);
  }
}
