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
#include <WebServer.h>
#include "time.h"

#include "SECRETS.h"
#include "parameters.h"
#include "camera.h"
#include "firebase.h"
#include "gemini.h"
#include "led_strip.h"
#include "door.h"
#include "temperature.h"

// ============================================================================
// STATE
// ============================================================================
WebServer webServer(80);
unsigned long lastTempReadMs = 0;

// ============================================================================
// WEB SERVER — /latest.jpg debug endpoint
// ============================================================================
void handleLatestJpeg() {
  if (latest_jpeg && latest_jpeg_size > 0) {
    WiFiClient client = webServer.client();
    client.print("HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\nContent-Length: ");
    client.print(latest_jpeg_size);
    client.print("\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n");
    size_t sent = 0;
    while (sent < latest_jpeg_size) {
      size_t chunk = min((size_t)1024, latest_jpeg_size - sent);
      client.write(latest_jpeg + sent, chunk);
      sent += chunk;
    }
    client.flush();
  } else {
    webServer.send(404, "text/plain", "No image captured yet. Send SCAN via serial first.");
  }
}

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
    Serial.println("[WIFI] Portal timed out, restarting");
    delay(3000);
    ESP.restart();
  }
  Serial.printf("[WIFI] Connected: %s (%s)\n",
                WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
}

void configureTime() {
  configTzTime(TIMEZONE, "pool.ntp.org", "time.nist.gov");
  time_t now = time(nullptr);
  unsigned long t0 = millis();
  while (now < 24 * 3600 && millis() - t0 < 10000) { delay(250); now = time(nullptr); }
}

// ============================================================================
// SCAN CYCLE
// ============================================================================
void captureAndProcess() {
  Serial.println("[SCAN] Starting...");
  ledStripOn();
  delay(20);

  size_t photo_size = 0;
  uint8_t* photo_data = capturePhoto(&photo_size);
  ledStripOff();

  if (!photo_data) {
    Serial.println("[SCAN] Capture failed");
    return;
  }

  Serial.println("[SCAN] Sending to Gemini...");
  String basic_items = fetchBasicItems();
  String response    = sendToGemini(photo_data, photo_size, basic_items);
  free(photo_data);

  if (response.length() == 0) {
    Serial.println("[SCAN] No Gemini response");
    return;
  }

  StaticJsonDocument<2048> detected_items;
  if (!parseGeminiResponse(response, detected_items)) {
    Serial.println("[SCAN] Parse failed");
    return;
  }

#if DEBUG_MODE
  if (detected_items.containsKey("description"))
    Serial.printf("[DEBUG] %s\n", detected_items["description"].as<const char*>());
#endif

  saveToFirebase(detected_items);
  saveScanHistory(detected_items);
  Serial.printf("[SCAN] Done — %d items written to Firestore\n",
                (int)detected_items["items"].as<JsonArray>().size());
}

// ============================================================================
// TEMPERATURE / HUMIDITY
// ============================================================================
void readAndPublishTemperature() {
  float tempC, humidity;
  if (!readTemperature(tempC, humidity)) {
    Serial.println("[TEMP] Read failed");
    return;
  }
  Serial.printf("[TEMP] %.1f C, %.1f %% RH\n", tempC, humidity);
  saveTemperature(tempC, humidity);
}

// ============================================================================
// SERIAL COMMANDS
// ============================================================================
void printHelp() {
  Serial.println("\n========================================");
  Serial.println("SmartFridge CAM — Commands");
  Serial.println("SCAN      — Capture, analyze, save to Firestore");
  Serial.println("LED ON    — LED strip on (test)");
  Serial.println("LED OFF   — LED strip off (test)");
  Serial.println("TEMP      — Read DHT11 + publish to Firestore");
  Serial.println("STATUS    — System status");
  Serial.println("WIFIRESET — Wipe WiFi credentials");
  Serial.println("HELP      — This menu");
  Serial.println("========================================\n");
}

void processSerialCommand(String cmd) {
  cmd.trim(); cmd.toUpperCase();
  if      (cmd == "SCAN")      captureAndProcess();
  else if (cmd == "LED ON")    ledStripOn();
  else if (cmd == "LED OFF")   ledStripOff();
  else if (cmd == "TEMP")      readAndPublishTemperature();
  else if (cmd == "STATUS")
    Serial.printf("[STATUS] WiFi: %s  IP: %s  Heap: %u  Door: %s\n",
                  WiFi.status() == WL_CONNECTED ? "OK" : "DISCONNECTED",
                  WiFi.localIP().toString().c_str(),
                  (unsigned)ESP.getFreeHeap(),
                  doorIsClosed() ? "CLOSED" : "OPEN");
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
  initWiFi();
  configureTime();

  initFlash();
  initCamera();
  initLEDStrip();
  initDoorSensor();
  initTempSensor();

  webServer.on("/latest.jpg", HTTP_GET, handleLatestJpeg);
  webServer.begin();
  Serial.printf("[WEB] http://%s/latest.jpg\n", WiFi.localIP().toString().c_str());

  printHelp();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[LOOP] WiFi lost — restarting");
    delay(2000);
    ESP.restart();
  }

  webServer.handleClient();

  if (millis() - lastTempReadMs >= TEMP_READ_INTERVAL_MS) {
    lastTempReadMs = millis();
    readAndPublishTemperature();
  }

  if (doorJustClosed()) {
    Serial.println("[DOOR] Closed — auto scan");
    delay(DOOR_SETTLE_MS);          // let door seal + items settle
    captureAndProcess();            // existing scan flow
  }

  if (Serial.available())
    processSerialCommand(Serial.readStringUntil('\n'));

  delay(50);
}

