#pragma once

// ============================================================================
// Monthly shopping statistics — "What did I buy this month?"
// Reads fridges/{FRIDGE_ID}/bought/{YYYY-MM} from Firestore and renders
// a colored bar-chart list on the TFT display.
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
#define MAX_BOUGHT_ITEMS 20

struct BoughtItem {
  String name;
  int    count;
};

static BoughtItem g_bought[MAX_BOUGHT_ITEMS];
static int        g_bought_count = 0;

// ----------------------------------------------------------------------------
// Fetch bought items for the current month from Firestore
// ----------------------------------------------------------------------------
bool fetchBoughtStats() {
  if (WiFi.status() != WL_CONNECTED) return false;

  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  char month_id[8];
  strftime(month_id, sizeof(month_id), "%Y-%m", t);

  String url = "https://firestore.googleapis.com/v1/projects/" +
               String(FIREBASE_PROJECT_ID) +
               "/databases/(default)/documents/fridges/" +
               String(FRIDGE_ID) +
               "/bought/" + String(month_id) +
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

  g_bought_count = 0;
  for (JsonPair kv : fields) {
    if (g_bought_count >= MAX_BOUGHT_ITEMS) break;
    g_bought[g_bought_count].name  = kv.key().c_str();
    g_bought[g_bought_count].count = kv.value()["integerValue"].as<int>();
    g_bought_count++;
  }

  // Sort descending by count (simple bubble sort — small array).
  for (int i = 0; i < g_bought_count - 1; i++) {
    for (int j = 0; j < g_bought_count - i - 1; j++) {
      if (g_bought[j].count < g_bought[j + 1].count) {
        BoughtItem tmp    = g_bought[j];
        g_bought[j]       = g_bought[j + 1];
        g_bought[j + 1]   = tmp;
      }
    }
  }

  Serial.printf("[STATS] %d bought items this month\n", g_bought_count);
  return true;
}

// ----------------------------------------------------------------------------
// Per-item bar color — cycles through a fixed palette so each item is visually
// distinct and stays the same color across re-renders (index-based, not random).
// ----------------------------------------------------------------------------
uint16_t statsBarColor(int index) {
  static const uint16_t palette[] = {
    TFT_CYAN, TFT_ORANGE, TFT_GREENYELLOW, TFT_MAGENTA,
    TFT_YELLOW, TFT_SKYBLUE, TFT_PINK, TFT_GOLD,
    TFT_VIOLET, TFT_DARKCYAN
  };
  return palette[index % (sizeof(palette) / sizeof(palette[0]))];
}

// ----------------------------------------------------------------------------
// Render the stats screen
// ----------------------------------------------------------------------------
void renderStatsScreen() {
  tft.fillScreen(TFT_BLACK);
  int w = tft.width();

  // Header
  tft.fillRect(0, 0, w, HEADER_HEIGHT_PX, TFT_NAVY);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextSize(2);
  tft.drawString("This Month", w / 2, HEADER_HEIGHT_PX / 2);

  // Sub-header
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  char month_label[16];
  strftime(month_label, sizeof(month_label), "%B %Y", t);
  tft.setTextDatum(MR_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(TFT_CYAN, TFT_NAVY);
  tft.drawString(String(month_label), w - SIDE_PADDING_PX, HEADER_HEIGHT_PX / 2);

  // Same back button as the item-detail / expiry-editor page.
  layoutDetailButtons();
  drawBtn(btnBack, "< Back", TFT_DARKGREY);

  if (g_bought_count == 0) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setTextSize(2);
    tft.drawString("No data yet", w / 2, tft.height() / 2 - 10);
    tft.setTextSize(1);
    tft.drawString("Items will appear once", w / 2, tft.height() / 2 + 14);
    tft.drawString("you buy something new", w / 2, tft.height() / 2 + 28);
    return;
  }

  // Find max count for bar scaling.
  int max_count = 1;
  for (int i = 0; i < g_bought_count; i++)
    if (g_bought[i].count > max_count) max_count = g_bought[i].count;

  // Each row is a colored "pill" bar with the item name inside it and the
  // count badge on the right — one color per item instead of one flat color.
  int list_top    = HEADER_HEIGHT_PX + 10;
  int list_bottom = tft.height() - 4;
  int row_h       = 34;
  int row_gap     = 6;
  int bar_min_w   = 64;                          // enough to fit the name
  int bar_max_w   = w - SIDE_PADDING_PX * 2 - 50; // 50px reserved for count badge
  int rows        = min(g_bought_count, (list_bottom - list_top) / (row_h + row_gap));

  for (int i = 0; i < rows; i++) {
    int y = list_top + i * (row_h + row_gap);
    uint16_t color = statsBarColor(i);

    int bar_w = bar_min_w + (int)((long)(g_bought[i].count) * (bar_max_w - bar_min_w) / max_count);
    int bar_x = SIDE_PADDING_PX;

    tft.fillRoundRect(bar_x, y, bar_w, row_h, 8, color);

    // Item name inside the bar — truncated so it never spills past the bar
    // edge into the count badge drawn just to the right of it.
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(TFT_BLACK, color);
    tft.setTextSize(1);
    tft.drawString(truncateItemName(g_bought[i].name), bar_x + 10, y + row_h / 2);

    // Count badge to the right of the bar.
    String count_str = String(g_bought[i].count) + "x";
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(color, TFT_BLACK);
    tft.setTextSize(2);
    tft.drawString(count_str, bar_x + bar_w + 8, y + row_h / 2);
  }

  if (g_bought_count > rows) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.setTextSize(1);
    tft.drawString("+ " + String(g_bought_count - rows) + " more",
                   w / 2, list_bottom - 6);
  }
}
