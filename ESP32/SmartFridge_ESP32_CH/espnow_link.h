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

// ----------------------------------------------------------------------------
// Inventory-changed receive — pushed by any CAM board right after it saves a
// fresh roof doc, as "INV:<roof>". Primary trigger for re-merging/re-fetching
// Firestore: it's more reliable than waiting on the RTDB SSE stream, since
// that stream only fires if the CAM board's rtdb_notify.h HTTPS PUT actually
// lands — a TLS handshake made right after the CAM's Gemini/GPT vision call,
// which can fail under heap pressure even with plenty of total free heap
// (mbedTLS needs one large contiguous block). ESP-NOW needs neither TLS nor a
// large contiguous buffer, so it isn't subject to that failure mode. The
// which-roof number isn't currently used (a full re-merge covers all roofs
// anyway) but is included for future per-roof diagnostics.
// ----------------------------------------------------------------------------
static volatile bool g_espnow_inventory_pending = false;

// ----------------------------------------------------------------------------
// Live View — CAM boards reply to a "LIVE_CAPTURE" request with a JPEG
// snapshot, split into chunks small enough for a single ESP-NOW packet
// (legacy ESP-NOW payload limit is ~250 bytes). Chunks are disambiguated from
// the ASCII "TEMP:"/"SCAN_TRIGGER" strings by a non-printable leading magic
// byte, since none of those strings can start with it.
// ----------------------------------------------------------------------------
#pragma pack(push, 1)
struct LiveViewChunkHdr {
  uint8_t  magic;        // LIVEVIEW_CHUNK_MAGIC
  uint8_t  roof;         // 1-based roof index this frame came from
  uint16_t chunkIndex;   // 0..totalChunks-1
  uint16_t totalChunks;
  uint16_t totalSize;    // full JPEG size in bytes (same value in every chunk)
};
#pragma pack(pop)

#define LIVEVIEW_CHUNK_MAGIC    0x01
#define LIVEVIEW_CHUNK_PAYLOAD  200                 // JPEG bytes per ESP-NOW packet
#define LIVEVIEW_MAX_JPEG_BYTES (40 * 1024)          // reject anything larger (malloc guard)
#define LIVEVIEW_MAX_CHUNKS     (LIVEVIEW_MAX_JPEG_BYTES / LIVEVIEW_CHUNK_PAYLOAD + 1)

static uint8_t* g_lv_rx_buf          = nullptr;
static size_t   g_lv_rx_size         = 0;
static uint16_t g_lv_rx_total_chunks = 0;
static uint16_t g_lv_rx_received     = 0;
static uint8_t  g_lv_rx_roof         = 0;
static bool     g_lv_rx_seen[LIVEVIEW_MAX_CHUNKS];
static volatile bool g_lv_rx_active    = false;   // a transfer is in progress
static volatile bool g_lv_rx_ready     = false;   // a transfer just completed
static volatile uint32_t g_lv_rx_last_chunk_ms = 0;

// Runs on the WiFi/LWIP task — only copies into the reassembly buffer, never
// touches the TFT (matches the pending-flag pattern used for TEMP: above).
void onLiveViewChunk(const uint8_t *data, int len) {
  if (len < (int)sizeof(LiveViewChunkHdr)) return;
  LiveViewChunkHdr hdr;
  memcpy(&hdr, data, sizeof(hdr));
  const uint8_t *payload = data + sizeof(hdr);
  int payloadLen = len - (int)sizeof(hdr);

  if (hdr.totalSize == 0 || hdr.totalSize > LIVEVIEW_MAX_JPEG_BYTES) return;
  if (hdr.totalChunks == 0 || hdr.totalChunks > LIVEVIEW_MAX_CHUNKS) return;
  if (hdr.chunkIndex >= hdr.totalChunks) return;

  // First chunk of a new transfer (re)starts reassembly, discarding whatever
  // partial buffer (if any) was in flight for a previous request.
  if (hdr.chunkIndex == 0) {
    if (g_lv_rx_buf) { free(g_lv_rx_buf); g_lv_rx_buf = nullptr; }
    g_lv_rx_buf = (uint8_t*)malloc(hdr.totalSize);
    if (!g_lv_rx_buf) { g_lv_rx_active = false; return; }
    g_lv_rx_size         = hdr.totalSize;
    g_lv_rx_total_chunks = hdr.totalChunks;
    g_lv_rx_received     = 0;
    g_lv_rx_roof         = hdr.roof;
    memset(g_lv_rx_seen, 0, sizeof(g_lv_rx_seen));
    g_lv_rx_active = true;
    g_lv_rx_ready  = false;
  }

  if (!g_lv_rx_active || !g_lv_rx_buf) return;
  if (hdr.totalChunks != g_lv_rx_total_chunks || hdr.roof != g_lv_rx_roof) return;  // stale packet from a prior transfer
  if (g_lv_rx_seen[hdr.chunkIndex]) { g_lv_rx_last_chunk_ms = millis(); return; }    // duplicate

  size_t offset = (size_t)hdr.chunkIndex * LIVEVIEW_CHUNK_PAYLOAD;
  size_t maxLen = g_lv_rx_size - offset;
  size_t copyLen = min((size_t)payloadLen, maxLen);
  memcpy(g_lv_rx_buf + offset, payload, copyLen);
  g_lv_rx_seen[hdr.chunkIndex] = true;
  g_lv_rx_received++;
  g_lv_rx_last_chunk_ms = millis();

  if (g_lv_rx_received >= g_lv_rx_total_chunks) {
    g_lv_rx_active = false;
    g_lv_rx_ready  = true;
  }
}

void onEspNowDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len >= 1 && data[0] == LIVEVIEW_CHUNK_MAGIC) { onLiveViewChunk(data, len); return; }

  if (len >= 4 && memcmp(data, "INV:", 4) == 0) {
    g_espnow_inventory_pending = true;
    return;
  }

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

// Returns true exactly once when a full JPEG frame has been reassembled,
// handing ownership of the malloc'd buffer to the caller (must free() it).
bool liveViewFrameReady(uint8_t &roof, uint8_t** jpegBuf, size_t* jpegLen) {
  if (!g_lv_rx_ready) return false;
  g_lv_rx_ready = false;
  roof     = g_lv_rx_roof;
  *jpegBuf = g_lv_rx_buf;
  *jpegLen = g_lv_rx_size;
  g_lv_rx_buf = nullptr;   // ownership transferred
  return true;
}

// Returns true exactly once if a transfer was in progress and stalled past
// LIVEVIEW_TIMEOUT_MS — frees the partial buffer so a stuck transfer can't
// wedge the next request.
bool liveViewTransferTimedOut() {
  if (!g_lv_rx_active) return false;
  if (millis() - g_lv_rx_last_chunk_ms < LIVEVIEW_TIMEOUT_MS) return false;
  g_lv_rx_active = false;
  if (g_lv_rx_buf) { free(g_lv_rx_buf); g_lv_rx_buf = nullptr; }
  return true;
}

// Returns true exactly once when a fresh temperature reading arrives.
bool espnowTemperatureReceived(float &tempC, float &humidity) {
  if (!g_espnow_temp_pending) return false;
  g_espnow_temp_pending = false;
  tempC = g_espnow_temp_c;
  humidity = g_espnow_humidity;
  return true;
}

// Returns true exactly once when a CAM board reports a fresh roof-doc write.
bool espnowInventoryUpdated() {
  if (!g_espnow_inventory_pending) return false;
  g_espnow_inventory_pending = false;
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

// Requests a Live View snapshot from a single roof (1-based), unlike
// espnowSendScanTrigger() which fans out to every roof — Live View only
// looks at one roof at a time.
void espnowSendLiveViewRequest(int roofIndex1Based) {
  if (roofIndex1Based < 1 || roofIndex1Based > NUM_ROOFS) return;
  const char *msg = "LIVE_CAPTURE";
  esp_err_t result = esp_now_send(CAM_MAC_ADDRS[roofIndex1Based - 1], (const uint8_t *)msg, strlen(msg));
  Serial.printf("[ESPNOW] >> LIVE_CAPTURE -> roof%d (%s)\n", roofIndex1Based, result == ESP_OK ? "ok" : "failed");
}
