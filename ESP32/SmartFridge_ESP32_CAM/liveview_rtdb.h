#pragma once

// ============================================================================
// Realtime Database SSE stream — Flutter app's Live View trigger
// ============================================================================
// Mirrors the CH board's rtdb_stream.h almost exactly (same long-lived SSE
// connection, same non-blocking poll/reconnect/dedup shape) but watches this
// board's own roof-specific request path instead of inventory_meta/updated_at:
//
//   fridges/{FRIDGE_ID}/liveview_requests/roof{CAMERA_ROOF}/requested_at
//
// The Flutter app writes {".sv": "timestamp"} there (see
// FridgeService.requestLiveViewSnapshot() in the app) when the user opens or
// refreshes Live View for this roof. liveViewRtdbStreamPoll() is non-blocking
// and must be called every loop() iteration; it returns true exactly once per
// genuine request, telling the caller to capture and upload a fresh snapshot
// right now (see capturePhotoForLiveView() / saveLiveViewPhotoToFirestore()).
//
// This is a second long-lived connection alongside the ESP-NOW Live View path
// (which needs no such thing — CH talks to this board directly) — every other
// Firestore call in this codebase opens/closes a short-lived WiFiClientSecure
// per request, so this file-scope client is deliberately the exception, same
// as on the CH board.
// ============================================================================

#include <WiFiClientSecure.h>
#include "parameters.h"
#include "SECRETS.h"

#define LIVEVIEW_RTDB_RETRY_MS    5000   // how often to retry connecting while down
#define LIVEVIEW_RTDB_TIMEOUT_MS 60000   // no bytes at all for this long -> assume dead

static WiFiClientSecure g_lv_rtdb_client;
static bool             g_lv_rtdb_connected     = false;
static unsigned long    g_lv_rtdb_last_retry_ms = 0;
static unsigned long    g_lv_rtdb_last_rx_ms    = 0;
static String           g_lv_rtdb_line_buf;
static String           g_lv_rtdb_last_event;      // most recent "event:" line seen
static String           g_lv_rtdb_last_value;      // last "data:" payload we actually acted on

void initLiveViewRtdbStream() {
  g_lv_rtdb_connected = false;
  g_lv_rtdb_last_retry_ms = 0;  // let the first loop() iteration connect immediately
}

static void liveViewRtdbDisconnect() {
  g_lv_rtdb_client.stop();
  g_lv_rtdb_connected = false;
  g_lv_rtdb_line_buf = "";
  g_lv_rtdb_last_event = "";
}

static bool liveViewRtdbConnect() {
  String url = String(FIREBASE_DATABASE_URL);
  int host_start = url.indexOf("://") + 3;
  int path_start = url.indexOf('/', host_start);
  String host = (path_start < 0) ? url.substring(host_start) : url.substring(host_start, path_start);

  g_lv_rtdb_client.setInsecure();
  g_lv_rtdb_client.setConnectionTimeout(5000);
  g_lv_rtdb_client.setHandshakeTimeout(5);  // seconds
  if (!g_lv_rtdb_client.connect(host.c_str(), 443)) {
    Serial.println("[LIVEVIEW-RTDB] stream connect failed");
    return false;
  }

  String path = "/fridges/" + String(FRIDGE_ID) + "/liveview_requests/" + INVENTORY_DOC_ID + "/requested_at.json";
  g_lv_rtdb_client.print(
    "GET " + path + " HTTP/1.1\r\n" +
    "Host: " + host + "\r\n" +
    "Accept: text/event-stream\r\n" +
    "Connection: keep-alive\r\n\r\n"
  );

  Serial.println("[LIVEVIEW-RTDB] stream connecting...");
  return true;
}

static bool liveViewRtdbHandleLine(const String& line) {
  g_lv_rtdb_last_rx_ms = millis();

  if (line.startsWith("event: ")) {
    g_lv_rtdb_last_event = line.substring(7);
    return false;
  }
  if (!line.startsWith("data: ")) return false;

  String data = line.substring(6);

  if (g_lv_rtdb_last_event == "keep-alive") return false;
  if (g_lv_rtdb_last_event != "put") return false;

  // Ignore the initial snapshot RTDB sends right after connecting if it's
  // identical to what we last acted on — otherwise every reconnect would
  // spuriously trigger another capture.
  if (data == g_lv_rtdb_last_value) return false;
  g_lv_rtdb_last_value = data;
  return true;
}

// Non-blocking. Returns true exactly once per genuine Live View request.
bool liveViewRtdbStreamPoll() {
  if (WiFi.status() != WL_CONNECTED) {
    if (g_lv_rtdb_connected) liveViewRtdbDisconnect();
    return false;
  }

  if (!g_lv_rtdb_connected) {
    if (millis() - g_lv_rtdb_last_retry_ms < LIVEVIEW_RTDB_RETRY_MS) return false;
    g_lv_rtdb_last_retry_ms = millis();
    if (!liveViewRtdbConnect()) return false;
    g_lv_rtdb_connected = true;
    g_lv_rtdb_last_rx_ms = millis();
    return false;
  }

  if (!g_lv_rtdb_client.connected()) {
    Serial.println("[LIVEVIEW-RTDB] stream dropped — will reconnect");
    liveViewRtdbDisconnect();
    return false;
  }

  if (millis() - g_lv_rtdb_last_rx_ms > LIVEVIEW_RTDB_TIMEOUT_MS) {
    Serial.println("[LIVEVIEW-RTDB] stream stalled (no data/keep-alive) — reconnecting");
    liveViewRtdbDisconnect();
    return false;
  }

  bool changed = false;
  while (g_lv_rtdb_client.available()) {
    char c = g_lv_rtdb_client.read();
    if (c == '\n') {
      String line = g_lv_rtdb_line_buf;
      g_lv_rtdb_line_buf = "";
      if (line.endsWith("\r")) line.remove(line.length() - 1);
      if (liveViewRtdbHandleLine(line)) changed = true;
    } else {
      g_lv_rtdb_line_buf += c;
    }
  }

  return changed;
}
