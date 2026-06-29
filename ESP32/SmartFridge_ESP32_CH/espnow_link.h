#pragma once

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "parameters.h"

// ============================================================================
// ESP-NOW link to the CAM board — one-way, CH sends commands, CAM doesn't
// reply. Replaces the old 2-wire UART link (see git history / uart_link.h)
// now that the hall sensor lives on this board.
//
// No wiring needed — rides on the same WiFi radio both boards already use
// for Firestore. ESP-NOW is L2 and works peer-to-peer with no router/internet
// required, BUT both radios must sit on the same channel. Relying on "join
// the same router so we end up on the same channel" breaks exactly when
// there's no internet (the channel drifts while disconnected/reconnecting),
// which defeats the point — door-close scans must still trigger with WiFi
// down. So we pin the radio to ESPNOW_CHANNEL right away; if/when the router
// connection succeeds the ESP32 WiFi stack will switch to the AP's channel
// for as long as it's associated (required to talk to it at all), and
// ESP-NOW keeps working via peer.channel = 0 ("current channel") either way.
//
// Sent directly to the CAM's MAC address (CAM_MAC_ADDR in parameters.h) —
// flash ESP32/SmartFridge_ESP32_GetMac/ onto the CAM to read it.
// ============================================================================

void initEspNowLink() {
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESPNOW] init failed");
    return;
  }

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, CAM_MAC_ADDR, 6);
  peer.channel = 0;   // track whatever channel the radio is currently on
  peer.encrypt = false;
  if (!esp_now_is_peer_exist(CAM_MAC_ADDR)) {
    esp_now_add_peer(&peer);
  }

  Serial.printf("[ESPNOW] link ready -> CAM %02X:%02X:%02X:%02X:%02X:%02X (channel %d)\n",
                CAM_MAC_ADDR[0], CAM_MAC_ADDR[1], CAM_MAC_ADDR[2],
                CAM_MAC_ADDR[3], CAM_MAC_ADDR[4], CAM_MAC_ADDR[5], ESPNOW_CHANNEL);
}

void espnowSendScanTrigger() {
  const char *msg = "SCAN_TRIGGER";
  esp_err_t result = esp_now_send(CAM_MAC_ADDR, (const uint8_t *)msg, strlen(msg));
  Serial.printf("[ESPNOW] >> SCAN_TRIGGER sent (%s)\n", result == ESP_OK ? "ok" : "failed");
}
