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

HardwareSerial GM65Serial(2);

static String        g_gm65_buf;
static unsigned long g_gm65_last_byte_ms = 0;

void initGM65() {
  GM65Serial.begin(GM65_BAUD, SERIAL_8N1, GM65_RX_PIN, GM65_TX_PIN);
  g_gm65_buf.reserve(64);
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
  String name_en = doc["product"]["product_name_en"].as<String>();
  String name    = name_en.length() > 0 ? name_en : doc["product"]["product_name"].as<String>();
  Serial.printf("[GM65][OFF] product_name_en=\"%s\" product_name=\"%s\" -> using \"%s\"\n",
                name_en.c_str(), doc["product"]["product_name"].as<String>().c_str(), name.c_str());
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

// Call from loop(). Buffers incoming bytes from the GM65 and, once a
// complete barcode arrives (flushed on an idle gap since the module doesn't
// reliably send a CR/LF terminator), looks up the product name and adds it
// to the inventory.
void pollGM65() {
  while (GM65Serial.available()) {
    char c = GM65Serial.read();
    g_gm65_last_byte_ms = millis();
    if (c == '\r' || c == '\n') continue;
    g_gm65_buf += c;
  }

  if (g_gm65_buf.length() == 0) return;
  if (millis() - g_gm65_last_byte_ms < GM65_IDLE_FLUSH_MS) return;

  String barcode = g_gm65_buf;
  g_gm65_buf = "";

  Serial.printf("[GM65] Scanned barcode: %s\n", barcode.c_str());
  showStatus("Scanned barcode", barcode);

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
