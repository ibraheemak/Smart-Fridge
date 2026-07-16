/*
 * Smart Fridge — Display ESP32 (CH9102 devkit)
 *
 * Renders the current Firestore inventory on the display, refreshing the
 * instant it changes via a Realtime Database SSE "doorbell" stream (see
 * rtdb_stream.h) instead of polling on a timer.
 * Add new peripherals by creating a new header (e.g. sensors.h) and
 * including it here.
 *
 * File layout:
 *   display.h     — TFT + icon rendering
 *   parameters.h  — pin assignments and tunable constants
 *   tft_setup.h   — TFT_eSPI pin config (auto-loaded by the library)
 *   SECRETS.h     — Firebase credentials
 *   gm65.h        — GM65 barcode scanner -> Open Food Facts -> inventory
 *   recipes.h     — Gemini recipe suggestions from current inventory (US #7)
 *   rtdb_notify.h — bumps the RTDB "inventory changed" doorbell after a write
 *   rtdb_stream.h — listens on that doorbell to trigger an instant re-fetch
 */

#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>
#include "time.h"

#include "SECRETS.h"
#include "parameters.h"
#include "time_sync.h"
#include "inventory_merge.h"
#include "display.h"
#include "touch.h"
#include "stats.h"
#include "door.h"
#include "espnow_link.h"
#include "buzzer.h"
#include "settings.h"
#include "liveview.h"
#include "notifications.h"
#include "gm65.h"
#include "recipes.h"
#include "rtdb_stream.h"

// ============================================================================
// STATE
// ============================================================================
String        g_last_signature = "";
unsigned long g_last_wifi_retry_ms = 0;
unsigned long g_last_clock_ms  = 0;   // last home-screen footer clock redraw
bool          g_was_offline    = false;
String        g_wifi_ssid;
String        g_wifi_pass;

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

  // Default nesting limit (10) is too shallow once per-unit expiries are
  // nested inside each item (fields > items > arrayValue > values > mapValue
  // > fields > expiries > arrayValue > values > stringValue) — without this,
  // parsing fails with TooDeep and the list silently appears empty.
  DynamicJsonDocument doc(8192);
  if (deserializeJson(doc, body, DeserializationOption::NestingLimit(20))) return false;

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

  // Detect units that still need an expiry date and enqueue prompts for them.
  // g_prev_item_count is 0 on the very first fetch (incl. right after a reboot),
  // so every item is treated as "found = false" and any empty expiry slot is
  // enqueued — this also re-prompts for units that were never dated before a
  // restart, instead of silently adopting them as the baseline.
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
    // Log a "new item detected" alert (in addition to the on-screen
    // VIEW_NEW_ITEM prompt). Only once the baseline is established
    // (g_prev_initialized) so a reboot — where the previous snapshot is empty
    // and every item looks new — doesn't flood the log.
    if (g_prev_initialized && !found && g_newitem_alert_on)
      addNotification("New item: " + it.name);
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

  g_prev_initialized = true;
  return true;
}

// ============================================================================
// DOOR STATE — writes to fridges/{id}/sensors/door
// ============================================================================
void saveDoorState(bool closed) {
  if (WiFi.status() != WL_CONNECTED) return;

  // Match the timestamp format the CAM board uses for sensors/temperature
  // ("YYYY-MM-DD HH:MM:SS", local time) so the app shows both consistently.
  // Skip the write entirely if the clock hasn't synced yet — better to miss
  // one door event than persist a bogus "1970-01-01" timestamp.
  char ts[20];
  if (!formatTimestampIfSynced(ts, sizeof(ts))) {
    Serial.println("[DOOR] Skipping Firestore write — clock not synced yet");
    return;
  }

  String url =
    "https://firestore.googleapis.com/v1/projects/" +
    String(FIREBASE_PROJECT_ID) +
    "/databases/(default)/documents/fridges/" +
    String(FRIDGE_ID) +
    "/sensors/door?key=" +
    String(FIREBASE_API_KEY);

  String body =
    "{\"fields\":{"
    "\"state\":{\"stringValue\":\"" + String(closed ? "closed" : "open") + "\"},"
    "\"updatedAt\":{\"stringValue\":\"" + String(ts) + "\"},"
    "\"source\":{\"stringValue\":\"ESP32-CH\"}"
    "}}";

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(5000);
  if (!http.begin(client, url)) return;
  http.addHeader("Content-Type", "application/json");
  int code = http.PATCH(body);
  http.end();
  Serial.printf("[DOOR] Firestore -> %s (HTTP %d)\n", closed ? "closed" : "open", code);
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
    showStatus("WiFi unavailable", "Will retry...");
    Serial.println("[WIFI] Portal timed out — continuing offline");
  }

  // Capture whatever credentials WiFiManager has on file (saved from a
  // previous successful connection, or just entered in the portal) so the
  // retry loop can re-issue WiFi.begin() with them even if this boot never
  // actually connected (e.g. router was briefly down and the portal timed
  // out before it came back).
  g_wifi_ssid = wm.getWiFiSSID();
  g_wifi_pass = wm.getWiFiPass();

  WiFi.mode(WIFI_STA);  // drop the AP WiFiManager may have left running

  Serial.printf("[WIFI] Connected: %s channel %d\n", WiFi.localIP().toString().c_str(), WiFi.channel());
}

// ============================================================================
// SETUP / LOOP
// ============================================================================
// Recovers from a genuine hang (e.g. a stalled TLS connect) that would
// otherwise freeze the display/touch forever with no watchdog to reset it.
// loop() normally completes well under a second, but several network calls
// in a single iteration can legitimately stack close to (RTDB connect 5s +
// fetchInventory's 10s HTTP timeout, etc.) — give real headroom above that
// so only a genuine hang (no timeout at all) trips it, not a slow-but-alive
// sequence of requests.
#define WATCHDOG_TIMEOUT_S 25

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[BOOT] SmartFridge Display starting");

  backlightOn();
  tft.init();
  tft.setRotation(DISPLAY_ROTATION);
  tft.fillScreen(TFT_BLACK);
  TJpgDec.setSwapBytes(true);
  initHebrewFont();

  showStatus("Smart Fridge", "Booting...");
  initTouch();
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
  initDoorSensor();
  initBuzzer();
  initSettings();   // load buzzer/door-alert/temp-humidity prefs, apply them
  loadNotifications();     // restore the persisted alerts log + retention setting
  purgeOldNotifications(); // drop anything already past its retention window
  initGM65();

  showStatus("Loading inventory", "");
  mergeRoofInventories();
  if (fetchInventory()) {
    g_last_signature = buildSignature();
    scanExpiries();   // log any already-expired / about-to-expire units at boot
    // Don't overwrite the new-item prompt that fetchInventory() may have just drawn.
    if (g_view == VIEW_LIST) renderInventory();
  } else {
    showStatus("No data yet", "Waiting for fridge scan");
  }
  initRtdbStream();

  // Temperature/humidity arrive via ESP-NOW push from the roof1 CAM board
  // (see espnowTemperatureReceived() below) — no fetch needed here. The home
  // screen shows g_temp_c/g_humidity's "-1 = no data yet" state until the
  // first push arrives.

  // Always start on the home screen after boot.
  if (g_view == VIEW_HOME) renderHomeScreen();

  // Arm the watchdog only now — everything above (WiFiManager portal, NTP
  // wait) can legitimately run long and would otherwise trip it during boot.
  //
  // The ESP32 core already auto-initializes a TWDT instance at startup for
  // the idle tasks (CONFIG_ESP_TASK_WDT_INIT=y, 5s timeout, panic-on-timeout
  // — see sdkconfig) but never subscribes loopTask to it. esp_task_wdt_init()
  // therefore returns ESP_ERR_INVALID_STATE here ("already initialized") and
  // — critically — does NOT apply wdt_config's longer timeout; falling
  // through to esp_task_wdt_add() in that case would subscribe loopTask to
  // the pre-existing 5s instance instead of our 25s one, tripping on any
  // ordinary multi-second loop() iteration. Reconfigure the existing
  // instance instead so our timeout actually takes effect.
  esp_task_wdt_config_t wdt_config = { WATCHDOG_TIMEOUT_S * 1000, 0, true };
  if (esp_task_wdt_init(&wdt_config) == ESP_ERR_INVALID_STATE) {
    esp_task_wdt_reconfigure(&wdt_config);
  }
  esp_task_wdt_add(NULL);
}

void loop() {
  esp_task_wdt_reset();

  retryTimeSyncIfNeeded();

  if (WiFi.status() != WL_CONNECTED) {
    g_was_offline = true;
    if (millis() - g_last_wifi_retry_ms >= WIFI_RECONNECT_INTERVAL_MS) {
      g_last_wifi_retry_ms = millis();
      if (g_wifi_ssid.length() > 0) {
        Serial.printf("[WIFI] Retrying connection to \"%s\"...\n", g_wifi_ssid.c_str());
        WiFi.begin(g_wifi_ssid.c_str(), g_wifi_pass.c_str());
      } else {
        Serial.println("[WIFI] No saved credentials to retry");
      }
    }
  } else if (g_was_offline) {
    g_was_offline = false;
    Serial.println("[WIFI] Restored — syncing offline barcodes");
    replayOfflineBarcodes();
  }

  // Temperature/humidity pushed from the roof1 CAM board over ESP-NOW —
  // refresh the home header the instant a new reading arrives.
  float temp_c, humidity;
  if (espnowTemperatureReceived(temp_c, humidity)) {
    g_temp_c   = temp_c;
    g_humidity = humidity;
    if (g_view == VIEW_HOME) updateHomeHeaderSensors();
    // Raise (or clear) the out-of-range alert on every fresh reading.
    checkEnvironmentAlert();
  }

  // Live View — a requested roof's JPEG snapshot finishing reassembly, or a
  // stalled transfer giving up (see espnow_link.h for the chunk protocol).
  uint8_t lv_roof; uint8_t* lv_jpeg = nullptr; size_t lv_len = 0;
  if (liveViewFrameReady(lv_roof, &lv_jpeg, &lv_len)) {
    liveViewOnFrameReceived(lv_roof, lv_jpeg, lv_len);
  }
  if (liveViewTransferTimedOut()) {
    liveViewOnTimeout();
  }

  // Show a deferred alert once the user is back on an idle screen.
  serviceEnvironmentAlert();

  if (rtdbStreamPoll()) {
    // Don't refresh while the user is on the new-item/expiry-entry screen — it
    // would overwrite the screen or disrupt an in-progress edit. Refreshing on
    // VIEW_STATS is fine since that screen doesn't depend on g_items.
    if (g_view == VIEW_HOME || g_view == VIEW_LIST || g_view == VIEW_STATS) {
      mergeRoofInventories();
      if (fetchInventory()) {
        scanExpiries();   // re-check expiries whenever the inventory refreshes
        String sig = buildSignature();
        if (sig != g_last_signature) {
          g_last_signature = sig;
          // fetchInventory() may have already switched to VIEW_NEW_ITEM and drawn
          // the notification screen (via processNextPending()) — don't paint over
          // it. g_pending_count alone isn't a reliable signal here: it's already
          // been decremented for the one notification currently on screen.
          if (g_view == VIEW_HOME)  renderHomeScreen();
          if (g_view == VIEW_LIST)  renderInventory();
        }
      }
    }
  }

  // Redraw just the home-screen clock every second — the date next to it
  // doesn't change second-to-second, see updateHomeFooterClock() in display.h.
  if (g_view == VIEW_HOME && millis() - g_last_clock_ms >= 1000) {
    g_last_clock_ms = millis();
    updateHomeFooterClock();
  }

  handleTouch();
  pollGM65();

  if (doorJustClosed()) {
    Serial.println("[DOOR] Closed — triggering CAM scan");
    // Keep buzzer updated during the settle wait so it doesn't overshoot.
    unsigned long settleStart = millis();
    while (millis() - settleStart < DOOR_SETTLE_MS) {
      updateBuzzer();
      delay(50);
    }
    espnowSendScanTrigger();
    saveDoorState(true);
  }
  if (doorJustOpened()) {
    saveDoorState(false);
  }

  if (doorOpenTooLong()) {
    Serial.println("[DOOR] Open too long — buzzing!");
    buzzFor(g_buzzer_duration_ms);
    addNotification("Door left open");
  }

  updateBuzzer();

  // Periodically drop notifications past their retention window (the user-set
  // "auto-delete after X days" in Alert Settings). Cheap and infrequent.
  static unsigned long g_last_notif_purge_ms = 0;
  if (millis() - g_last_notif_purge_ms >= 300000UL) {   // every 5 min
    g_last_notif_purge_ms = millis();
    // Re-scan expiries too: an item can cross its "about to expire" / "expired"
    // threshold purely by the clock advancing, with no inventory change to
    // trigger the poll-driven scan above.
    int before = g_notif_count;
    scanExpiries();
    bool purged = purgeOldNotifications();
    if (purged) saveNotifications();
    if ((purged || g_notif_count != before) && g_view == VIEW_HOME) renderHomeScreen();
  }

  delay(50);
}
