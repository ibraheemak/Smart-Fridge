#pragma once

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "parameters.h"
#include "SECRETS.h"

// ============================================================================
// Timestamp helpers
// ============================================================================
String getFormattedTimestamp() {
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  char buf[20];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", t);
  return String(buf);
}

String getISOTimestamp() {
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  char buf[20];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H-%M-%S", t);
  return String(buf);
}

String getWeekId() {
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  char buf[14];
  snprintf(buf, sizeof(buf), "%04d-%02d-W%d", t->tm_year+1900, t->tm_mon+1, (t->tm_mday-1)/7+1);
  return String(buf);
}

String getMonthId() {
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  char buf[8];
  strftime(buf, sizeof(buf), "%Y-%m", t);
  return String(buf);
}

// ============================================================================
// Firestore read
// ============================================================================
String fetchBasicItems() {
  String url = "https://firestore.googleapis.com/v1/projects/" + String(FIREBASE_PROJECT_ID) +
               "/databases/(default)/documents/basic-items/basic-items?key=" + String(FIREBASE_API_KEY);
  HTTPClient http;
  http.begin(url);
  if (http.GET() != 200) { http.end(); return ""; }
  String body = http.getString();
  http.end();

  StaticJsonDocument<2048> doc;
  if (deserializeJson(doc, body)) return "";
  String result = "";
  for (JsonObject v : doc["fields"]["items"]["arrayValue"]["values"].as<JsonArray>()) {
    if (result.length()) result += ", ";
    result += v["stringValue"].as<String>();
  }
  return result;
}

// ============================================================================
// Firestore write
// ============================================================================

// Fetch the existing expiries array for a named item from the current
// inventory document. Returns a comma-separated list of "YYYY-MM-DD" strings
// (or empty string if none exist). Used so a new scan doesn't wipe dates the
// user already entered.
String fetchExistingExpiries(const String& item_name) {
  String url = "https://firestore.googleapis.com/v1/projects/" + String(FIREBASE_PROJECT_ID) +
               "/databases/(default)/documents/fridges/" + String(FRIDGE_ID) +
               "/inventory/" + INVENTORY_DOC_ID + "?key=" + String(FIREBASE_API_KEY);
  HTTPClient http;
  http.begin(url);
  int code = http.GET();
  if (code != 200) { http.end(); return ""; }
  String body = http.getString();
  http.end();

  DynamicJsonDocument doc(8192);
  if (deserializeJson(doc, body, DeserializationOption::NestingLimit(20))) return "";

  JsonArray existing = doc["fields"]["items"]["arrayValue"]["values"];
  for (JsonObject v : existing) {
    JsonObject mf = v["mapValue"]["fields"];
    String name = mf["name"]["stringValue"].as<String>();
    if (!name.equalsIgnoreCase(item_name)) continue;

    // Found matching item — collect its expiries.
    String result = "";
    JsonArray ea = mf["expiries"]["arrayValue"]["values"];
    for (JsonObject ev : ea) {
      String d = ev["stringValue"].as<String>();
      if (result.length()) result += ",";
      result += d;
    }
    // Backward-compat: single legacy "expiry" field.
    if (result.length() == 0) {
      String legacy = mf["expiry"]["stringValue"].as<String>();
      if (legacy.length() == 10) result = legacy;
    }
    return result;
  }
  return "";
}

// Percent-encode a string for safe use as a URL query value (Firestore field
// names can contain spaces/punctuation from AI-recognized item names).
String urlEncode(const String& s) {
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

// Firestore field-path segments containing anything outside [a-zA-Z0-9_] (e.g.
// spaces in "Greek Yogurt") must be wrapped in backticks, with any backtick
// or backslash inside escaped — otherwise updateMask.fieldPaths is rejected
// as an invalid argument.
String quoteFieldPath(const String& name) {
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

// ============================================================================
// Bought items tracking
// Increments counters in fridges/{fridge}/bought/{YYYY-MM} for every item the
// user just bought: either a name that wasn't in the previous scan at all,
// or an existing item whose quantity went up (more units bought).
// ============================================================================
// Increments fridges/{fridge}/bought/{month} counters — shared across all
// roofs since this is a fridge-wide purchase analytic, not roof-specific.
void saveBoughtItems(DynamicJsonDocument& old_doc, JsonDocument& new_items_doc) {
  if (WiFi.status() != WL_CONNECTED) return;

  JsonArray new_items = new_items_doc["items"].as<JsonArray>();

  JsonArray old_items = old_doc["fields"]["items"]["arrayValue"]["values"];

  String month_id = getMonthId();
  String url = "https://firestore.googleapis.com/v1/projects/" + String(FIREBASE_PROJECT_ID) +
               "/databases/(default)/documents/fridges/" + String(FRIDGE_ID) +
               "/bought/" + month_id + "?key=" + String(FIREBASE_API_KEY);

  // First read existing counts for this month.
  DynamicJsonDocument bought_doc(4096);
  bool has_bought = false;
  {
    HTTPClient http;
    http.begin(url);
    if (http.GET() == 200)
      has_bought = !deserializeJson(bought_doc, http.getString());
    http.end();
  }

  bool any_new = false;
  StaticJsonDocument<2048> patch_doc;
  JsonObject patch_fields = patch_doc.createNestedObject("fields");
  String mask_params = "";

  for (JsonObject new_item : new_items) {
    String new_name = new_item["name"].as<String>();
    if (new_name.length() == 0) continue;
    int new_qty = new_item["quantity"].as<String>().toInt();
    if (new_qty <= 0) new_qty = 1;

    // Find the matching item (by name) in the previous scan, if any.
    bool found = false;
    int old_qty = 0;
    for (JsonObject old_item : old_items) {
      JsonObject of = old_item["mapValue"]["fields"];
      String old_name = of["name"]["stringValue"].as<String>();
      if (old_name.equalsIgnoreCase(new_name)) {
        found = true;
        old_qty = of["quantity"]["stringValue"].as<String>().toInt();
        break;
      }
    }

    // Brand-new item, or an existing one bought in greater quantity than before.
    bool just_bought = !found || new_qty > old_qty;
    Serial.printf("[BOUGHT] check %s: found=%d old_qty=%d new_qty=%d just_bought=%d\n",
                  new_name.c_str(), found, old_qty, new_qty, just_bought);

    if (just_bought) {
      int prev_count = 0;
      if (has_bought && bought_doc["fields"].containsKey(new_name))
        prev_count = bought_doc["fields"][new_name]["integerValue"].as<int>();
      patch_fields[new_name]["integerValue"] = prev_count + 1;
      any_new = true;
      mask_params += "&updateMask.fieldPaths=" + urlEncode(quoteFieldPath(new_name));
      Serial.printf("[BOUGHT] %s -> %d this month\n", new_name.c_str(), prev_count + 1);
    }
  }

  if (!any_new) return;

  String payload;
  serializeJson(patch_doc, payload);
  HTTPClient http;
  // updateMask.fieldPaths makes this a merge — without it, Firestore replaces
  // the whole document with just the fields in the body, wiping every other
  // item's count that wasn't touched by this scan.
  http.begin(url + mask_params);
  http.addHeader("Content-Type", "application/json");
  int code = http.PATCH(payload);
  String resp = (code != 200 && code != 201) ? http.getString() : "";
  http.end();
  Serial.printf("[BOUGHT] save %s (HTTP %d)%s%s\n",
                (code==200||code==201) ? "OK" : "FAILED", code,
                resp.length() ? " — " : "", resp.c_str());
}

bool saveToFirebase(JsonDocument& items_doc) {
  if (!items_doc.containsKey("items") || WiFi.status() != WL_CONNECTED) return false;

  // First, read this roof's existing document once to detect newly bought
  // items. Expiry dates are NOT tracked here — they live only on the CH
  // board's merged fridges/{fridge}/inventory/current doc (see
  // inventory_merge.h on the CH board), since that's the single source of
  // truth users edit via touch.
  String cur_url = "https://firestore.googleapis.com/v1/projects/" + String(FIREBASE_PROJECT_ID) +
                   "/databases/(default)/documents/fridges/" + String(FRIDGE_ID) +
                   "/inventory/" + INVENTORY_DOC_ID + "?key=" + String(FIREBASE_API_KEY);
  DynamicJsonDocument cur_doc(8192);
  bool has_existing = false;
  {
    HTTPClient http;
    http.begin(cur_url);
    int get_code = http.GET();
    if (get_code == 200) {
      // Default nesting limit (10) is too shallow for this doc shape once
      // per-unit expiries are nested inside each item (fields > items >
      // arrayValue > values > mapValue > fields > expiries > arrayValue >
      // values > stringValue).
      DeserializationError err = deserializeJson(cur_doc, http.getString(), DeserializationOption::NestingLimit(20));
      has_existing = !err;
      if (err) Serial.printf("[BOUGHT] skip — couldn't parse existing inventory doc: %s\n", err.c_str());
    } else {
      Serial.printf("[BOUGHT] skip — no existing inventory doc yet (GET %d)\n", get_code);
    }
    http.end();
  }

  // Detect and record newly bought items before overwriting.
  if (has_existing) saveBoughtItems(cur_doc, items_doc);

  StaticJsonDocument<4096> doc;
  JsonObject fields = doc.createNestedObject("fields");
  fields["updatedAt"]["stringValue"] = getFormattedTimestamp();
  fields["source"]["stringValue"]    = "ESP32-CAM-" INVENTORY_DOC_ID;

  JsonArray values = fields["items"]["arrayValue"].createNestedArray("values");
  for (JsonObject item : items_doc["items"].as<JsonArray>()) {
    JsonObject mf = values.createNestedObject()["mapValue"].createNestedObject("fields");
    mf["name"]["stringValue"]       = item["name"].as<String>();
    mf["quantity"]["stringValue"]   = item["quantity"].as<String>();
    mf["confidence"]["stringValue"] = item["confidence"].as<String>();
  }

  String payload;
  serializeJson(doc, payload);
  HTTPClient http;
  http.begin(cur_url);
  http.addHeader("Content-Type", "application/json");
  int code = http.PATCH(payload);
  http.end();
  Serial.printf("[FIREBASE] save %s (%d items) -> inventory/%s\n",
                (code==200||code==201) ? "OK" : "FAILED",
                (int)items_doc["items"].as<JsonArray>().size(), INVENTORY_DOC_ID);
  return (code == 200 || code == 201);
}

bool saveTemperature(float tempC, float humidity) {
  if (WiFi.status() != WL_CONNECTED) return false;

  StaticJsonDocument<512> doc;
  JsonObject fields = doc.createNestedObject("fields");
  fields["updatedAt"]["stringValue"]   = getFormattedTimestamp();
  fields["temperature"]["doubleValue"] = tempC;
  fields["humidity"]["doubleValue"]    = humidity;
  fields["source"]["stringValue"]      = "ESP32-CAM";

  String payload;
  serializeJson(doc, payload);
  String url = "https://firestore.googleapis.com/v1/projects/" + String(FIREBASE_PROJECT_ID) +
               "/databases/(default)/documents/fridges/" + String(FRIDGE_ID) +
               "/sensors/temperature?key=" + String(FIREBASE_API_KEY);
  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  int code = http.PATCH(payload);
  http.end();
  Serial.printf("[FIREBASE] temperature save %s (%.1f C, %.1f %%)\n",
                (code==200||code==201) ? "OK" : "FAILED", tempC, humidity);
  return (code == 200 || code == 201);
}

bool saveScanHistory(JsonDocument& items_doc) {
  if (!items_doc.containsKey("items")) return false;
  if (items_doc["items"].as<JsonArray>().size() == 0) {
    Serial.println("[FIREBASE] scan skipped — no items detected");
    return false;
  }

  DynamicJsonDocument doc(6144);
  JsonObject fields = doc.createNestedObject("fields");
  fields["timestamp"]["stringValue"] = getFormattedTimestamp();
  fields["weekId"]["stringValue"]    = getWeekId();
  fields["monthId"]["stringValue"]   = getMonthId();
  fields["source"]["stringValue"]    = "ESP32-CAM-" INVENTORY_DOC_ID;

  JsonArray values = fields["items"]["arrayValue"].createNestedArray("values");
  for (JsonObject item : items_doc["items"].as<JsonArray>()) {
    JsonObject mf = values.createNestedObject()["mapValue"].createNestedObject("fields");
    mf["name"]["stringValue"]       = item["name"].as<String>();
    mf["quantity"]["stringValue"]   = item["quantity"].as<String>();
    mf["confidence"]["stringValue"] = item["confidence"].as<String>();
  }

  String payload;
  serializeJson(doc, payload);
  doc.clear();

  String url = String("https://firestore.googleapis.com/v1/projects/") + FIREBASE_PROJECT_ID +
               "/databases/(default)/documents/fridges/" + FRIDGE_ID +
               "/scans/" + getISOTimestamp() + "?key=" + FIREBASE_API_KEY;
  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  int code = http.PATCH(payload);
  http.end();
  return (code == 200 || code == 201);
}
