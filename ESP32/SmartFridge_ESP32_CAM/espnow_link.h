#pragma once

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "parameters.h"

// ============================================================================
// ESP-NOW link to the CH board — one-way, CH sends commands, this board
// doesn't reply. Replaces the old 2-wire UART link (see git history /
// uart_link.h). No wiring needed — see espnow_link.h on the CH board for why
// the channel is pinned via ESPNOW_CHANNEL instead of relying on the router.
// ============================================================================

static volatile bool g_espnow_scan_trigger_pending = false;

void onEspNowDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len == (int)strlen("SCAN_TRIGGER") && memcmp(data, "SCAN_TRIGGER", len) == 0) {
    g_espnow_scan_trigger_pending = true;
  }
}

void initEspNowLink() {
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESPNOW] init failed");
    return;
  }

  esp_now_register_recv_cb(onEspNowDataRecv);
  Serial.printf("[ESPNOW] link ready — listening (channel %d)\n", ESPNOW_CHANNEL);
}

// Returns true exactly once when a SCAN_TRIGGER command arrives.
bool espnowScanTriggerReceived() {
  if (!g_espnow_scan_trigger_pending) return false;
  g_espnow_scan_trigger_pending = false;
  return true;
}
