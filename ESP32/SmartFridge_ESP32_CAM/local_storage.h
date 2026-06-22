#pragma once
// SPIFFS-based offline photo buffer.
// Saves the last captured JPEG to flash so it can be uploaded to Firebase
// Storage later if WiFi was unavailable at scan time.

#include <SPIFFS.h>

#define SPIFFS_PENDING_PHOTO  "/pending.jpg"

void initLocalStorage() {
  if (!SPIFFS.begin(true)) {   // true = format on first use
    Serial.println("[SPIFFS] Mount failed — offline buffer unavailable");
    return;
  }
  Serial.printf("[SPIFFS] Ready — free: %u / %u bytes\n",
                (unsigned)(SPIFFS.totalBytes() - SPIFFS.usedBytes()),
                (unsigned)SPIFFS.totalBytes());
}

bool savePhotoPending(const uint8_t* buf, size_t len) {
  File f = SPIFFS.open(SPIFFS_PENDING_PHOTO, "w");
  if (!f) {
    Serial.println("[SPIFFS] Cannot open for write");
    return false;
  }
  size_t written = f.write(buf, len);
  f.close();
  bool ok = (written == len);
  Serial.printf("[SPIFFS] %s %u bytes to %s\n",
                ok ? "Saved" : "PARTIAL write", (unsigned)written, SPIFFS_PENDING_PHOTO);
  return ok;
}

bool hasPendingPhoto() {
  return SPIFFS.exists(SPIFFS_PENDING_PHOTO);
}

// Returns a heap-allocated buffer containing the pending JPEG.
// Sets *len to the byte count. Caller must free() the buffer.
// Returns nullptr on failure.
uint8_t* loadPendingPhoto(size_t* len) {
  File f = SPIFFS.open(SPIFFS_PENDING_PHOTO, "r");
  if (!f) { *len = 0; return nullptr; }
  *len = f.size();
  uint8_t* buf = (uint8_t*)malloc(*len);
  if (!buf) {
    Serial.printf("[SPIFFS] malloc(%u) failed\n", (unsigned)*len);
    f.close(); *len = 0; return nullptr;
  }
  f.read(buf, *len);
  f.close();
  Serial.printf("[SPIFFS] Loaded %u bytes from %s\n", (unsigned)*len, SPIFFS_PENDING_PHOTO);
  return buf;
}

void clearPendingPhoto() {
  SPIFFS.remove(SPIFFS_PENDING_PHOTO);
  Serial.println("[SPIFFS] Pending photo cleared");
}
