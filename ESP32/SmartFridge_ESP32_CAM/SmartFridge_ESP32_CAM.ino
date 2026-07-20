/*
 * SmartFridge ESP32-CAM
 *
 * Captures fridge photos, sends them to Gemini AI, and writes the
 * resulting inventory to Firestore. The display is handled separately
 * by the ESP32-CH board.
 *
 * File layout:
 *   camera.h   — camera init, flash, capture
 *   firebase.h — Firestore read/write
 *   gemini.h   — Gemini AI request/parse
 *   led_strip.h— WS2811 LED strip
 *   parameters.h — pin assignments and tunable constants
 *   SECRETS.h  — API keys and Firebase credentials
 */

#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "time.h"

#include "SECRETS.h"
#include "parameters.h"
#include "camera.h"
#include "firebase.h"
#include "gemini.h"
#include "led_strip.h"
#include "espnow_link.h"
#include "liveview_rtdb.h"
#include "temperature.h"
#include "offline_buffer.h"

// ============================================================================
// STATE
// ============================================================================
unsigned long lastTempReadMs = 0;
unsigned long lastWifiRetryMs = 0;
unsigned long lastTimeSyncMs = 0;
bool wasOffline = false;
String savedWifiSSID;
String savedWifiPass;

// ============================================================================
// WIFI & TIME
// ============================================================================
void checkResetButton() {
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
  if (digitalRead(RESET_BUTTON_PIN) != LOW) return;
  Serial.println("[WIFI] BOOT held — keep holding to wipe credentials...");
  unsigned long t0 = millis();
  while (digitalRead(RESET_BUTTON_PIN) == LOW) {
    if (millis() - t0 >= RESET_HOLD_MS) {
      WiFiManager wm;
      wm.resetSettings();
      Serial.println("[WIFI] Wiped, restarting...");
      delay(1000);
      ESP.restart();
    }
    delay(50);
  }
}

void initWiFi() {
  Serial.println("[WIFI] Connecting...");
  WiFiManager wm;
  wm.setConnectTimeout(20);
  wm.setConfigPortalTimeout(WIFI_PORTAL_TIMEOUT_S);
  wm.setAPCallback([](WiFiManager*) {
    Serial.printf("[WIFI] Open AP \"%s\" -> http://192.168.4.1\n", WIFI_AP_NAME);
  });
  if (!wm.autoConnect(WIFI_AP_NAME)) {
    Serial.println("[WIFI] Portal timed out — continuing offline");
  }

  // Capture whatever credentials WiFiManager has on file (saved from a
  // previous successful connection, or just entered in the portal) so the
  // retry loop can re-issue WiFi.begin() with them even if this boot never
  // actually connected (e.g. router was briefly down and the portal timed
  // out before it came back).
  savedWifiSSID = wm.getWiFiSSID();
  savedWifiPass = wm.getWiFiPass();

  WiFi.mode(WIFI_STA);  // drop the AP WiFiManager may have left running

  Serial.printf("[WIFI] Connected: %s (%s) channel %d\n",
                WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.channel());
}

void configureTime() {
  configTzTime(TIMEZONE, "pool.ntp.org", "time.nist.gov");
  unsigned long t0 = millis();
  while (!isTimeSynced() && millis() - t0 < 10000) delay(250);
  if (isTimeSynced()) {
    Serial.printf("[TIME] NTP synced: %s\n", getFormattedTimestamp().c_str());
  } else {
    // Common when the board boots faster than the router/DNS is ready. The
    // old code gave up here and ran all day on a 1970 clock; loop() now keeps
    // retrying, and timestamped writes are skipped until the clock is real.
    Serial.println("[TIME] NTP not synced yet — will keep retrying from loop()");
  }
}

// ============================================================================
// SCAN CYCLE
// ============================================================================
void captureAndProcess() {
  Serial.println("[SCAN] Starting...");
#if CAMERA_ROOF == 1
  ledStripOn();
  delay(20);
#endif

  size_t photo_size = 0;
  uint8_t* photo_data = capturePhoto(&photo_size);
#if CAMERA_ROOF == 1
  ledStripOff();
#endif

  if (!photo_data) {
    Serial.println("[SCAN] Capture failed");
    return;
  }

  bool wifiOk = (WiFi.status() == WL_CONNECTED) && (WiFi.localIP() != IPAddress(0,0,0,0));
  if (!wifiOk) {
    Serial.println("[SCAN] No WiFi — saving photo to SPIFFS for later");
    savePhotoOffline(photo_data, photo_size);
    free(photo_data);
    return;
  }

  String basic_items  = fetchBasicItems();
  String known_items  = fetchCurrentItemNames();
  StaticJsonDocument<2048> detected_items;
  bool ok = detectItemsFromPhoto(photo_data, photo_size, basic_items, known_items, detected_items);
  free(photo_data);

  if (!ok) {
    Serial.println("[SCAN] No usable AI response");
    return;
  }

#if DEBUG_MODE
  if (detected_items.containsKey("description"))
    Serial.printf("[DEBUG] %s\n", detected_items["description"].as<const char*>());
#endif

  if (saveToFirebase(detected_items)) espnowSendInventoryUpdated();
  saveScanHistory(detected_items);
  Serial.printf("[SCAN] Done — %d items written to Firestore\n",
                (int)detected_items["items"].as<JsonArray>().size());
}

// ============================================================================
// TEMPERATURE / HUMIDITY
// ============================================================================
#if CAMERA_ROOF == 1
void readAndPublishTemperature() {
  float tempC, humidity;
  if (!readTemperature(tempC, humidity)) {
    Serial.println("[TEMP] Read failed");
    return;
  }
  Serial.printf("[TEMP] %.1f C, %.1f %% RH\n", tempC, humidity);
  saveTemperature(tempC, humidity);
  espnowSendTemperature(tempC, humidity);
}
#endif

// ============================================================================
// SERIAL COMMANDS
// ============================================================================
void printHelp() {
  Serial.println("\n========================================");
  Serial.println("SmartFridge CAM — Commands");
  Serial.println("SCAN      — Capture, analyze, save to Firestore");
#if CAMERA_ROOF == 1
  Serial.println("LED ON    — LED strip on (test)");
  Serial.println("LED OFF   — LED strip off (test)");
  Serial.println("TEMP      — Read DHT11 + publish to Firestore");
#endif
  Serial.println("STATUS    — System status");
  Serial.println("WIFIRESET — Wipe WiFi credentials");
  Serial.println("HELP      — This menu");
  Serial.println("========================================\n");
}

void processSerialCommand(String cmd) {
  cmd.trim(); cmd.toUpperCase();
  if      (cmd == "SCAN")      captureAndProcess();
#if CAMERA_ROOF == 1
  else if (cmd == "LED ON")    ledStripOn();
  else if (cmd == "LED OFF")   ledStripOff();
  else if (cmd == "TEMP")      readAndPublishTemperature();
#endif
  else if (cmd == "STATUS")
    Serial.printf("[STATUS] WiFi: %s  IP: %s  Heap: %u\n",
                  WiFi.status() == WL_CONNECTED ? "OK" : "DISCONNECTED",
                  WiFi.localIP().toString().c_str(),
                  (unsigned)ESP.getFreeHeap());
  else if (cmd == "WIFIRESET") { WiFiManager wm; wm.resetSettings(); ESP.restart(); }
  else if (cmd == "HELP")      printHelp();
  else if (cmd.length() > 0)   Serial.printf("[CMD] Unknown: %s\n", cmd.c_str());
}

// ============================================================================
// SETUP / LOOP
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[BOOT] SmartFridge CAM starting");

  checkResetButton();

  // Start the WiFi radio and pin the ESP-NOW channel BEFORE connecting to the
  // router. esp_wifi_set_channel() only takes effect while the STA is
  // disconnected — call it after WiFiManager has already associated (as the
  // old order did) and it silently no-ops, leaving each board on whatever
  // channel the router happened to assign it. Boards on a router that steers
  // clients to different channels then never see each other's broadcasts.
  WiFi.mode(WIFI_STA);
  initEspNowLink();

  initWiFi();
  reassertEspNowChannel();  // undo any channel change from WiFiManager's config portal
  configureTime();

  initOfflineBuffer();
  initFlash();
  initCamera();
  initLiveViewRtdbStream();
#if CAMERA_ROOF == 1
  initLEDStrip();
  initTempSensor();
#endif

  printHelp();
}

void loop() {
  bool wifiOk = (WiFi.status() == WL_CONNECTED) && (WiFi.localIP() != IPAddress(0,0,0,0));
  if (!wifiOk) {
    if (!wasOffline) {
      Serial.println("[LOOP] WiFi lost — running offline");
      wasOffline = true;
    }
    if (millis() - lastWifiRetryMs >= WIFI_RECONNECT_INTERVAL_MS) {
      lastWifiRetryMs = millis();
      if (savedWifiSSID.length() > 0) {
        Serial.printf("[WIFI] Retrying connection to \"%s\"...\n", savedWifiSSID.c_str());
        WiFi.begin(savedWifiSSID.c_str(), savedWifiPass.c_str());
      } else {
        Serial.println("[WIFI] No saved credentials to retry");
      }
    }
  } else {
    if (wasOffline) {
      Serial.println("[LOOP] WiFi restored");
      wasOffline = false;
    }
    // NTP often isn't reachable at boot (router/DNS not up yet). Keep retrying
    // so the board doesn't spend the whole session stamping 1970 — until this
    // succeeds, scan history and bought counters are skipped.
    if (!isTimeSynced() && millis() - lastTimeSyncMs >= TIME_RESYNC_INTERVAL_MS) {
      lastTimeSyncMs = millis();
      Serial.println("[TIME] clock still unsynced — retrying NTP");
      configTzTime(TIMEZONE, "pool.ntp.org", "time.nist.gov");
    }
    replayOfflinePhotos();
  }

#if CAMERA_ROOF == 1
  if (millis() - lastTempReadMs >= TEMP_READ_INTERVAL_MS) {
    lastTempReadMs = millis();
    readAndPublishTemperature();
  }
#endif

  if (espnowScanTriggerReceived()) {
    Serial.println("[ESPNOW] SCAN_TRIGGER received — auto scan");
    captureAndProcess();            // existing scan flow
    // Signal CH the camera is done so it can process any barcode scan it held
    // back for camera priority. Sent after captureAndProcess() returns so it
    // covers every exit path (success or an early capture/WiFi/Gemini bail).
    espnowSendScanDone();
  }

  if (espnowLiveViewRequested()) {
    Serial.println("[ESPNOW] LIVE_CAPTURE received — sending snapshot");
    size_t sz = 0;
    uint8_t* jpeg = capturePhotoForLiveView(&sz);
    if (jpeg) { espnowSendLiveViewFrame(jpeg, sz); free(jpeg); }
  }

  if (liveViewRtdbStreamPoll()) {
    Serial.println("[LIVEVIEW-RTDB] request received — capturing snapshot");
    size_t sz = 0;
    uint8_t* jpeg = capturePhotoForLiveView(&sz);
    if (jpeg) { saveLiveViewPhotoToFirestore(jpeg, sz); free(jpeg); }
  }

  if (Serial.available())
    processSerialCommand(Serial.readStringUntil('\n'));

  delay(50);
}

