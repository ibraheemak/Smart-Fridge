#pragma once

// Offline SPIFFS buffer — saves captured JPEGs to flash when WiFi is absent,
// then replays them through the normal Gemini → Firestore pipeline once WiFi
// comes back.  Nothing else in the sketch needs to change: captureAndProcess()
// calls savePhotoOffline() instead of sendToGemini() when there is no WiFi,
// and the loop() calls replayOfflinePhotos() whenever WiFi is connected.

#include "SPIFFS.h"
#include "esp_camera.h"

// Only the most recent photo is kept — a new capture replaces whatever was
// buffered before, so there is never more than one queued at a time.
#define OFFLINE_DIR          "/offline"
#define OFFLINE_EXT          ".jpg"
#define OFFLINE_PATH         OFFLINE_DIR "/latest" OFFLINE_EXT

static bool spiffs_ready = false;

bool initOfflineBuffer() {
    if (!SPIFFS.begin(true)) {
        Serial.println("[OFFLINE] SPIFFS mount failed");
        return false;
    }
    spiffs_ready = true;
    Serial.printf("[OFFLINE] SPIFFS ready — %u KB used / %u KB total\n",
                  (unsigned)(SPIFFS.usedBytes() / 1024),
                  (unsigned)(SPIFFS.totalBytes() / 1024));
    return true;
}

// Returns true if a buffered photo is waiting.
bool offlinePhotoPending() {
    return spiffs_ready && SPIFFS.exists(OFFLINE_PATH);
}

// Save a raw JPEG buffer to SPIFFS, replacing whatever was buffered before.
// Returns true on success.
bool savePhotoOffline(const uint8_t* data, size_t size) {
    if (!spiffs_ready) return false;

    if (SPIFFS.exists(OFFLINE_PATH)) SPIFFS.remove(OFFLINE_PATH);

    File f = SPIFFS.open(OFFLINE_PATH, FILE_WRITE);
    if (!f) {
        Serial.printf("[OFFLINE] Cannot open %s for write\n", OFFLINE_PATH);
        return false;
    }
    size_t written = f.write(data, size);
    f.close();
    if (written != size) {
        Serial.printf("[OFFLINE] Write incomplete (%u/%u) — removing\n",
                      (unsigned)written, (unsigned)size);
        SPIFFS.remove(OFFLINE_PATH);
        return false;
    }
    Serial.printf("[OFFLINE] Saved %s (%u bytes)\n", OFFLINE_PATH, (unsigned)size);
    return true;
}

// Called from loop() when WiFi is connected.  Sends the buffered photo
// through Gemini → Firestore exactly as a live scan would, then deletes it.
// Returns true if the photo was replayed.
//
// Forward-declares the functions it needs from gemini.h / firebase.h so
// this file can be included before them.
bool   detectItemsFromPhoto(uint8_t*, size_t, const String&, JsonDocument&);
String fetchBasicItems();
bool   saveToFirebase(JsonDocument&);
bool   saveScanHistory(JsonDocument&);

bool replayOfflinePhotos() {
    if (!offlinePhotoPending()) return false;

    File f = SPIFFS.open(OFFLINE_PATH, FILE_READ);
    if (!f) return false;
    size_t size = f.size();
    Serial.printf("[OFFLINE] WiFi restored — replaying %s (%u bytes)\n", OFFLINE_PATH, (unsigned)size);

    uint8_t* buf = (uint8_t*)malloc(size);
    if (!buf) {
        Serial.println("[OFFLINE] malloc failed — keeping photo for next retry");
        f.close();
        return false;
    }
    f.read(buf, size);
    f.close();

    String basic_items = fetchBasicItems();
    StaticJsonDocument<2048> detected_items;
    bool ok = detectItemsFromPhoto(buf, size, basic_items, detected_items);
    free(buf);

    if (!ok) {
        Serial.println("[OFFLINE] No usable AI response — keeping photo for next retry");
        return false;
    }

    saveToFirebase(detected_items);
    saveScanHistory(detected_items);
    Serial.printf("[OFFLINE] Replayed OK — deleting %s\n", OFFLINE_PATH);
    SPIFFS.remove(OFFLINE_PATH);
    return true;
}
