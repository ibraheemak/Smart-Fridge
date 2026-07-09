#pragma once

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "parameters.h"

// ============================================================================
// ESP-NOW link to the CH board — mostly one-way (CH sends SCAN_TRIGGER
// commands, this board doesn't reply), but CAMERA_ROOF == 1 also pushes
// DHT11 temperature/humidity readings back to CH (see
// espnowSendTemperature() below) instead of CH polling Firestore for them.
// Replaces the old 2-wire UART link (see git history / uart_link.h). No
// wiring needed — see espnow_link.h on the CH board for why the channel is
// pinned via ESPNOW_CHANNEL instead of relying on the router.
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

#if CAMERA_ROOF == 1
  esp_now_peer_info_t ch_peer = {};
  memcpy(ch_peer.peer_addr, CH_MAC_ADDR, 6);
  ch_peer.channel = 0;   // track whatever channel the radio is currently on
  ch_peer.encrypt = false;
  if (!esp_now_is_peer_exist(CH_MAC_ADDR)) {
    esp_now_add_peer(&ch_peer);
  }
#endif

  Serial.printf("[ESPNOW] link ready — listening (channel %d)\n", ESPNOW_CHANNEL);
}

// Re-pin the channel after a WiFiManager connect attempt. If it actually
// connected to the router this is a harmless no-op (the STA can't change
// channel while associated). If it fell back to hosting its own config
// portal (WIFI_AP_STA) and timed out still offline, opening that portal's
// softAP silently retuned the radio away from ESPNOW_CHANNEL — this puts it
// back so the board still matches CH/other CAM boards while offline.
void reassertEspNowChannel() {
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  Serial.printf("[ESPNOW] channel re-asserted (now %d)\n", WiFi.channel());
}

// Returns true exactly once when a SCAN_TRIGGER command arrives.
bool espnowScanTriggerReceived() {
  if (!g_espnow_scan_trigger_pending) return false;
  g_espnow_scan_trigger_pending = false;
  return true;
}

#if CAMERA_ROOF == 1
// Fire-and-forget push of a fresh DHT11 reading to the CH board's display.
// Called right after readTemperature()/saveTemperature() succeed — never
// blocks (esp_now_send() queues the frame and returns immediately), so it
// can't interfere with the DHT11 bit-banging or the SCAN_TRIGGER recv path
// above, which run independently of this send.
void espnowSendTemperature(float tempC, float humidity) {
  char msg[32];
  snprintf(msg, sizeof(msg), "TEMP:%.1f,%.1f", tempC, humidity);
  esp_err_t result = esp_now_send(CH_MAC_ADDR, (const uint8_t *)msg, strlen(msg));
  Serial.printf("[ESPNOW] >> %s -> %s\n", msg, result == ESP_OK ? "ok" : "failed");
}
#endif
