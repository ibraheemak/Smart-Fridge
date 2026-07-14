#pragma once

// ============================================================================
// Realtime Database SSE stream — replaces the old 15s Firestore poll
// ============================================================================
// Keeps one long-lived HTTPS connection open to
// fridges/{FRIDGE_ID}/inventory_meta/updated_at.json with
// "Accept: text/event-stream". RTDB pushes an "event: put" / "data: ..."
// frame the instant that value changes (bumped by rtdb_notify.h whenever
// Firestore inventory data is written), plus periodic "event: keep-alive"
// frames while nothing changes.
//
// The connect+read loop runs on its own FreeRTOS task pinned to core 0
// (Arduino loop() runs on core 1) instead of inline in loop(), because
// WiFiClientSecure::connect() is a genuinely blocking call — TCP connect
// plus TLS handshake can take seconds, and a failed/unreachable RTDB host
// used to block loop() (and therefore handleTouch()) for that long on every
// retry, which made the touchscreen appear dead whenever "[RTDB] stream
// connect failed" was being logged. Running it on its own task means it can
// block as long as it wants without ever stalling touch polling.
//
// rtdbStreamPoll() is called every loop() iteration; it's just a non-blocking
// check of a flag the task sets, and returns true exactly once per genuine
// change, telling the caller to re-fetch Firestore right now instead of
// waiting on a timer.
// ============================================================================

#include <WiFiClientSecure.h>
#include "parameters.h"
#include "SECRETS.h"

#define RTDB_STREAM_RETRY_MS   5000   // how often to retry connecting while down
#define RTDB_STREAM_TIMEOUT_MS 60000  // no bytes at all for this long -> assume dead

static WiFiClientSecure     g_rtdb_client;
static bool                 g_rtdb_connected     = false;
static unsigned long        g_rtdb_last_retry_ms = 0;
static unsigned long        g_rtdb_last_rx_ms    = 0;
static String               g_rtdb_line_buf;
static String               g_rtdb_last_event;      // most recent "event:" line seen
static String               g_rtdb_last_value;      // last "data:" payload we actually acted on
static volatile bool        g_rtdb_change_flag  = false;  // set by the task, drained by rtdbStreamPoll()
static TaskHandle_t         g_rtdb_task_handle  = nullptr;

static void rtdbStreamTask(void* pvParameters);

void initRtdbStream() {
  g_rtdb_connected = false;
  g_rtdb_last_retry_ms = 0;  // let the task's first pass connect immediately
  xTaskCreatePinnedToCore(rtdbStreamTask, "rtdb_stream", 10240, nullptr, 1,
                          &g_rtdb_task_handle, 0);
}

static void rtdbStreamDisconnect() {
  g_rtdb_client.stop();
  g_rtdb_connected = false;
  g_rtdb_line_buf = "";
  g_rtdb_last_event = "";
}

static bool rtdbStreamConnect() {
  // FIREBASE_DATABASE_URL is just "https://<host>" — split host out for the
  // raw WiFiClientSecure connect (HTTPClient isn't used here since it doesn't
  // support leaving the connection open past a single request/response).
  String url = String(FIREBASE_DATABASE_URL);
  int host_start = url.indexOf("://") + 3;
  int path_start = url.indexOf('/', host_start);
  String host = (path_start < 0) ? url.substring(host_start) : url.substring(host_start, path_start);

  g_rtdb_client.setInsecure();
  // Default WiFiClientSecure timeouts are far too long for something running
  // synchronously in loop() alongside touch polling: TCP connect defaults to
  // 30s, and the TLS handshake defaults to a full 120s (note: plain
  // setTimeout() only bounds later stream reads, not connect()/handshake —
  // these are the two calls that actually govern how long connect() below
  // can block). Cap both so a stalled/bad connection attempt can't freeze
  // the UI for minutes.
  g_rtdb_client.setConnectionTimeout(5000);
  g_rtdb_client.setHandshakeTimeout(5);  // seconds
  if (!g_rtdb_client.connect(host.c_str(), 443)) {
    Serial.println("[RTDB] stream connect failed");
    return false;
  }

  String path = "/fridges/" + String(FRIDGE_ID) + "/inventory_meta/updated_at.json";
  g_rtdb_client.print(
    "GET " + path + " HTTP/1.1\r\n" +
    "Host: " + host + "\r\n" +
    "Accept: text/event-stream\r\n" +
    "Connection: keep-alive\r\n\r\n"
  );

  Serial.println("[RTDB] stream connecting...");
  return true;
}

// Handles one complete line of the SSE response (HTTP header lines, blank
// separator lines, or "event: ..." / "data: ..." frame lines — RTDB doesn't
// distinguish them for us, so we just track the most recent "event:" seen).
static bool rtdbHandleLine(const String& line) {
  g_rtdb_last_rx_ms = millis();

  if (line.startsWith("event: ")) {
    g_rtdb_last_event = line.substring(7);
    return false;
  }
  if (!line.startsWith("data: ")) return false;

  String data = line.substring(6);

  if (g_rtdb_last_event == "keep-alive") return false;
  if (g_rtdb_last_event != "put") return false;

  // Ignore the initial snapshot RTDB sends right after connecting (a "put" of
  // whatever the value already was) if it's identical to what we last acted
  // on — otherwise every reconnect would spuriously trigger a refetch.
  if (data == g_rtdb_last_value) return false;
  g_rtdb_last_value = data;
  return true;
}

// Runs entirely on its own task/core — free to block on connect()/TLS
// handshake without ever stalling loop() or touch polling.
static void rtdbStreamTask(void* pvParameters) {
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      if (g_rtdb_connected) rtdbStreamDisconnect();
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }

    if (!g_rtdb_connected) {
      if (millis() - g_rtdb_last_retry_ms < RTDB_STREAM_RETRY_MS) {
        vTaskDelay(pdMS_TO_TICKS(100));
        continue;
      }
      g_rtdb_last_retry_ms = millis();
      if (!rtdbStreamConnect()) {
        vTaskDelay(pdMS_TO_TICKS(100));
        continue;
      }
      g_rtdb_connected = true;
      g_rtdb_last_rx_ms = millis();
      continue;
    }

    if (!g_rtdb_client.connected()) {
      Serial.println("[RTDB] stream dropped — will reconnect");
      rtdbStreamDisconnect();
      continue;
    }

    if (millis() - g_rtdb_last_rx_ms > RTDB_STREAM_TIMEOUT_MS) {
      Serial.println("[RTDB] stream stalled (no data/keep-alive) — reconnecting");
      rtdbStreamDisconnect();
      continue;
    }

    bool changed = false;
    while (g_rtdb_client.available()) {
      char c = g_rtdb_client.read();
      if (c == '\n') {
        String line = g_rtdb_line_buf;
        g_rtdb_line_buf = "";
        if (line.endsWith("\r")) line.remove(line.length() - 1);
        if (rtdbHandleLine(line)) changed = true;
      } else {
        g_rtdb_line_buf += c;
      }
    }
    if (changed) g_rtdb_change_flag = true;

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// Non-blocking. Returns true exactly once per genuine inventory-changed event.
bool rtdbStreamPoll() {
  if (!g_rtdb_change_flag) return false;
  g_rtdb_change_flag = false;
  return true;
}
