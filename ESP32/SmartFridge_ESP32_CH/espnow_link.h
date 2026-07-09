#pragma once

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "parameters.h"

// ============================================================================
// ESP-NOW link to the CAM boards — mostly one-way (CH sends SCAN_TRIGGER,
// CAM boards don't reply), but the roof1 CAM board also pushes DHT11
// temperature/humidity readings back here (see espnowTemperatureReceived()
// below) instead of this board polling Firestore for them. Replaces the old
// 2-wire UART link (see git history / uart_link.h) now that the hall sensor
// lives on this board.
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
// SCAN_TRIGGER is sent as a unicast to each known CAM board's MAC (see
// CAM_MAC_ADDRS in parameters.h), one send per roof — NOT a broadcast. A
// broadcast to FF:FF:FF:FF:FF:FF would need zero per-board MAC bookkeeping,
// but 802.11 broadcast frames get no MAC-layer ACK/retry: if a CAM board's
// radio was even briefly busy when the broadcast went out, it just silently
// missed the trigger with nothing to fall back on (observed: only one of
// two CAM boards would sometimes take a photo). Unicast frames get hardware
// ACK + automatic retry, so this is meaningfully more reliable at the cost
// of listing each board's MAC once below.
// ============================================================================

// ----------------------------------------------------------------------------
// Temperature receive — pushed by the roof1 CAM board as "TEMP:<c>,<h>".
// The callback runs on the WiFi/LWIP task, not the Arduino loop() task, so
// it only parses the message and stashes the result in volatile globals +
// a pending flag (same pattern CAM's espnow_link.h already uses for
// SCAN_TRIGGER) — it never touches the TFT or calls esp_now_send() itself.
// ----------------------------------------------------------------------------
static volatile bool  g_espnow_temp_pending = false;
static volatile float g_espnow_temp_c       = -1.0f;
static volatile float g_espnow_humidity     = -1.0f;

void onEspNowDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len < 5 || memcmp(data, "TEMP:", 5) != 0) return;

  char buf[32];
  int n = min(len - 5, (int)sizeof(buf) - 1);
  memcpy(buf, data + 5, n);
  buf[n] = '\0';

  float tempC, humidity;
  if (sscanf(buf, "%f,%f", &tempC, &humidity) != 2) return;

  g_espnow_temp_c   = tempC;
  g_espnow_humidity = humidity;
  g_espnow_temp_pending = true;
}

// Returns true exactly once when a fresh temperature reading arrives.
bool espnowTemperatureReceived(float &tempC, float &humidity) {
  if (!g_espnow_temp_pending) return false;
  g_espnow_temp_pending = false;
  tempC = g_espnow_temp_c;
  humidity = g_espnow_humidity;
  return true;
}

void initEspNowLink() {
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESPNOW] init failed");
    return;
  }

  esp_now_register_recv_cb(onEspNowDataRecv);

  for (int i = 0; i < NUM_ROOFS; i++) {
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, CAM_MAC_ADDRS[i], 6);
    peer.channel = 0;   // track whatever channel the radio is currently on
    peer.encrypt = false;
    if (!esp_now_is_peer_exist(CAM_MAC_ADDRS[i])) {
      esp_now_add_peer(&peer);
    }
  }

  Serial.printf("[ESPNOW] link ready -> %d known CAM board(s) (channel %d)\n", NUM_ROOFS, ESPNOW_CHANNEL);
}

// Re-pin the channel after a WiFiManager connect attempt. If it actually
// connected to the router this is a harmless no-op (the STA can't change
// channel while associated). If it fell back to hosting its own config
// portal (WIFI_AP_STA) and timed out still offline, opening that portal's
// softAP silently retuned the radio away from ESPNOW_CHANNEL — this puts it
// back so the board still matches the CAM boards while offline.
void reassertEspNowChannel() {
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  Serial.printf("[ESPNOW] channel re-asserted (now %d)\n", WiFi.channel());
}

void espnowSendScanTrigger() {
  const char *msg = "SCAN_TRIGGER";
  for (int i = 0; i < NUM_ROOFS; i++) {
    esp_err_t result = esp_now_send(CAM_MAC_ADDRS[i], (const uint8_t *)msg, strlen(msg));
    Serial.printf("[ESPNOW] >> SCAN_TRIGGER -> roof%d (%s)\n", i + 1, result == ESP_OK ? "ok" : "failed");
  }
}
