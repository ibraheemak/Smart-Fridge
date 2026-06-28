#pragma once

// ============================================================================
// GM65 barcode scanner (US #15 — manual product scanner)
//
// Reads barcodes over UART (see GM65_RX_PIN/GM65_TX_PIN in parameters.h),
// looks up the product name via the Open Food Facts API, and merges it into
// the Firestore inventory the same way the CAM board's AI scan does: fetch
// the current document, rebuild the full items array, PATCH it back. This
// deliberately does NOT touch g_items directly — leaving it stale until the
// next fetchInventory() poll lets that function's existing "new unit needs
// an expiry date" diff logic pick up the scanned item automatically, exactly
// as it does for camera-detected items.
//
// Requires touch.h and display.h to already be included (for g_view,
// VIEW_LIST, renderInventory()) and fetchInventory() to be defined in the
// main .ino.
// ============================================================================

#include <HardwareSerial.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "parameters.h"
#include "SECRETS.h"

// Defined in the main .ino — forward-declared here because gm65.h is part of
// the same contiguous #include block, so Arduino's auto-prototyping (which
// inserts prototypes after that block) runs too late for pollGM65() below.
bool fetchInventory();

// UART1, not UART2 — uart_link.h's `Serial2` (door-close -> CAM SCAN_TRIGGER
// link) already owns the ESP32's UART2 peripheral on GPIO 16/17. A second
// HardwareSerial object constructed with the same peripheral number (2)
// would silently fight over it: whichever .begin() runs last rewires UART2's
// GPIO-matrix pin routing, so the other link's traffic ends up physically
// transmitted on the wrong pins. GM65Serial must live on the one remaining
// free peripheral, UART1, routed to GPIO 32/33 below.
HardwareSerial GM65Serial(1);

static String        g_gm65_buf;
static unsigned long g_gm65_last_byte_ms = 0;

// ---------------------------------------------------------------------------
// Offline barcode buffer — holds barcodes scanned while WiFi is absent.
// Stored in RAM (simple array). Max 20 items; oldest are dropped if full.
// ---------------------------------------------------------------------------
#define GM65_OFFLINE_MAX 20
static String g_offline_barcodes[GM65_OFFLINE_MAX];
static int    g_offline_count = 0;

void saveOfflineBarcode(const String& barcode) {
  if (g_offline_count >= GM65_OFFLINE_MAX) {
    Serial.println("[GM65][OFFLINE] Buffer full — dropping oldest barcode");
    for (int i = 1; i < GM65_OFFLINE_MAX; i++) g_offline_barcodes[i-1] = g_offline_barcodes[i];
    g_offline_count = GM65_OFFLINE_MAX - 1;
  }
  g_offline_barcodes[g_offline_count++] = barcode;
  Serial.printf("[GM65][OFFLINE] Saved barcode \"%s\" (%d in queue)\n", barcode.c_str(), g_offline_count);
}


// ----------------------------------------------------------------------------
// Scan trigger state machine.
//
// The GM65 defaults to Continuous Mode — its laser/illumination never turns
// off, so it constantly tries to decode whatever's in the lens and beeps on
// any false-positive read (a label, an edge, anything that vaguely looks like
// a barcode). initGM65() switches it to Command Triggered Mode (zone bit
// 0x0000, see manual section 3.4/8.5) so it only scans when explicitly told
// to — triggerGM65Scan() is wired to the "Scan" footer button in touch.h.
//
// On trigger, the module immediately echoes a fixed 7-byte ack frame
// (0x02 0x00 0x00 0x01 0x00 0x33 0x31) before it starts scanning — pollGM65()
// must swallow that frame so it isn't mistaken for barcode data.
// ----------------------------------------------------------------------------
enum Gm65State { GM65_IDLE, GM65_AWAITING_ACK, GM65_READING };

static Gm65State     g_gm65_state       = GM65_IDLE;
static unsigned long g_gm65_scan_started_ms = 0;
static size_t        g_gm65_ack_matched = 0;

static const uint8_t GM65_ACK[] = {0x02, 0x00, 0x00, 0x01, 0x00, 0x33, 0x31};

// CRC_CCITT (poly 0x1021, init 0) exactly as specified in the GM65 manual
// section 8.1 — covers [Types, Lens, Address-hi, Address-lo, Datas]. The
// manual also documents an 0xAB 0xCD "skip CRC check" filler value, but it
// isn't honored by this module's firmware in practice, so commands are
// always sent with a real computed CRC instead.
uint16_t gm65Crc(const uint8_t* data, size_t len) {
  uint32_t crc = 0;
  for (size_t b = 0; b < len; b++) {
    for (uint8_t i = 0x80; i != 0; i >>= 1) {
      crc <<= 1;
      if (crc & 0x10000) crc ^= 0x11021;
      if (data[b] & i)   crc ^= 0x1021;
    }
  }
  return (uint16_t)crc;
}

// Builds and sends a "Write Zone Bit" frame (manual section 8.3) for a
// single byte at `address`.
void gm65WriteZoneBit(uint16_t address, uint8_t value) {
  uint8_t body[] = {0x08, 0x01, (uint8_t)(address >> 8), (uint8_t)(address & 0xFF), value};
  uint16_t crc = gm65Crc(body, sizeof(body));
  uint8_t frame[] = {0x7E, 0x00, body[0], body[1], body[2], body[3], body[4],
                      (uint8_t)(crc >> 8), (uint8_t)(crc & 0xFF)};
  GM65Serial.write(frame, sizeof(frame));
}

// TEMPORARY diagnostic — confirms whether the module's binary command
// channel (manual section 8) is reachable at all over the current wiring.
// "Find baud rate" is a fixed, pre-verified byte sequence straight from
// Appendix A (no CRC computed here, so a failure can't be a CRC bug). A
// healthy module replies "02 00 00 02 39 01 SS SS" within a few ms. Remove
// this block once mode-switching is confirmed working.
void gm65DiagnosticPing() {
  static const uint8_t find_baud_cmd[] = {0x7E, 0x00, 0x07, 0x01, 0x00, 0x2A, 0x02, 0xD8, 0x0F};
  GM65Serial.write(find_baud_cmd, sizeof(find_baud_cmd));

  unsigned long start = millis();
  String hex;
  while (millis() - start < 500) {
    while (GM65Serial.available()) {
      uint8_t b = GM65Serial.read();
      char buf[4];
      snprintf(buf, sizeof(buf), "%02X ", b);
      hex += buf;
    }
  }
  if (hex.length() == 0) {
    Serial.println("[GM65][DIAG] no response to 'find baud rate' — binary command channel is NOT reachable (check wiring/levels or this module's firmware may not support the documented protocol)");
  } else {
    Serial.printf("[GM65][DIAG] response: %s\n", hex.c_str());
  }
}

void initGM65() {
  GM65Serial.begin(GM65_BAUD, SERIAL_8N1, GM65_RX_PIN, GM65_TX_PIN);
  g_gm65_buf.reserve(64);

  // Give the module's own MCU time to finish booting before talking to it —
  // commands sent too early are silently dropped (writes have no ack).
  delay(300);

  gm65DiagnosticPing();

  // Switch to Command Triggered Mode (zone bit 0x0000 = 0xD5: LED-on-success,
  // mute off, standard aim/illumination, mode bits 01 = command triggered).
  gm65WriteZoneBit(0x0000, 0xD5);
}

// Starts a single scan window: per manual section 3.4, in Command Triggered
// Mode the module begins reading when bit0 of zone bit 0x0002 is written 1.
// Call this from a user action (e.g. a touchscreen button) — never
// automatically/continuously, or it just recreates the false-beep problem
// Command Triggered Mode is meant to fix.
void triggerGM65Scan() {
  gm65WriteZoneBit(0x0002, 0x01);

  g_gm65_buf = "";
  g_gm65_ack_matched = 0;
  g_gm65_state = GM65_AWAITING_ACK;
  g_gm65_scan_started_ms = millis();

  showStatus("Scanning...", "Point scanner at barcode");
}

// Looks up a barcode via Open Food Facts. Returns the product name, or ""
// if not found / not reachable. status==0 means "not in their database".
String lookupProductName(const String& barcode) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[GM65][OFF] skip lookup — WiFi not connected");
    return "";
  }

  String url = "https://world.openfoodfacts.org/api/v2/product/" + barcode +
               ".json?fields=product_name,product_name_en,status";
  Serial.printf("[GM65][OFF] GET %s\n", url.c_str());

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(10000);
  if (!http.begin(client, url)) {
    Serial.println("[GM65][OFF] http.begin failed");
    return "";
  }

  int code = http.GET();
  String body = http.getString();
  http.end();
  Serial.printf("[GM65][OFF] HTTP %d, body: %s\n", code, body.c_str());

  if (code != 200) return "";

  DynamicJsonDocument doc(2048);
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.printf("[GM65][OFF] JSON parse error: %s\n", err.c_str());
    return "";
  }

  int status = doc["status"].as<int>();
  if (status != 1) {
    Serial.printf("[GM65][OFF] product not found (status=%d)\n", status);
    return "";
  }

  // Prefer the English name — the TFT's built-in font has no glyphs for
  // Hebrew/other non-Latin scripts, so a localized-only name would just
  // render as blank/garbage on screen. Falls back to whatever's there.
  //
  // Field may be present but JSON null (not just absent) when Open Food
  // Facts has no English name — ArduinoJson's as<String>() on a null
  // JsonVariant returns the literal string "null", not "", so isNull() must
  // be checked explicitly or a null product_name_en ends up displayed as
  // the word "null" instead of falling back.
  JsonVariant en_v   = doc["product"]["product_name_en"];
  JsonVariant name_v = doc["product"]["product_name"];
  String name_en   = en_v.isNull()   ? "" : en_v.as<String>();
  String name_local = name_v.isNull() ? "" : name_v.as<String>();
  String name = name_en.length() > 0 ? name_en : name_local;
  Serial.printf("[GM65][OFF] product_name_en=\"%s\" product_name=\"%s\" -> using \"%s\"\n",
                name_en.c_str(), name_local.c_str(), name.c_str());
  return name;
}

// ----------------------------------------------------------------------------
// URL / field-path helpers (mirrors CAM board's firebase.h — Firestore
// field names can contain spaces/punctuation from real product names).
// ----------------------------------------------------------------------------
String gm65UrlEncode(const String& s) {
  String out;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += c;
    } else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
      out += buf;
    }
  }
  return out;
}

String gm65QuoteFieldPath(const String& name) {
  bool needs_quoting = false;
  for (size_t i = 0; i < name.length(); i++) {
    char c = name[i];
    if (!isalnum((unsigned char)c) && c != '_') { needs_quoting = true; break; }
  }
  if (!needs_quoting) return name;

  String out = "`";
  for (size_t i = 0; i < name.length(); i++) {
    char c = name[i];
    if (c == '`' || c == '\\') out += '\\';
    out += c;
  }
  out += "`";
  return out;
}

// Increments fridges/{fridge}/bought/{YYYY-MM}/{item_name} by 1. A GM65 scan
// is an explicit "I just bought this" action, so unlike the CAM board's
// heuristic (which infers a purchase from a quantity increase), this always
// counts as one purchase.
bool saveBoughtItem(const String& item_name) {
  if (WiFi.status() != WL_CONNECTED) return false;

  time_t now = time(nullptr);
  char month_id[8];
  strftime(month_id, sizeof(month_id), "%Y-%m", localtime(&now));

  String url = "https://firestore.googleapis.com/v1/projects/" + String(FIREBASE_PROJECT_ID) +
               "/databases/(default)/documents/fridges/" + String(FRIDGE_ID) +
               "/bought/" + String(month_id) + "?key=" + String(FIREBASE_API_KEY);

  int prev_count = 0;
  {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(10000);
    if (http.begin(client, url)) {
      int code = http.GET();
      String body = http.getString();
      Serial.printf("[GM65][BOUGHT] GET %s -> %d, body: %s\n", url.c_str(), code, body.c_str());
      if (code == 200) {
        DynamicJsonDocument bought_doc(4096);
        if (!deserializeJson(bought_doc, body) && bought_doc["fields"].containsKey(item_name))
          prev_count = bought_doc["fields"][item_name]["integerValue"].as<int>();
      }
      http.end();
    }
  }

  StaticJsonDocument<512> patch_doc;
  JsonObject patch_fields = patch_doc.createNestedObject("fields");
  patch_fields[item_name]["integerValue"] = prev_count + 1;

  String mask = "&updateMask.fieldPaths=" + gm65UrlEncode(gm65QuoteFieldPath(item_name));
  String payload;
  serializeJson(patch_doc, payload);
  Serial.printf("[GM65][BOUGHT] PATCH payload: %s\n", payload.c_str());

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(10000);
  if (!http.begin(client, url + mask)) return false;
  http.addHeader("Content-Type", "application/json");
  int code = http.PATCH(payload);
  String resp = http.getString();
  http.end();

  Serial.printf("[GM65][BOUGHT] %s -> %d this month (HTTP %d)%s%s\n",
                item_name.c_str(), prev_count + 1, code,
                (code != 200 && code != 201) ? " — " : "", resp.c_str());
  return (code == 200 || code == 201);
}

// Adds one unit of item_name to the Firestore inventory — increments
// quantity if an item with that name already exists (case-insensitive),
// otherwise appends a new entry with quantity 1. Existing expiry dates are
// preserved untouched.
bool addScannedItemToInventory(const String& item_name) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[GM65][INV] skip — WiFi not connected");
    return false;
  }

  String url =
    "https://firestore.googleapis.com/v1/projects/" + String(FIREBASE_PROJECT_ID) +
    "/databases/(default)/documents/fridges/" + String(FRIDGE_ID) +
    "/inventory/current?key=" + String(FIREBASE_API_KEY);

  DynamicJsonDocument cur_doc(8192);
  bool has_existing = false;
  {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(10000);
    if (http.begin(client, url)) {
      int code = http.GET();
      String body = http.getString();
      Serial.printf("[GM65][INV] GET current -> %d, body: %s\n", code, body.c_str());
      if (code == 200) {
        DeserializationError err =
          deserializeJson(cur_doc, body, DeserializationOption::NestingLimit(20));
        has_existing = !err;
        if (err) Serial.printf("[GM65][INV] JSON parse error: %s\n", err.c_str());
      }
      http.end();
    } else {
      Serial.println("[GM65][INV] http.begin (GET) failed");
    }
  }
  Serial.printf("[GM65][INV] has_existing=%d\n", has_existing);

  DynamicJsonDocument doc(8192);
  JsonObject fields = doc["fields"].to<JsonObject>();

  time_t now = time(nullptr);
  char ts[20];
  strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&now));
  fields["updatedAt"]["stringValue"] = ts;
  fields["source"]["stringValue"]    = "GM65";

  JsonArray values = fields["items"]["arrayValue"]["values"].to<JsonArray>();
  bool matched = false;

  if (has_existing) {
    JsonArray existing_items = cur_doc["fields"]["items"]["arrayValue"]["values"];
    for (JsonObject ev : existing_items) {
      JsonObject emf = ev["mapValue"]["fields"];
      String existing_name = emf["name"]["stringValue"].as<String>();

      int qty = emf["quantity"]["stringValue"].as<String>().toInt();
      if (qty <= 0) qty = 1;
      if (!matched && existing_name.equalsIgnoreCase(item_name)) {
        qty += 1;
        matched = true;
      }

      JsonObject out = values.createNestedObject()["mapValue"]["fields"].to<JsonObject>();
      out["name"]["stringValue"]       = existing_name;
      out["quantity"]["stringValue"]   = String(qty);
      out["confidence"]["stringValue"] = emf["confidence"]["stringValue"].as<String>();

      JsonArray ea = emf["expiries"]["arrayValue"]["values"];
      if (ea.size() > 0) {
        JsonArray out_ea = out["expiries"]["arrayValue"]["values"].to<JsonArray>();
        for (JsonObject ed : ea)
          out_ea.createNestedObject()["stringValue"] = ed["stringValue"].as<String>();
      }
    }
  }

  if (!matched) {
    JsonObject out = values.createNestedObject()["mapValue"]["fields"].to<JsonObject>();
    out["name"]["stringValue"]       = item_name;
    out["quantity"]["stringValue"]   = "1";
    out["confidence"]["stringValue"] = "100";  // manually scanned, not AI-estimated
  }

  String payload;
  serializeJson(doc, payload);
  Serial.printf("[GM65][INV] PATCH payload: %s\n", payload.c_str());

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(10000);
  if (!http.begin(client, url)) {
    Serial.println("[GM65][INV] http.begin (PATCH) failed");
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  int code = http.PATCH(payload);
  String resp = http.getString();
  http.end();

  Serial.printf("[GM65][INV] save %s for \"%s\" (HTTP %d)%s%s\n",
                (code == 200 || code == 201) ? "OK" : "FAILED", item_name.c_str(), code,
                (code != 200 && code != 201) ? " — " : "", resp.c_str());
  return (code == 200 || code == 201);
}

// Call from loop(). Only reacts to bytes while a scan is armed (see
// triggerGM65Scan()) — in Command Triggered Mode the module stays silent
// otherwise, so stray bytes here would indicate noise, not a real scan.
// Swallows the module's ack frame, then buffers the barcode digits until a
// complete read arrives (flushed on an idle gap since the module doesn't
// reliably send a CR/LF terminator), looks up the product name and adds it
// to the inventory.
void pollGM65() {
  while (GM65Serial.available()) {
    uint8_t b = GM65Serial.read();
    g_gm65_last_byte_ms = millis();

    if (g_gm65_state == GM65_AWAITING_ACK) {
      if (b == GM65_ACK[g_gm65_ack_matched]) {
        g_gm65_ack_matched++;
        if (g_gm65_ack_matched == sizeof(GM65_ACK)) g_gm65_state = GM65_READING;
      } else {
        g_gm65_ack_matched = (b == GM65_ACK[0]) ? 1 : 0;
      }
      continue;
    }

    if (g_gm65_state != GM65_READING) continue;  // not armed — ignore stray bytes

    char c = (char)b;
    if (c == '\r' || c == '\n') continue;
    g_gm65_buf += c;
  }

  if (g_gm65_state == GM65_IDLE) return;

  if (g_gm65_buf.length() == 0) {
    // Still waiting for the ack or for the first barcode byte — give up
    // after the timeout so the UI doesn't show "Scanning..." forever.
    if (millis() - g_gm65_scan_started_ms > GM65_SCAN_TIMEOUT_MS) {
      g_gm65_state = GM65_IDLE;
      showStatus("No barcode found", "");
      delay(1200);
      if (fetchInventory() && g_view == VIEW_LIST) renderInventory();
    }
    return;
  }
  if (millis() - g_gm65_last_byte_ms < GM65_IDLE_FLUSH_MS) return;

  String barcode = g_gm65_buf;
  g_gm65_buf = "";
  g_gm65_state = GM65_IDLE;

  Serial.printf("[GM65] Scanned barcode: %s\n", barcode.c_str());
  showStatus("Scanned barcode", barcode);

  if (WiFi.status() != WL_CONNECTED) {
    saveOfflineBarcode(barcode);
    showStatus("No Internet", "Item saved, will sync when back online");
    delay(5000);
    g_view = VIEW_LIST;
    renderInventory();
    return;
  }

  String name = lookupProductName(barcode);
  if (name.length() == 0) {
    showStatus("Unknown item", "Barcode " + barcode + " not found");
    delay(1500);
  } else {
    showStatus("Adding item...", name);
    bool ok = addScannedItemToInventory(name);
    if (ok) saveBoughtItem(name);
    showStatus(ok ? "Added!" : "Save failed", name);
    delay(1500);
  }

  if (fetchInventory() && g_view == VIEW_LIST) renderInventory();
}

// Called from loop() when WiFi is restored — processes every queued barcode
// exactly as a live scan would (lookup -> inventory -> bought).
void replayOfflineBarcodes() {
  if (g_offline_count == 0) return;
  Serial.printf("[GM65][OFFLINE] WiFi restored — replaying %d barcode(s)\n", g_offline_count);
  for (int i = 0; i < g_offline_count; i++) {
    String barcode = g_offline_barcodes[i];
    Serial.printf("[GM65][OFFLINE] Replaying: %s\n", barcode.c_str());
    showStatus("Syncing offline...", barcode);
    String name = lookupProductName(barcode);
    if (name.length() == 0) {
      Serial.printf("[GM65][OFFLINE] Barcode %s not found — skipping\n", barcode.c_str());
    } else {
      bool ok = addScannedItemToInventory(name);
      if (ok) saveBoughtItem(name);
      Serial.printf("[GM65][OFFLINE] \"%s\" -> %s\n", name.c_str(), ok ? "OK" : "FAILED");
    }
  }
  g_offline_count = 0;
  if (fetchInventory() && g_view == VIEW_LIST) renderInventory();
}
