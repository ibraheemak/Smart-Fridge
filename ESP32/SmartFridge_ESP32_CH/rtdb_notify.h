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
// Fire-and-forget: failure here is non-fatal, since a later write's bump (or
// the next unrelated inventory change) will eventually wake the stream up
// anyway.
// ============================================================================

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "parameters.h"
#include "SECRETS.h"

inline void rtdbNotifyInventoryChanged() {
  if (WiFi.status() != WL_CONNECTED) return;

  String url = String(FIREBASE_DATABASE_URL) +
               "/fridges/" + String(FRIDGE_ID) + "/inventory_meta/updated_at.json";

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(5000);
  if (!http.begin(client, url)) return;
  http.addHeader("Content-Type", "application/json");
  int code = http.PUT("{\".sv\": \"timestamp\"}");
  Serial.printf("[RTDB] notify -> %d\n", code);
  http.end();
}
