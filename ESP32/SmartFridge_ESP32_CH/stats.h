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
// Not a hard technical limit — just a fixed-array cap. The stats screen
// already scrolls (see computeStatsLayout()'s up/down arrows below), so this
// only needs to be large enough that a real month's worth of distinct
// groceries never gets truncated; each entry is a single String + int, so
// raising it costs negligible RAM on this board.
#define MAX_BOUGHT_ITEMS 60

struct BoughtItem {
  String name;
  int    count;
};

// Not static: touch.h (included before this file) forward-declares
// g_bought_count as extern so handleTouch() can page the scroll buttons.
BoughtItem g_bought[MAX_BOUGHT_ITEMS];
int        g_bought_count = 0;

// Scroll offset into g_bought[], mirroring g_list_scroll for the inventory
// list — set by the up/down scroll buttons in touch.h, reset to 0 whenever
// stats are (re)loaded so the screen always opens at the top.
int g_stats_scroll = 0;

// ----------------------------------------------------------------------------
// Fetch bought items for the current month from Firestore
// ----------------------------------------------------------------------------
bool fetchBoughtStats() {
  g_stats_scroll = 0;  // always (re)open the stats screen scrolled to the top
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
  prepSecureClient(client);
  HTTPClient http;
  http.setTimeout(10000);
  if (!http.begin(client, url)) return false;

  int code = http.GET();
  if (code != 200) { http.end(); return false; }

  String body = http.getString();
  http.end();

  // Sized for MAX_BOUGHT_ITEMS real product names, not just 20 — 4096 was
  // already tight for a month with many distinct purchases.
  DynamicJsonDocument doc(8192);
  if (deserializeJson(doc, body)) return false;

  JsonObject fields = doc["fields"];
  if (fields.isNull()) return false;

  // Firestore's REST API returns a map's fields sorted alphabetically by
  // name, not by count — so simply taking the first MAX_BOUGHT_ITEMS fields
  // encountered would silently drop whichever items happen to sort late
  // (e.g. "Original Oatcakes" behind a month with 20+ earlier-alphabet
  // purchases), even though they're genuinely in the document. Once the
  // array fills up, replace the current-smallest count instead of just
  // stopping, so the on-device list always holds the true top
  // MAX_BOUGHT_ITEMS by count regardless of field iteration order.
  g_bought_count = 0;
  for (JsonPair kv : fields) {
    int count = kv.value()["integerValue"].as<int>();
    if (g_bought_count < MAX_BOUGHT_ITEMS) {
      g_bought[g_bought_count].name  = kv.key().c_str();
      g_bought[g_bought_count].count = count;
      g_bought_count++;
    } else {
      int min_idx = 0;
      for (int i = 1; i < g_bought_count; i++)
        if (g_bought[i].count < g_bought[min_idx].count) min_idx = i;
      if (count > g_bought[min_idx].count) {
        g_bought[min_idx].name  = kv.key().c_str();
        g_bought[min_idx].count = count;
      }
    }
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
// Layout of the scrollable stats list — shared by renderStatsScreen()
// (drawing) and handleTouch() (up/down button paging), same pattern as
// computeListLayout() for the inventory screen in display.h.
// ----------------------------------------------------------------------------
#define STATS_ROW_H   34
#define STATS_ROW_GAP  6

StatsLayout computeStatsLayout() {
  StatsLayout l;
  l.top    = HEADER_HEIGHT_PX + 10;
  l.bottom = tft.height() - 4;

  int remaining = max(0, g_bought_count - g_stats_scroll);
  int step = STATS_ROW_H + STATS_ROW_GAP;

  l.show_up = g_stats_scroll > 0;
  int avail = l.bottom - l.top - (l.show_up ? SCROLL_ARROW_H : 0);
  int capacity = max(0, avail / step);
  l.show_down = remaining > capacity;
  if (l.show_down) {
    capacity = max(0, (avail - SCROLL_ARROW_H) / step);
    l.show_down = remaining > capacity;
  }
  l.rows_visible = min(capacity, remaining);

  // Fixed step for the up/down buttons — see the matching comment in
  // computeListLayout() (display.h): rows_visible shrinks on a partial last
  // page, and paging back up by that smaller count would land short and
  // re-show items already seen.
  l.page_step = max(1, (l.bottom - l.top - 2 * SCROLL_ARROW_H) / step);

  l.up_y    = l.top;
  l.rows_y0 = l.top + (l.show_up ? SCROLL_ARROW_H : 0);
  l.down_y  = l.rows_y0 + l.rows_visible * step;
  return l;
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

  // Clamp scroll in case the list shrank since the last render (e.g. a
  // re-fetch returned fewer items) — same reasoning as renderInventory()'s
  // clamp loop in display.h.
  while (g_stats_scroll > 0 && computeStatsLayout().rows_visible == 0) g_stats_scroll--;

  StatsLayout l = computeStatsLayout();

  // Each row is a colored "pill" bar with the item name inside it and the
  // count badge on the right — one color per item instead of one flat color.
  int bar_min_w = 64;                          // enough to fit the name
  int bar_max_w = w - SIDE_PADDING_PX * 2 - 50; // 50px reserved for count badge

  if (l.show_up) drawScrollArrow(l.up_y, true);

  for (int i = 0; i < l.rows_visible; i++) {
    int idx = g_stats_scroll + i;
    int y = l.rows_y0 + i * (STATS_ROW_H + STATS_ROW_GAP);
    uint16_t color = statsBarColor(idx);

    int bar_w = bar_min_w + (int)((long)(g_bought[idx].count) * (bar_max_w - bar_min_w) / max_count);
    int bar_x = SIDE_PADDING_PX;

    tft.fillRoundRect(bar_x, y, bar_w, STATS_ROW_H, 8, color);

    // Item name inside the bar — truncated so it never spills past the bar
    // edge into the count badge drawn just to the right of it.
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(TFT_BLACK, color);
    tft.setTextSize(1);
    drawItemName(g_bought[idx].name, bar_x + 10, y + STATS_ROW_H / 2, bar_w - 20);

    // Count badge to the right of the bar.
    String count_str = String(g_bought[idx].count) + "x";
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(color, TFT_BLACK);
    tft.setTextSize(2);
    tft.drawString(count_str, bar_x + bar_w + 8, y + STATS_ROW_H / 2);
  }

  if (l.show_down) drawScrollArrow(l.down_y, false);
}
