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
static volatile bool g_espnow_liveview_pending     = false;

void onEspNowDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len == (int)strlen("SCAN_TRIGGER") && memcmp(data, "SCAN_TRIGGER", len) == 0) {
    g_espnow_scan_trigger_pending = true;
    return;
  }
  if (len == (int)strlen("LIVE_CAPTURE") && memcmp(data, "LIVE_CAPTURE", len) == 0) {
    g_espnow_liveview_pending = true;
  }
}

void initEspNowLink() {
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESPNOW] init failed");
    return;
  }

  esp_now_register_recv_cb(onEspNowDataRecv);

  // Every roof needs a return path to the CH board now (Live View snapshots
  // come back from whichever roof was asked), not just roof1 — roof1 is
  // only special in that it *also* pushes DHT11 temperature (see
  // espnowSendTemperature() below, still #if CAMERA_ROOF == 1 guarded).
  esp_now_peer_info_t ch_peer = {};
  memcpy(ch_peer.peer_addr, CH_MAC_ADDR, 6);
  ch_peer.channel = 0;   // track whatever channel the radio is currently on
  ch_peer.encrypt = false;
  if (!esp_now_is_peer_exist(CH_MAC_ADDR)) {
    esp_now_add_peer(&ch_peer);
  }

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

// Returns true exactly once when a LIVE_CAPTURE command arrives.
bool espnowLiveViewRequested() {
  if (!g_espnow_liveview_pending) return false;
  g_espnow_liveview_pending = false;
  return true;
}

// ----------------------------------------------------------------------------
// Live View — sends a captured JPEG back to the CH board as a sequence of
// small chunks (see LiveViewChunkHdr on the CH board's espnow_link.h for the
// full protocol rationale: legacy ESP-NOW payload is ~250 bytes, so a frame
// that's tens of KB has to be split). Layout here must match the CH side
// exactly, byte for byte.
// ----------------------------------------------------------------------------
#pragma pack(push, 1)
struct LiveViewChunkHdr {
  uint8_t  magic;
  uint8_t  roof;
  uint16_t chunkIndex;
  uint16_t totalChunks;
  uint16_t totalSize;
};
#pragma pack(pop)

#define LIVEVIEW_CHUNK_MAGIC    0x01
#define LIVEVIEW_CHUNK_PAYLOAD  200
#define LIVEVIEW_SEND_DELAY_MS   12   // pacing between sends so ESP-NOW's TX queue doesn't overrun

void espnowSendLiveViewFrame(const uint8_t* jpeg, size_t len) {
  uint16_t totalChunks = (len + LIVEVIEW_CHUNK_PAYLOAD - 1) / LIVEVIEW_CHUNK_PAYLOAD;
  uint8_t packet[sizeof(LiveViewChunkHdr) + LIVEVIEW_CHUNK_PAYLOAD];

  for (uint16_t i = 0; i < totalChunks; i++) {
    size_t offset = (size_t)i * LIVEVIEW_CHUNK_PAYLOAD;
    size_t chunkLen = min((size_t)LIVEVIEW_CHUNK_PAYLOAD, len - offset);

    LiveViewChunkHdr hdr = { LIVEVIEW_CHUNK_MAGIC, (uint8_t)CAMERA_ROOF, i, totalChunks, (uint16_t)len };
    memcpy(packet, &hdr, sizeof(hdr));
    memcpy(packet + sizeof(hdr), jpeg + offset, chunkLen);

    esp_now_send(CH_MAC_ADDR, packet, sizeof(hdr) + chunkLen);
    delay(LIVEVIEW_SEND_DELAY_MS);
  }
  Serial.printf("[ESPNOW] >> live view frame sent (%d bytes, %d chunks)\n", (int)len, totalChunks);
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

// Fire-and-forget "I just wrote a new roof doc" signal, called right after
// saveToFirebase() succeeds — used instead of relying solely on
// rtdb_notify.h's HTTPS PUT to bump inventory_meta/updated_at. That PUT rides
// a fresh TLS handshake right after this board's Gemini/GPT vision call and
// can fail outright under heap pressure ("SSL - Memory allocation failed")
// even with plenty of *total* free heap, because mbedTLS needs one large
// contiguous block. ESP-NOW needs no TLS handshake and no contiguous buffer
// of that size, so it isn't subject to the same failure mode — see the CH
// board's espnowInventoryUpdated() for the receiving side.
void espnowSendInventoryUpdated() {
  char msg[16];
  snprintf(msg, sizeof(msg), "INV:%d", CAMERA_ROOF);
  esp_err_t result = esp_now_send(CH_MAC_ADDR, (const uint8_t *)msg, strlen(msg));
  Serial.printf("[ESPNOW] >> %s -> %s\n", msg, result == ESP_OK ? "ok" : "failed");
}
