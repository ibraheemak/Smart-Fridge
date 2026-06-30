#pragma once

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "parameters.h"

// ============================================================================
// ESP-NOW link to the CAM boards — one-way, CH sends commands, no CAM board
// replies. Replaces the old 2-wire UART link (see git history / uart_link.h)
// now that the hall sensor lives on this board.
//
// No wiring needed — rides on the same WiFi radio all boards already use for
// Firestore. ESP-NOW is L2 and works peer-to-peer with no router/internet
// required, BUT all radios must sit on the same channel. Relying on "join
// the same router so we end up on the same channel" breaks exactly when
// there's no internet (the channel drifts while disconnected/reconnecting),
// which defeats the point — door-close scans must still trigger with WiFi
// down. So we pin the radio to ESPNOW_CHANNEL right away; if/when the router
// connection succeeds the ESP32 WiFi stack will switch to the AP's channel
// for as long as it's associated (required to talk to it at all), and
// ESP-NOW keeps working via peer.channel = 0 ("current channel") either way.
//
// Broadcast to FF:FF:FF:FF:FF:FF so every CAM board (any roof, any count)
// receives the trigger with zero per-board MAC configuration on this side.
// ============================================================================

static uint8_t ESPNOW_BROADCAST_ADDR[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void initEspNowLink() {
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESPNOW] init failed");
    return;
  }

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, ESPNOW_BROADCAST_ADDR, 6);
  peer.channel = 0;   // track whatever channel the radio is currently on
  peer.encrypt = false;
  if (!esp_now_is_peer_exist(ESPNOW_BROADCAST_ADDR)) {
    esp_now_add_peer(&peer);
  }

  Serial.printf("[ESPNOW] link ready -> broadcast (channel %d)\n", ESPNOW_CHANNEL);
}

void espnowSendScanTrigger() {
  const char *msg = "SCAN_TRIGGER";
  esp_err_t result = esp_now_send(ESPNOW_BROADCAST_ADDR, (const uint8_t *)msg, strlen(msg));
  Serial.printf("[ESPNOW] >> SCAN_TRIGGER broadcast (%s)\n", result == ESP_OK ? "ok" : "failed");
}
