#pragma once

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "parameters.h"
#include "SECRETS.h"

// ============================================================================
// Merges the per-roof scan docs (fridges/{FRIDGE_ID}/inventory/roof1..roofN,
// written independently by each ESP32-CAM board — see CAMERA_ROOF in the CAM
// sketch) into the single fridges/{FRIDGE_ID}/inventory/current doc that the
// rest of this board (display, touch-edit, GM65 barcode scans) already
// reads/writes. Items with the same name (case-insensitive) across roofs are
// combined by summing quantity; expiry dates already recorded against an
// item in /current are preserved untouched, since CAM boards never write
// expiries themselves (this board is the only place expiries get entered).
//
// An item that was in /current before this merge but that no roof reports
// this cycle is dropped — the cameras no longer see it, so it's treated as
// used up/discarded. The one exception is items tagged "source":"gm65" (see
// gm65.h's addScannedItemToInventory()): barcode-scanned items are never
// implicitly deleted by a merge, since a camera may never actually see them
// (e.g. a small/hidden item), only an explicit future action should remove
// them. That source tag is propagated forward on every merge so it isn't
// lost the next cycle even if the item also happens to be camera-visible.
// ============================================================================

struct MergedItem {
  String name;
  int    quantity;
  String confidence;
};

static int confidenceRank(const String& c) {
  if (c.equalsIgnoreCase("high"))   return 3;
  if (c.equalsIgnoreCase("medium")) return 2;
  if (c.equalsIgnoreCase("low"))    return 1;
  return 0;
}

bool mergeRoofInventories() {
  if (WiFi.status() != WL_CONNECTED) return false;

  MergedItem merged[MAX_ITEMS_DISPLAYED];
  int merged_count = 0;

  for (int roof = 1; roof <= NUM_ROOFS; roof++) {
    String url =
      "https://firestore.googleapis.com/v1/projects/" + String(FIREBASE_PROJECT_ID) +
      "/databases/(default)/documents/fridges/" + String(FRIDGE_ID) +
      "/inventory/roof" + String(roof) + "?key=" + String(FIREBASE_API_KEY);

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(10000);
    if (!http.begin(client, url)) continue;
    int code = http.GET();
    String body = http.getString();
    http.end();
    if (code != 200) continue;

    DynamicJsonDocument doc(8192);
    if (deserializeJson(doc, body, DeserializationOption::NestingLimit(20))) continue;

    JsonArray items = doc["fields"]["items"]["arrayValue"]["values"];
    for (JsonObject v : items) {
      JsonObject mf = v["mapValue"]["fields"];
      String name = mf["name"]["stringValue"].as<String>();
      if (name.length() == 0) continue;
      int qty = mf["quantity"]["stringValue"].as<String>().toInt();
      if (qty <= 0) qty = 1;
      String conf = mf["confidence"]["stringValue"].as<String>();

      int idx = -1;
      for (int i = 0; i < merged_count; i++) {
        if (merged[i].name.equalsIgnoreCase(name)) { idx = i; break; }
      }
      if (idx >= 0) {
        merged[idx].quantity += qty;
        if (confidenceRank(conf) > confidenceRank(merged[idx].confidence))
          merged[idx].confidence = conf;
      } else if (merged_count < MAX_ITEMS_DISPLAYED) {
        merged[merged_count].name       = name;
        merged[merged_count].quantity   = qty;
        merged[merged_count].confidence = conf;
        merged_count++;
      }
    }
  }

  // Fetch the existing merged doc so user-entered expiry dates survive.
  String cur_url =
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
    if (http.begin(client, cur_url)) {
      int code = http.GET();
      if (code == 200)
        has_existing = !deserializeJson(cur_doc, http.getString(), DeserializationOption::NestingLimit(20));
      http.end();
    }
  }

  DynamicJsonDocument out_doc(8192);
  JsonObject fields = out_doc["fields"].to<JsonObject>();
  time_t now = time(nullptr);
  char ts[20];
  strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&now));
  fields["updatedAt"]["stringValue"] = ts;
  fields["source"]["stringValue"]    = "ESP32-CH-merge";

  JsonArray values = fields["items"]["arrayValue"]["values"].to<JsonArray>();
  for (int i = 0; i < merged_count; i++) {
    JsonObject out = values.createNestedObject()["mapValue"]["fields"].to<JsonObject>();
    out["name"]["stringValue"]       = merged[i].name;
    out["quantity"]["stringValue"]   = String(merged[i].quantity);
    out["confidence"]["stringValue"] = merged[i].confidence;

    if (has_existing) {
      JsonArray existing_items = cur_doc["fields"]["items"]["arrayValue"]["values"];
      for (JsonObject ev : existing_items) {
        JsonObject emf = ev["mapValue"]["fields"];
        if (!String(emf["name"]["stringValue"].as<String>()).equalsIgnoreCase(merged[i].name)) continue;
        JsonArray ea = emf["expiries"]["arrayValue"]["values"];
        if (ea.size() > 0) {
          JsonArray out_ea = out["expiries"]["arrayValue"]["values"].to<JsonArray>();
          for (JsonObject ed : ea)
            out_ea.createNestedObject()["stringValue"] = ed["stringValue"].as<String>();
        }
        // Propagate the "gm65" source tag forward so a barcode-scanned item
        // that's also currently camera-visible doesn't lose its
        // never-auto-delete protection the next time the camera stops
        // seeing it.
        String src = emf["source"]["stringValue"].as<String>();
        if (src.equalsIgnoreCase("gm65")) out["source"]["stringValue"] = "gm65";
        break;
      }
    }
  }

  // Carry over items already in /current that no roof reported this cycle
  // ONLY if they're tagged "source":"gm65" (barcode scans, which write
  // straight to /current and never appear in a roof doc — a camera may never
  // see them at all, so they must not be implicitly deleted just because no
  // roof mentions them). Anything else not reported by any roof this cycle
  // is treated as used up/discarded and dropped.
  if (has_existing) {
    JsonArray existing_items = cur_doc["fields"]["items"]["arrayValue"]["values"];
    for (JsonObject ev : existing_items) {
      JsonObject emf = ev["mapValue"]["fields"];
      String name = emf["name"]["stringValue"].as<String>();
      if (name.length() == 0) continue;
      if (!String(emf["source"]["stringValue"].as<String>()).equalsIgnoreCase("gm65")) continue;

      bool already_merged = false;
      for (int i = 0; i < merged_count; i++) {
        if (merged[i].name.equalsIgnoreCase(name)) { already_merged = true; break; }
      }
      if (already_merged) continue;

      JsonObject out = values.createNestedObject()["mapValue"]["fields"].to<JsonObject>();
      out["name"]["stringValue"]       = name;
      out["quantity"]["stringValue"]   = emf["quantity"]["stringValue"].as<String>();
      out["confidence"]["stringValue"] = emf["confidence"]["stringValue"].as<String>();
      out["source"]["stringValue"]     = "gm65";

      JsonArray ea = emf["expiries"]["arrayValue"]["values"];
      if (ea.size() > 0) {
        JsonArray out_ea = out["expiries"]["arrayValue"]["values"].to<JsonArray>();
        for (JsonObject ed : ea)
          out_ea.createNestedObject()["stringValue"] = ed["stringValue"].as<String>();
      }
    }
  }

  String payload;
  serializeJson(out_doc, payload);
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(10000);
  if (!http.begin(client, cur_url)) return false;
  http.addHeader("Content-Type", "application/json");
  int code = http.PATCH(payload);
  http.end();
  Serial.printf("[MERGE] %d roofs -> current %s (HTTP %d, %d items)\n",
                NUM_ROOFS, (code == 200 || code == 201) ? "OK" : "FAILED", code, merged_count);
  return (code == 200 || code == 201);
}
