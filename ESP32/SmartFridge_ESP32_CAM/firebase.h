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
               "/inventory/current?key=" + String(FIREBASE_API_KEY);
  HTTPClient http;
  http.begin(url);
  int code = http.GET();
  if (code != 200) { http.end(); return ""; }
  String body = http.getString();
  http.end();

  DynamicJsonDocument doc(8192);
  if (deserializeJson(doc, body)) return "";

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

// ============================================================================
// Consumed items tracking
// Increments counters in fridges/{fridge}/consumed/{YYYY-MM} for every item
// that is in the new scan but wasn't in the previous one — i.e. an item the
// user just bought, not one they used up.
// ============================================================================
void saveConsumedItems(DynamicJsonDocument& old_doc, JsonDocument& new_items_doc) {
  if (WiFi.status() != WL_CONNECTED) return;

  JsonArray new_items = new_items_doc["items"].as<JsonArray>();

  JsonArray old_items = old_doc["fields"]["items"]["arrayValue"]["values"];

  String month_id = getMonthId();
  String url = "https://firestore.googleapis.com/v1/projects/" + String(FIREBASE_PROJECT_ID) +
               "/databases/(default)/documents/fridges/" + String(FRIDGE_ID) +
               "/consumed/" + month_id + "?key=" + String(FIREBASE_API_KEY);

  // First read existing counts for this month.
  DynamicJsonDocument consumed_doc(4096);
  bool has_consumed = false;
  {
    HTTPClient http;
    http.begin(url);
    if (http.GET() == 200)
      has_consumed = !deserializeJson(consumed_doc, http.getString());
    http.end();
  }

  bool any_new = false;
  StaticJsonDocument<2048> patch_doc;
  JsonObject patch_fields = patch_doc.createNestedObject("fields");

  for (JsonObject new_item : new_items) {
    String new_name = new_item["name"].as<String>();
    if (new_name.length() == 0) continue;

    // Check if this item already existed in the previous scan.
    bool existed_before = false;
    for (JsonObject old_item : old_items) {
      String old_name = old_item["mapValue"]["fields"]["name"]["stringValue"].as<String>();
      if (old_name.equalsIgnoreCase(new_name)) {
        existed_before = true;
        break;
      }
    }

    if (!existed_before) {
      // Newly bought item — bump (or start) its counter for this month.
      int prev_count = 0;
      if (has_consumed && consumed_doc["fields"].containsKey(new_name))
        prev_count = consumed_doc["fields"][new_name]["integerValue"].as<int>();
      patch_fields[new_name]["integerValue"] = prev_count + 1;
      any_new = true;
      Serial.printf("[CONSUMED] %s -> %d this month\n", new_name.c_str(), prev_count + 1);
    }
  }

  if (!any_new) return;

  String payload;
  serializeJson(patch_doc, payload);
  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  int code = http.PATCH(payload);
  http.end();
  Serial.printf("[CONSUMED] save %s\n", (code==200||code==201) ? "OK" : "FAILED");
}

bool saveToFirebase(JsonDocument& items_doc) {
  if (!items_doc.containsKey("items") || WiFi.status() != WL_CONNECTED) return false;

  // First, read the whole current document once so we can preserve expiries
  // and detect consumed items.
  String cur_url = "https://firestore.googleapis.com/v1/projects/" + String(FIREBASE_PROJECT_ID) +
                   "/databases/(default)/documents/fridges/" + String(FRIDGE_ID) +
                   "/inventory/current?key=" + String(FIREBASE_API_KEY);
  DynamicJsonDocument cur_doc(8192);
  bool has_existing = false;
  {
    HTTPClient http;
    http.begin(cur_url);
    if (http.GET() == 200) {
      has_existing = !deserializeJson(cur_doc, http.getString());
    }
    http.end();
  }

  // Detect and record consumed items before overwriting.
  if (has_existing) saveConsumedItems(cur_doc, items_doc);

  StaticJsonDocument<4096> doc;
  JsonObject fields = doc.createNestedObject("fields");
  fields["updatedAt"]["stringValue"] = getFormattedTimestamp();
  fields["source"]["stringValue"]    = "ESP32-CAM";

  JsonArray values = fields["items"]["arrayValue"].createNestedArray("values");
  for (JsonObject item : items_doc["items"].as<JsonArray>()) {
    String item_name = item["name"].as<String>();
    JsonObject mf = values.createNestedObject()["mapValue"].createNestedObject("fields");
    mf["name"]["stringValue"]       = item_name;
    mf["quantity"]["stringValue"]   = item["quantity"].as<String>();
    mf["confidence"]["stringValue"] = item["confidence"].as<String>();

    // Preserve any expiry dates the user already entered for this item.
    if (has_existing) {
      JsonArray existing_items = cur_doc["fields"]["items"]["arrayValue"]["values"];
      for (JsonObject ev : existing_items) {
        JsonObject emf = ev["mapValue"]["fields"];
        if (!String(emf["name"]["stringValue"].as<String>()).equalsIgnoreCase(item_name)) continue;

        // Copy expiries array.
        JsonArray ea = emf["expiries"]["arrayValue"]["values"];
        if (ea.size() > 0) {
          JsonArray out_ea = mf["expiries"]["arrayValue"].createNestedArray("values");
          for (JsonObject ed : ea)
            out_ea.createNestedObject()["stringValue"] = ed["stringValue"].as<String>();
        }
        break;
      }
    }
  }

  String payload;
  serializeJson(doc, payload);
  HTTPClient http;
  http.begin(cur_url);
  http.addHeader("Content-Type", "application/json");
  int code = http.PATCH(payload);
  http.end();
  Serial.printf("[FIREBASE] save %s (%d items)\n",
                (code==200||code==201) ? "OK" : "FAILED",
                (int)items_doc["items"].as<JsonArray>().size());
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
  fields["source"]["stringValue"]    = "ESP32-CAM";

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
