/*
 * Smart Fridge — Display ESP32 (CH9102 devkit)
 *
 * Polls Firestore for the current inventory and renders it on the display.
 * Add new peripherals by creating a new header (e.g. sensors.h) and
 * including it here.
 *
 * File layout:
 *   display.h    — TFT + icon rendering
 *   parameters.h — pin assignments and tunable constants
 *   tft_setup.h  — TFT_eSPI pin config (auto-loaded by the library)
 *   SECRETS.h    — Firebase credentials
 */

#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "time.h"

#include "SECRETS.h"
#include "parameters.h"
#include "display.h"
#include "touch.h"
#include "dht11.h"

// ============================================================================
// STATE
// ============================================================================
String        g_last_signature = "";
unsigned long g_last_poll_ms   = 0;

// Previous inventory snapshot — used to detect new units added since last scan.
// Populated after the first successful fetch so we don't false-trigger on boot.
struct PrevItem {
  String name;
  int    expiry_count;  // how many expiry slots existed before
};
PrevItem      g_prev_items[MAX_ITEMS_DISPLAYED];
int           g_prev_item_count  = 0;
bool          g_prev_initialized = false;  // false until after the very first fetch

// ============================================================================
// FIRESTORE
// ============================================================================
String buildSignature() {
  String sig = g_updated_at + "|";
  for (int i = 0; i < g_item_count; i++)
    sig += g_items[i].name + ":" + g_items[i].quantity + ";";
  return sig;
}

bool fetchInventory() {
  if (WiFi.status() != WL_CONNECTED) return false;

  String url =
    "https://firestore.googleapis.com/v1/projects/" +
    String(FIREBASE_PROJECT_ID) +
    "/databases/(default)/documents/fridges/" +
    String(FRIDGE_ID) +
    "/inventory/current?key=" +
    String(FIREBASE_API_KEY);

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(10000);
  if (!http.begin(client, url)) {
    Serial.println("[FIREBASE] http.begin failed");
    return false;
  }

  int code = http.GET();
  if (code != 200) {
    Serial.printf("[FIREBASE] GET inventory/current -> %d\n", code);
    http.end();
    return false;
  }

  String body = http.getString();
  http.end();

  DynamicJsonDocument doc(8192);
  if (deserializeJson(doc, body)) return false;

  JsonObject fields = doc["fields"];
  if (fields.isNull()) return false;

  g_updated_at = fields["updatedAt"]["stringValue"].as<String>();
  JsonArray values = fields["items"]["arrayValue"]["values"];

  // Save previous snapshot before overwriting g_items.
  g_prev_item_count = g_item_count;
  for (int i = 0; i < g_item_count; i++) {
    g_prev_items[i].name         = g_items[i].name;
    g_prev_items[i].expiry_count = g_items[i].expiry_count;
  }

  g_item_count = 0;
  for (JsonObject v : values) {
    if (g_item_count >= MAX_ITEMS_DISPLAYED) break;
    JsonObject mf = v["mapValue"]["fields"];
    InventoryItem& it = g_items[g_item_count];
    it.name       = mf["name"]["stringValue"].as<String>();
    it.quantity   = mf["quantity"]["stringValue"].as<String>();
    it.confidence = mf["confidence"]["stringValue"].as<String>();
    it.expiry_count = 0;

    // Parse expiries array (new format).
    JsonArray ea = mf["expiries"]["arrayValue"]["values"];
    for (JsonObject ev : ea) {
      if (it.expiry_count >= MAX_EXPIRIES_PER_ITEM) break;
      it.expiries[it.expiry_count++] = ev["stringValue"].as<String>();
    }
    // Backward-compat: if old "expiry" single field exists and no array yet.
    if (it.expiry_count == 0) {
      String legacy = mf["expiry"]["stringValue"].as<String>();
      if (legacy.length() == 10) {
        it.expiries[0] = legacy;
        it.expiry_count = 1;
      }
    }

    g_item_count++;
  }

  Serial.printf("[FIREBASE] %d items, updatedAt=%s\n", g_item_count, g_updated_at.c_str());

  // Detect new units and enqueue expiry prompts — skip on very first fetch.
  if (g_prev_initialized) {
    for (int i = 0; i < g_item_count; i++) {
      InventoryItem& it = g_items[i];
      // Find matching item in previous snapshot.
      int prev_expiry_count = 0;
      bool found = false;
      for (int j = 0; j < g_prev_item_count; j++) {
        if (g_prev_items[j].name.equalsIgnoreCase(it.name)) {
          prev_expiry_count = g_prev_items[j].expiry_count;
          found = true;
          break;
        }
      }
      // New units = slots that didn't exist before and have no date yet.
      int start = found ? prev_expiry_count : 0;
      // Parse quantity to estimate how many units there are now.
      int qty = it.quantity.toInt();
      if (qty <= 0) qty = it.expiry_count > 0 ? it.expiry_count : 1;
      // Clamp to array bounds.
      if (qty > MAX_EXPIRIES_PER_ITEM) qty = MAX_EXPIRIES_PER_ITEM;
      // Grow expiry_count to match quantity (new slots start empty).
      while (it.expiry_count < qty) it.expiries[it.expiry_count++] = "";
      // Enqueue any new empty slots.
      for (int s = start; s < it.expiry_count; s++) {
        if (it.expiries[s].length() == 0)
          enqueuePendingExpiry(i, s);
      }
    }
    if (g_pending_count > 0) processNextPending();
  }

  g_prev_initialized = true;
  return true;
}

// ============================================================================
// WIFI
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
      Serial.println("[WIFI] Credentials wiped, restarting...");
      delay(1000);
      ESP.restart();
    }
    delay(50);
  }
}

void initWiFi() {
  showStatus("Connecting WiFi", "AP: " WIFI_AP_NAME);
  WiFiManager wm;
  wm.setConnectTimeout(20);
  wm.setConfigPortalTimeout(WIFI_PORTAL_TIMEOUT_S);
  wm.setAPCallback([](WiFiManager*) {
    showStatus("Setup needed", "Join " WIFI_AP_NAME);
  });
  if (!wm.autoConnect(WIFI_AP_NAME)) {
    showStatus("WiFi failed", "Restarting...");
    delay(3000);
    ESP.restart();
  }
  Serial.printf("[WIFI] Connected: %s\n", WiFi.localIP().toString().c_str());
}

void configureTime() {
  configTzTime(TIMEZONE, "pool.ntp.org", "time.nist.gov");
  time_t now = time(nullptr);
  unsigned long t0 = millis();
  while (now < 24 * 3600 && millis() - t0 < 10000) { delay(250); now = time(nullptr); }
}

// ============================================================================
// SETUP / LOOP
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[BOOT] SmartFridge Display starting");

  backlightOn();
  tft.init();
  tft.setRotation(DISPLAY_ROTATION);
  tft.fillScreen(TFT_BLACK);
  TJpgDec.setSwapBytes(true);

  showStatus("Smart Fridge", "Booting...");
  initTouch();
  checkResetButton();
  initWiFi();
  configureTime();
  initDHT11();

  showStatus("Loading inventory", "");
  if (fetchInventory()) {
    g_last_signature = buildSignature();
    renderInventory();
  } else {
    showStatus("No data yet", "Waiting for fridge scan");
  }
  g_last_poll_ms = millis();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    showStatus("WiFi lost", "Restarting...");
    delay(2000);
    ESP.restart();
  }

  if (millis() - g_last_poll_ms >= INVENTORY_POLL_INTERVAL_MS) {
    g_last_poll_ms = millis();
    // Don't poll while the user is entering an expiry date — it would overwrite the screen.
    if (g_view == VIEW_LIST) {
      if (fetchInventory()) {
        String sig = buildSignature();
        if (sig != g_last_signature) {
          g_last_signature = sig;
          // Only redraw if no pending notification is about to take over the screen.
          if (g_pending_count == 0) renderInventory();
        }
      }
    }
  }

  handleTouch();
  tickDHT11();
  delay(50);
}
