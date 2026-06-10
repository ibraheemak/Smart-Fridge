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
bool saveToFirebase(JsonDocument& items_doc) {
  if (!items_doc.containsKey("items") || WiFi.status() != WL_CONNECTED) return false;

  StaticJsonDocument<4096> doc;
  JsonObject fields = doc.createNestedObject("fields");
  fields["updatedAt"]["stringValue"] = getFormattedTimestamp();
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
  String url = "https://firestore.googleapis.com/v1/projects/" + String(FIREBASE_PROJECT_ID) +
               "/databases/(default)/documents/fridges/" + String(FRIDGE_ID) +
               "/inventory/current?key=" + String(FIREBASE_API_KEY);
  HTTPClient http;
  http.begin(url);
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
