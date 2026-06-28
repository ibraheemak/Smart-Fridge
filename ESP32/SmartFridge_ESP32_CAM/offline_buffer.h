#pragma once

// Offline SPIFFS buffer — saves captured JPEGs to flash when WiFi is absent,
// then replays them through the normal Gemini → Firestore pipeline once WiFi
// comes back.  Nothing else in the sketch needs to change: captureAndProcess()
// calls savePhotoOffline() instead of sendToGemini() when there is no WiFi,
// and the loop() calls replayOfflinePhotos() whenever WiFi is connected.

#include "SPIFFS.h"
#include "esp_camera.h"

// Maximum number of photos to queue.  Each JPEG is ~40-80 KB at JPEG quality
// 20, so 5 photos ≈ 200-400 KB — well within the 1 MB SPIFFS partition that
// the default ESP32 partition table provides.
#define OFFLINE_MAX_PHOTOS   5
#define OFFLINE_DIR          "/offline"
#define OFFLINE_EXT          ".jpg"

static bool spiffs_ready = false;

bool initOfflineBuffer() {
    if (!SPIFFS.begin(true)) {
        Serial.println("[OFFLINE] SPIFFS mount failed");
        return false;
    }
    if (!SPIFFS.exists(OFFLINE_DIR)) {
        // SPIFFS has no real directories — the "directory" is just a path
        // prefix, so we only need to create it if it doesn't exist as a file.
    }
    spiffs_ready = true;
    Serial.printf("[OFFLINE] SPIFFS ready — %u KB used / %u KB total\n",
                  (unsigned)(SPIFFS.usedBytes() / 1024),
                  (unsigned)(SPIFFS.totalBytes() / 1024));
    return true;
}

// Returns how many buffered photos are waiting.
int offlinePhotoCount() {
    if (!spiffs_ready) return 0;
    int count = 0;
    File root = SPIFFS.open(OFFLINE_DIR);
    if (!root || !root.isDirectory()) return 0;
    File f = root.openNextFile();
    while (f) { count++; f = root.openNextFile(); }
    return count;
}

// Save a raw JPEG buffer to SPIFFS.  Returns true on success.
bool savePhotoOffline(const uint8_t* data, size_t size) {
    if (!spiffs_ready) return false;

    int count = offlinePhotoCount();
    if (count >= OFFLINE_MAX_PHOTOS) {
        Serial.printf("[OFFLINE] Buffer full (%d photos) — dropping oldest\n", count);
        // Delete the oldest file to make room.
        File root = SPIFFS.open(OFFLINE_DIR);
        if (root && root.isDirectory()) {
            File oldest = root.openNextFile();
            if (oldest) {
                String path = oldest.path();
                oldest.close();
                SPIFFS.remove(path);
                Serial.printf("[OFFLINE] Deleted %s\n", path.c_str());
            }
        }
    }

    // Build a unique filename from the current millis tick (good enough for
    // ordering; a real timestamp would need NTP which isn't available offline).
    String path = String(OFFLINE_DIR) + "/" + String(millis()) + OFFLINE_EXT;
    File f = SPIFFS.open(path, FILE_WRITE);
    if (!f) {
        Serial.printf("[OFFLINE] Cannot open %s for write\n", path.c_str());
        return false;
    }
    size_t written = f.write(data, size);
    f.close();
    if (written != size) {
        Serial.printf("[OFFLINE] Write incomplete (%u/%u) — removing\n",
                      (unsigned)written, (unsigned)size);
        SPIFFS.remove(path);
        return false;
    }
    Serial.printf("[OFFLINE] Saved %s (%u bytes) — %d photo(s) queued\n",
                  path.c_str(), (unsigned)size, offlinePhotoCount());
    return true;
}

// Called from loop() when WiFi is connected.  Sends each buffered photo
// through Gemini → Firestore exactly as a live scan would, then deletes it.
// Returns true if at least one photo was replayed.
//
// Forward-declares the two functions it needs from gemini.h / firebase.h so
// this file can be included before them.
String sendToGemini(uint8_t*, size_t, const String&);
bool   parseGeminiResponse(const String&, JsonDocument&);
String fetchBasicItems();
bool   saveToFirebase(JsonDocument&);
bool   saveScanHistory(JsonDocument&);

bool replayOfflinePhotos() {
    if (!spiffs_ready) return false;
    int count = offlinePhotoCount();
    if (count == 0) return false;

    Serial.printf("[OFFLINE] WiFi restored — replaying %d buffered photo(s)\n", count);

    File root = SPIFFS.open(OFFLINE_DIR);
    if (!root || !root.isDirectory()) return false;

    bool any_ok = false;
    String basic_items = fetchBasicItems();

    File f = root.openNextFile();
    while (f) {
        String path = f.path();
        size_t size = f.size();
        Serial.printf("[OFFLINE] Replaying %s (%u bytes)\n", path.c_str(), (unsigned)size);

        uint8_t* buf = (uint8_t*)malloc(size);
        if (!buf) {
            Serial.println("[OFFLINE] malloc failed — skipping");
            f = root.openNextFile();
            continue;
        }
        f.read(buf, size);
        f.close();

        String response = sendToGemini(buf, size, basic_items);
        free(buf);

        if (response.length() == 0) {
            Serial.println("[OFFLINE] Gemini returned empty — keeping photo for next retry");
            f = root.openNextFile();
            continue;
        }

        StaticJsonDocument<2048> detected_items;
        if (!parseGeminiResponse(response, detected_items)) {
            Serial.println("[OFFLINE] Parse failed — keeping photo");
            f = root.openNextFile();
            continue;
        }

        saveToFirebase(detected_items);
        saveScanHistory(detected_items);
        Serial.printf("[OFFLINE] Replayed OK — deleting %s\n", path.c_str());
        SPIFFS.remove(path);
        any_ok = true;

        f = root.openNextFile();
    }
    return any_ok;
}
