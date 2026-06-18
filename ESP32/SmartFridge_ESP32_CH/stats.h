#pragma once

// ============================================================================
// Monthly consumption statistics — "What did I use this month?"
// Reads fridges/{FRIDGE_ID}/consumed/{YYYY-MM} from Firestore and renders
// a sorted bar-chart list on the TFT display.
// ============================================================================

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "display.h"
#include "parameters.h"
#include "SECRETS.h"

// ----------------------------------------------------------------------------
// Data
// ----------------------------------------------------------------------------
#define MAX_CONSUMED_ITEMS 20

struct ConsumedItem {
  String name;
  int    count;
};

static ConsumedItem g_consumed[MAX_CONSUMED_ITEMS];
static int          g_consumed_count = 0;

// ----------------------------------------------------------------------------
// Fetch consumed items for the current month from Firestore
// ----------------------------------------------------------------------------
bool fetchConsumedStats() {
  if (WiFi.status() != WL_CONNECTED) return false;

  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  char month_id[8];
  strftime(month_id, sizeof(month_id), "%Y-%m", t);

  String url = "https://firestore.googleapis.com/v1/projects/" +
               String(FIREBASE_PROJECT_ID) +
               "/databases/(default)/documents/fridges/" +
               String(FRIDGE_ID) +
               "/consumed/" + String(month_id) +
               "?key=" + String(FIREBASE_API_KEY);

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(10000);
  if (!http.begin(client, url)) return false;

  int code = http.GET();
  if (code != 200) { http.end(); return false; }

  String body = http.getString();
  http.end();

  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, body)) return false;

  JsonObject fields = doc["fields"];
  if (fields.isNull()) return false;

  g_consumed_count = 0;
  for (JsonPair kv : fields) {
    if (g_consumed_count >= MAX_CONSUMED_ITEMS) break;
    g_consumed[g_consumed_count].name  = kv.key().c_str();
    g_consumed[g_consumed_count].count = kv.value()["integerValue"].as<int>();
    g_consumed_count++;
  }

  // Sort descending by count (simple bubble sort — small array).
  for (int i = 0; i < g_consumed_count - 1; i++) {
    for (int j = 0; j < g_consumed_count - i - 1; j++) {
      if (g_consumed[j].count < g_consumed[j + 1].count) {
        ConsumedItem tmp   = g_consumed[j];
        g_consumed[j]      = g_consumed[j + 1];
        g_consumed[j + 1]  = tmp;
      }
    }
  }

  Serial.printf("[STATS] %d consumed items this month\n", g_consumed_count);
  return true;
}

// ----------------------------------------------------------------------------
// Render the stats screen
// ----------------------------------------------------------------------------
void renderStatsScreen() {
  tft.fillScreen(TFT_BLACK);
  int w = tft.width();

  // Header
  tft.fillRect(0, 0, w, HEADER_HEIGHT_PX, TFT_NAVY);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextSize(2);
  tft.drawString("This Month", SIDE_PADDING_PX, HEADER_HEIGHT_PX / 2);

  // Sub-header
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  char month_label[16];
  strftime(month_label, sizeof(month_label), "%B %Y", t);
  tft.setTextDatum(MR_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(TFT_CYAN, TFT_NAVY);
  tft.drawString(String(month_label), w - SIDE_PADDING_PX, HEADER_HEIGHT_PX / 2);

  if (g_consumed_count == 0) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setTextSize(2);
    tft.drawString("No data yet", w / 2, tft.height() / 2 - 10);
    tft.setTextSize(1);
    tft.drawString("Items will appear after", w / 2, tft.height() / 2 + 14);
    tft.drawString("products are consumed", w / 2, tft.height() / 2 + 28);
    drawFooter("< Back");
    return;
  }

  // Find max count for bar scaling.
  int max_count = 1;
  for (int i = 0; i < g_consumed_count; i++)
    if (g_consumed[i].count > max_count) max_count = g_consumed[i].count;

  // Draw rows.
  int list_top    = HEADER_HEIGHT_PX + 6;
  int list_bottom = tft.height() - FOOTER_HEIGHT_PX - 4;
  int row_h       = 36;
  int bar_max_w   = w - SIDE_PADDING_PX * 2 - 80;  // 80px reserved for name + count label
  int rows        = min(g_consumed_count, (list_bottom - list_top) / row_h);

  for (int i = 0; i < rows; i++) {
    int y      = list_top + i * row_h;
    uint16_t bg = (i % 2 == 0) ? TFT_BLACK : 0x18E3;
    tft.fillRect(0, y, w, row_h - 2, bg);

    // Item name (left).
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(TFT_WHITE, bg);
    tft.setTextSize(1);
    tft.drawString(g_consumed[i].name, SIDE_PADDING_PX, y + row_h / 2);

    // Count label (right).
    String count_str = String(g_consumed[i].count) + "x";
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(TFT_CYAN, bg);
    tft.drawString(count_str, w - SIDE_PADDING_PX, y + row_h / 2);

    // Bar.
    int bar_w = (int)((long)g_consumed[i].count * bar_max_w / max_count);
    int bar_x = SIDE_PADDING_PX + 90;
    int bar_y = y + row_h / 2 - 6;
    tft.fillRect(bar_x, bar_y, bar_w, 12, TFT_DARKCYAN);
    tft.drawRect(bar_x, bar_y, bar_max_w, 12, TFT_DARKGREY);
  }

  if (g_consumed_count > rows) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.setTextSize(1);
    tft.drawString("+ " + String(g_consumed_count - rows) + " more",
                   w / 2, list_bottom);
  }

  drawFooter("< Back to inventory");
}
