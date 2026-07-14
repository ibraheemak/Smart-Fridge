#pragma once

// ============================================================================
// Recipe recommendations (US #7)
//
// Sends the current in-memory inventory (g_items[], populated by
// fetchInventory() in the .ino) as a text-only prompt to Gemini and renders
// up to MAX_RECIPES suggestions on a list screen, opened from the home
// "RECIPES" tile. The user picks filter chips first (Breakfast/Lunch/
// Healthy/Quick), then taps "Get Recommendations" to actually query Gemini —
// entering the screen or toggling a chip never fires a request on its own.
// Tapping a suggestion opens a detail view with the full steps, one per
// line, with scroll arrows if they don't all fit.
//
// Unlike the CAM board's gemini.h (SmartFridge_ESP32_CAM/gemini.h), this is a
// text-only prompt — no image/base64 payload — so the request/response
// handling is written directly here rather than shared with the CAM sketch
// (the two boards are separate .ino files with no shared build).
// ============================================================================

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "display.h"
#include "parameters.h"
#include "SECRETS.h"

struct RecipeItem {
  String name;
  String summary;
  String steps;   // individual steps joined by '\n' — see fetchRecipes()
};

RecipeItem g_recipes[MAX_RECIPES];
int        g_recipe_count = 0;
int        g_recipes_scroll = 0;
int        g_recipe_detail_index = -1;
// Shown centered on the list screen whenever g_recipe_count == 0 — why there's
// nothing to show yet (not queried, offline, empty fridge, Gemini error)
// rather than a blank list.
String     g_recipes_status = "Choose your filters, then tap Get Recommendations";

// Filter chips (tap to select) — mutually exclusive, like radio buttons, so
// there's exactly one active mode at a time (or none). Folded into the Gemini
// prompt below. Not persisted across reboots. Selecting one just updates the
// chip and clears any stale results — it does NOT re-query; the user taps
// "Get Recommendations" to do that explicitly (see onGetRecommendationsPressed()).
enum RecipeFilter { FILTER_NONE, FILTER_BREAKFAST, FILTER_LUNCH, FILTER_HEALTHY, FILTER_QUICK };
RecipeFilter g_selected_filter = FILTER_NONE;

#define MAX_STEPS_PER_RECIPE  8

// ----------------------------------------------------------------------------
// Detail-screen state — declared up here (not next to drawRecipeDetail()
// further down) because handleRecipesTouch() resets g_detail_scroll when
// opening a recipe, and a plain global (not just the extern in touch.h) must
// be declared before its first use in this translation unit.
// ----------------------------------------------------------------------------
#define MAX_DETAIL_LINES  40
#define DETAIL_LINE_H     26

String g_detail_lines[MAX_DETAIL_LINES];
int    g_detail_line_count = 0;
int    g_detail_scroll     = 0;

struct DetailLayout {
  bool show_up;   int up_y;
  int  rows_y0;   int rows_visible;
  bool show_down; int down_y;
  int  page_step;
};
DetailLayout g_detail_layout;

// ----------------------------------------------------------------------------
// Word-wraps text into left-aligned lines at the current text size/font,
// breaking on spaces and hard-cutting a single word that's wider than
// maxWidth on its own. Writes into outLines[0..count) and returns how many lines
// it wrote (capped at maxOutLines) — used both for the summary (drawn
// directly) and, via wrapIntoLines(), for individual recipe steps.
// ----------------------------------------------------------------------------
int wrapIntoLines(const String& text, int maxWidth, String* outLines, int maxOutLines) {
  int n = text.length();
  int start = 0, count = 0;
  while (start < n && count < maxOutLines) {
    int cut = start;
    for (int i = start; i <= n; i++) {
      if (i == n || text[i] == ' ') {
        if (tft.textWidth(text.substring(start, i)) > maxWidth) break;
        cut = i;
        if (i == n) break;
      }
    }
    if (cut == start) {
      // A single word wider than maxWidth — hard-cut it so the loop still advances.
      cut = start + 1;
      while (cut < n && tft.textWidth(text.substring(start, cut)) <= maxWidth) cut++;
    }
    outLines[count++] = text.substring(start, cut);
    start = cut;
    while (start < n && text[start] == ' ') start++;
  }
  return count;
}

int drawWrappedText(const String& text, int x, int y, int maxWidth, int lineHeight, int maxLines) {
  String lines[8];
  int n = min(maxLines, 8);
  int count = wrapIntoLines(text, maxWidth, lines, n);
  tft.setTextDatum(TL_DATUM);
  for (int i = 0; i < count; i++) tft.drawString(lines[i], x, y + i * lineHeight);
  return count;
}

// Shrinks text with a trailing "..." until it fits maxWidthPx at the
// current text size — caller must setTextSize()/font before calling.
String truncateToWidth(const String& text, int maxWidthPx) {
  if (tft.textWidth(text) <= maxWidthPx) return text;
  String s = text;
  while (s.length() > 0 && tft.textWidth(s + "...") > maxWidthPx) s.remove(s.length() - 1);
  return s + "...";
}

// ----------------------------------------------------------------------------
// Fetch — build an ingredient list from the current inventory and ask Gemini
// for a handful of recipes using mostly what's on hand.
// ----------------------------------------------------------------------------
String buildIngredientList() {
  String list;
  for (int i = 0; i < g_item_count; i++) {
    if (list.length() > 0) list += ", ";
    list += g_items[i].name;
  }
  return list;
}

// Builds the "only suggest recipes that are X" clause for whichever single
// filter chip is selected — empty string (no clause added) if none is.
String buildFilterInstruction() {
  switch (g_selected_filter) {
    case FILTER_BREAKFAST: return "Only suggest recipes suitable for breakfast. ";
    case FILTER_LUNCH:     return "Only suggest recipes suitable for lunch. ";
    case FILTER_HEALTHY:   return "Only suggest recipes that are healthy and low-calorie. ";
    case FILTER_QUICK:     return "Only suggest recipes that are quick, ready in 15 minutes or less. ";
    default:               return "";
  }
}

bool fetchRecipes() {
  g_recipe_count   = 0;
  g_recipes_scroll = 0;
  g_recipes_status = "";

  if (WiFi.status() != WL_CONNECTED) { g_recipes_status = "No WiFi connection"; return false; }

  String ingredients = buildIngredientList();
  if (ingredients.length() == 0) { g_recipes_status = "Fridge is empty - scan some items first"; return false; }

  String prompt =
    "I have these ingredients in my fridge: " + ingredients + ". " +
    buildFilterInstruction() +
    "Suggest up to " + String(MAX_RECIPES) + " simple recipes using mostly these "
    "ingredients (common pantry staples like salt, oil, sugar, water are fine "
    "too). Return valid JSON only, no markdown fences: {\"recipes\":[{\"name\":"
    "\"recipe name\",\"summary\":\"one short sentence\",\"steps\":[\"step 1 "
    "text\",\"step 2 text\"]}]} — each array entry in \"steps\" must be exactly "
    "one instruction step, not the whole method in one string.";

  DynamicJsonDocument reqDoc(4096);
  JsonArray contents = reqDoc["contents"].to<JsonArray>();
  JsonObject content0 = contents.createNestedObject();
  JsonArray parts = content0["parts"].to<JsonArray>();
  parts.createNestedObject()["text"] = prompt;
  String body;
  serializeJson(reqDoc, body);

  String url = String(GEMINI_API_ENDPOINT) + "?key=" + GEMINI_API_KEY;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(GEMINI_RECIPES_TIMEOUT_MS);
  if (!http.begin(client, url)) { g_recipes_status = "Request failed"; return false; }
  http.addHeader("Content-Type", "application/json");

  int code = http.POST(body);
  if (code != 200) {
    Serial.printf("[RECIPES] Gemini HTTP %d\n", code);
    http.end();
    g_recipes_status = "Could not reach Gemini (HTTP " + String(code) + ")";
    return false;
  }
  String response = http.getString();
  http.end();

  StaticJsonDocument<8192> full;
  if (deserializeJson(full, response)) { g_recipes_status = "Bad response from Gemini"; return false; }
  const char* text = full["candidates"][0]["content"]["parts"][0]["text"];
  if (!text) { g_recipes_status = "No suggestions returned"; return false; }

  // Strip a ```json ... ``` fence if Gemini added one despite the prompt,
  // then trim to the outermost {...} — mirrors parseGeminiResponse() in the
  // CAM board's gemini.h.
  String json_text = String(text);
  json_text.trim();
  if (json_text.startsWith("```")) {
    int nl = json_text.indexOf('\n');
    if (nl >= 0) json_text = json_text.substring(nl + 1);
    int fence = json_text.lastIndexOf("```");
    if (fence >= 0) json_text = json_text.substring(0, fence);
    json_text.trim();
  }
  int fb = json_text.indexOf('{'), lb = json_text.lastIndexOf('}');
  if (fb >= 0 && lb > fb) json_text = json_text.substring(fb, lb + 1);

  DynamicJsonDocument recipesDoc(4096);
  if (deserializeJson(recipesDoc, json_text)) { g_recipes_status = "Could not parse suggestions"; return false; }

  JsonArray arr = recipesDoc["recipes"];
  if (arr.isNull()) { g_recipes_status = "No suggestions returned"; return false; }

  for (JsonObject r : arr) {
    if (g_recipe_count >= MAX_RECIPES) break;
    g_recipes[g_recipe_count].name    = r["name"]    | "Recipe";
    g_recipes[g_recipe_count].summary = r["summary"] | "";

    // Steps arrive as a JSON array (one instruction per entry) — join them
    // with '\n' so the detail screen can split back into one line per step.
    String joined;
    JsonArray steps = r["steps"];
    int stepCount = 0;
    if (!steps.isNull()) {
      for (JsonVariant sv : steps) {
        if (stepCount >= MAX_STEPS_PER_RECIPE) break;
        String step = sv.as<String>();
        step.trim();
        if (step.length() == 0) continue;
        if (joined.length() > 0) joined += "\n";
        joined += step;
        stepCount++;
      }
    }
    g_recipes[g_recipe_count].steps = joined;
    g_recipe_count++;
  }

  if (g_recipe_count == 0) g_recipes_status = "No suggestions returned";
  Serial.printf("[RECIPES] %d suggestions\n", g_recipe_count);
  return g_recipe_count > 0;
}

// ----------------------------------------------------------------------------
// List screen
// ----------------------------------------------------------------------------
BtnRect btnGetRecommendations;
BtnRect btnFilterBreakfast, btnFilterLunch, btnFilterHealthy, btnFilterQuick;

// Filter chips + "Get Recommendations" button sit between the header and the
// list — bump the list's top down past them so computeRecipesLayout() and
// renderRecipesScreen() never disagree about where the list actually starts
// (same reasoning as computeListLayout()'s shared ListLayout in display.h).
//
// Chips are laid out as a 2x2 grid (not a cramped single row of 4) so each
// one is a large, easy-to-hit tap target with a legible size-2 label.
#define FILTER_BAR_Y   (HEADER_HEIGHT_PX + 6)
#define FILTER_CHIP_H  42
#define FILTER_ROW_GAP 8
#define FILTER_BAR_H   (FILTER_CHIP_H * 2 + FILTER_ROW_GAP)
#define GET_BTN_Y      (FILTER_BAR_Y + FILTER_BAR_H + 8)
#define GET_BTN_H      34
#define RECIPES_LIST_TOP (GET_BTN_Y + GET_BTN_H + 8)

RecipesLayout computeRecipesLayout() {
  RecipesLayout l;
  l.top    = RECIPES_LIST_TOP;
  l.bottom = tft.height() - 4;

  int remaining = max(0, g_recipe_count - g_recipes_scroll);

  l.show_up = g_recipes_scroll > 0;
  int avail = l.bottom - l.top - (l.show_up ? SCROLL_ARROW_H : 0);
  int capacity = max(0, avail / RECIPE_ROW_H);
  l.show_down = remaining > capacity;
  if (l.show_down) {
    capacity = max(0, (avail - SCROLL_ARROW_H) / RECIPE_ROW_H);
    l.show_down = remaining > capacity;
  }
  l.rows_visible = min(capacity, remaining);

  l.page_step = max(1, (l.bottom - l.top - 2 * SCROLL_ARROW_H) / RECIPE_ROW_H);

  l.up_y    = l.top;
  l.rows_y0 = l.top + (l.show_up ? SCROLL_ARROW_H : 0);
  l.down_y  = l.rows_y0 + l.rows_visible * RECIPE_ROW_H;
  return l;
}

void drawRecipeRow(int index, int y) {
  int w = tft.width();
  int rowH = RECIPE_ROW_H - ROW_GAP_PX;
  uint16_t bg = (index % 2 == 0) ? TFT_BLACK : 0x18E3;

  tft.fillRect(0, y, w, RECIPE_ROW_H, TFT_BLACK);
  tft.fillRect(0, y, w, rowH, bg);

  int text_x     = SIDE_PADDING_PX;
  int arrow_x    = w - ROW_ARROW_ZONE_PX;
  int text_right = arrow_x - 4;

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, bg);
  tft.setTextSize(2);
  tft.drawString(truncateToWidth(g_recipes[index].name, text_right - text_x), text_x, y + 8);

  tft.setTextColor(TFT_LIGHTGREY, bg);
  tft.setTextSize(1);
  tft.drawString(truncateToWidth(g_recipes[index].summary, text_right - text_x), text_x, y + 34);

  // Same tappable "open details" arrow as inventory rows (display.h).
  tft.drawFastVLine(arrow_x, y + 6, rowH - 12, TFT_DARKGREY);
  int ax = arrow_x + (ROW_ARROW_ZONE_PX - 18) / 2;
  int ay = y + rowH / 2;
  tft.fillTriangle(ax, ay - 12, ax, ay + 12, ax + 18, ay, TFT_LIGHTGREY);

  tft.drawRect(0, y, w, rowH, TFT_DARKGREY);
}

// A radio-style chip — filled green + bright border when it's the selected
// filter, muted grey otherwise, so the active mode is visible at a glance.
// Size-2 label (bigger, easier to read than the old size-1 row-of-4 chips).
void drawFilterChip(BtnRect b, const char* label, bool active) {
  uint16_t bg = active ? TFT_DARKGREEN : 0x2945;
  tft.fillRoundRect(b.x, b.y, b.w, b.h, 8, bg);
  tft.drawRoundRect(b.x, b.y, b.w, b.h, 8, active ? TFT_GREENYELLOW : TFT_DARKGREY);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, bg);
  tft.setTextSize(2);
  tft.drawString(label, b.x + b.w / 2, b.y + b.h / 2);
}

// 2x2 grid — bigger tap targets than a single row of 4 skinny chips.
void drawFilterBar() {
  int w = tft.width();
  const int colGap = 10;
  int chipW = (w - 2 * SIDE_PADDING_PX - colGap) / 2;
  int col0 = SIDE_PADDING_PX;
  int col1 = col0 + chipW + colGap;
  int row0 = FILTER_BAR_Y;
  int row1 = row0 + FILTER_CHIP_H + FILTER_ROW_GAP;

  btnFilterBreakfast = {col0, row0, chipW, FILTER_CHIP_H};
  btnFilterLunch     = {col1, row0, chipW, FILTER_CHIP_H};
  btnFilterHealthy   = {col0, row1, chipW, FILTER_CHIP_H};
  btnFilterQuick     = {col1, row1, chipW, FILTER_CHIP_H};

  drawFilterChip(btnFilterBreakfast, "Breakfast", g_selected_filter == FILTER_BREAKFAST);
  drawFilterChip(btnFilterLunch,     "Lunch",     g_selected_filter == FILTER_LUNCH);
  drawFilterChip(btnFilterHealthy,   "Healthy",   g_selected_filter == FILTER_HEALTHY);
  drawFilterChip(btnFilterQuick,     "Quick",     g_selected_filter == FILTER_QUICK);
}

void renderRecipesScreen() {
  tft.fillScreen(TFT_BLACK);
  int w = tft.width();

  tft.fillRect(0, 0, w, HEADER_HEIGHT_PX, TFT_NAVY);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextSize(2);
  tft.drawString("Recipe Ideas", w / 2, HEADER_HEIGHT_PX / 2);

  layoutDetailButtons();
  drawBtn(btnBack, "< Back", TFT_DARKGREY);

  drawFilterBar();

  btnGetRecommendations = {SIDE_PADDING_PX, GET_BTN_Y, w - 2 * SIDE_PADDING_PX, GET_BTN_H};
  drawBtn(btnGetRecommendations, "Get Recommendations", TFT_DARKGREEN);

  if (g_recipe_count == 0) {
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setTextSize(2);
    drawWrappedText(g_recipes_status, SIDE_PADDING_PX, RECIPES_LIST_TOP + 20, w - 2 * SIDE_PADDING_PX, 26, 3);
    return;
  }

  // Clamp scroll in case the list shrank (e.g. a re-query returned fewer items).
  while (g_recipes_scroll > 0 && computeRecipesLayout().rows_visible == 0) g_recipes_scroll--;

  RecipesLayout l = computeRecipesLayout();
  if (l.show_up) drawScrollArrow(l.up_y, true);
  for (int i = 0; i < l.rows_visible; i++)
    drawRecipeRow(g_recipes_scroll + i, l.rows_y0 + i * RECIPE_ROW_H);
  if (l.show_down) drawScrollArrow(l.down_y, false);
}

void openRecipesScreen() {
  g_view = VIEW_RECIPES;
  // Deliberately no fetchRecipes() call here — the user picks filters first,
  // then taps "Get Recommendations" (onGetRecommendationsPressed()) to
  // actually query Gemini. Keeps any recipes/status from a previous visit
  // this boot from confusingly reappearing under a different filter set.
  g_recipe_count   = 0;
  g_recipes_scroll = 0;
  g_recipes_status = "Choose your filters, then tap Get Recommendations";
  renderRecipesScreen();
}

void onGetRecommendationsPressed() {
  showStatus("Loading recipes...", "Asking Gemini AI");
  fetchRecipes();
  renderRecipesScreen();
}

// Selects a filter chip (tapping the already-selected one clears it back to
// "no filter") and clears any stale results — re-querying is a separate
// explicit step (the "Get Recommendations" button) so switching modes a few
// times in a row doesn't fire a slow Gemini request per tap.
void selectFilterAndReset(RecipeFilter f) {
  g_selected_filter = (g_selected_filter == f) ? FILTER_NONE : f;
  g_recipe_count   = 0;
  g_recipes_scroll = 0;
  g_recipes_status = "Filters changed - tap Get Recommendations";
  renderRecipesScreen();
}

void handleRecipesTouch(int tx, int ty) {
  if (inBtn(btnBackHit, tx, ty)) { g_view = VIEW_HOME; renderHomeScreen(); return; }
  if (inBtn(btnGetRecommendations, tx, ty)) { onGetRecommendationsPressed(); return; }

  if (inBtn(btnFilterBreakfast, tx, ty)) { selectFilterAndReset(FILTER_BREAKFAST); return; }
  if (inBtn(btnFilterLunch,     tx, ty)) { selectFilterAndReset(FILTER_LUNCH);     return; }
  if (inBtn(btnFilterHealthy,   tx, ty)) { selectFilterAndReset(FILTER_HEALTHY);   return; }
  if (inBtn(btnFilterQuick,     tx, ty)) { selectFilterAndReset(FILTER_QUICK);     return; }

  if (g_recipe_count == 0) return;

  RecipesLayout l = computeRecipesLayout();
  if (ty < l.top || ty > l.bottom) return;

  if (l.show_up && ty >= l.up_y && ty < l.up_y + SCROLL_ARROW_H) {
    g_recipes_scroll = max(0, g_recipes_scroll - l.page_step);
    renderRecipesScreen();
    return;
  }
  if (l.show_down && ty >= l.down_y && ty < l.down_y + SCROLL_ARROW_H) {
    g_recipes_scroll = min(g_recipe_count, g_recipes_scroll + l.page_step);
    renderRecipesScreen();
    return;
  }

  if (!l.show_up && ty < l.rows_y0 - TOP_ROW_HIT_EXTEND_PX) return;

  int rel, row;
  if (ty < l.rows_y0) {
    row = 0;
    rel = 0;
  } else {
    rel = (ty - l.rows_y0) % RECIPE_ROW_H;
    row = (ty - l.rows_y0) / RECIPE_ROW_H;
  }
  if (row < 0 || row >= l.rows_visible) return;
  int idx = g_recipes_scroll + row;
  if (idx >= g_recipe_count) return;
  if (rel >= RECIPE_ROW_H - ROW_TAP_DEADZONE_PX) return;

  g_recipe_detail_index = idx;
  g_detail_scroll = 0;   // always open a recipe's detail screen scrolled to the top
  g_view = VIEW_RECIPE_DETAIL;
  drawRecipeDetail(idx);
}

// ----------------------------------------------------------------------------
// Detail screen — one step per line, with scroll arrows if they overflow.
// (g_detail_lines/g_detail_scroll/g_detail_layout etc. are declared near the
// top of this file — see the comment there for why.)
// ----------------------------------------------------------------------------

// Splits r.steps on '\n', numbers each step "N. ...", and word-wraps each
// into g_detail_lines[] — one array entry per rendered line (a long step can
// span several lines) so the detail screen can scroll line-by-line rather
// than step-by-step. Caller must have set the text size to be used for
// measurement/drawing before calling (tft.textWidth() is size/font-dependent).
void buildDetailLines(RecipeItem& r, int maxWidth) {
  g_detail_line_count = 0;
  int stepNum = 1;
  int start = 0;
  int n = r.steps.length();

  while (start <= n && g_detail_line_count < MAX_DETAIL_LINES) {
    int nl = r.steps.indexOf('\n', start);
    String step = (nl >= 0) ? r.steps.substring(start, nl) : r.steps.substring(start);
    step.trim();
    if (step.length() > 0) {
      String prefixed = String(stepNum) + ". " + step;
      int added = wrapIntoLines(prefixed, maxWidth,
                                 &g_detail_lines[g_detail_line_count],
                                 MAX_DETAIL_LINES - g_detail_line_count);
      g_detail_line_count += added;
      stepNum++;
    }
    if (nl < 0) break;
    start = nl + 1;
  }
}

void drawRecipeDetail(int idx) {
  tft.fillScreen(TFT_BLACK);
  int w = tft.width(), h = tft.height();
  RecipeItem& r = g_recipes[idx];

  tft.fillRect(0, 0, w, HEADER_HEIGHT_PX, TFT_NAVY);
  layoutDetailButtons();
  drawBtn(btnBack, "< Back", TFT_DARKGREY);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextSize(2);
  tft.drawString(truncateToWidth(r.name, w - 180), w / 2, HEADER_HEIGHT_PX / 2);

  int y = HEADER_HEIGHT_PX + 12;
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextSize(2);
  int summaryLines = drawWrappedText(r.summary, SIDE_PADDING_PX, y, w - 2 * SIDE_PADDING_PX, 24, 2);

  int labelY = y + summaryLines * 24 + 8;
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(1);
  tft.drawString("STEPS", SIDE_PADDING_PX, labelY);

  int listTop    = labelY + 18;
  int listBottom = h - 4;
  int maxWidth   = w - 2 * SIDE_PADDING_PX;

  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  buildDetailLines(r, maxWidth);

  // Clamp scroll in case a shorter recipe was opened after scrolling a longer one.
  while (g_detail_scroll > 0 && g_detail_scroll >= g_detail_line_count) g_detail_scroll--;

  int avail = listBottom - listTop;
  bool show_up = g_detail_scroll > 0;
  int availAfterUp = avail - (show_up ? SCROLL_ARROW_H : 0);
  int capacity = max(0, availAfterUp / DETAIL_LINE_H);
  int remaining = max(0, g_detail_line_count - g_detail_scroll);
  bool show_down = remaining > capacity;
  if (show_down) {
    capacity = max(0, (availAfterUp - SCROLL_ARROW_H) / DETAIL_LINE_H);
    show_down = remaining > capacity;
  }
  int rows_visible = min(capacity, remaining);
  int page_step = max(1, (avail - 2 * SCROLL_ARROW_H) / DETAIL_LINE_H);

  int cursorY = listTop;
  int up_y = listTop;
  if (show_up) { drawScrollArrow(up_y, true); cursorY += SCROLL_ARROW_H; }
  int rows_y0 = cursorY;

  tft.setTextDatum(TL_DATUM);
  for (int i = 0; i < rows_visible; i++)
    tft.drawString(g_detail_lines[g_detail_scroll + i], SIDE_PADDING_PX, rows_y0 + i * DETAIL_LINE_H);

  int down_y = rows_y0 + rows_visible * DETAIL_LINE_H;
  if (show_down) drawScrollArrow(down_y, false);

  g_detail_layout.show_up     = show_up;
  g_detail_layout.up_y        = up_y;
  g_detail_layout.rows_y0     = rows_y0;
  g_detail_layout.rows_visible = rows_visible;
  g_detail_layout.show_down   = show_down;
  g_detail_layout.down_y      = down_y;
  g_detail_layout.page_step   = page_step;
}

void handleRecipeDetailTouch(int tx, int ty) {
  if (inBtn(btnBackHit, tx, ty)) { g_view = VIEW_RECIPES; renderRecipesScreen(); return; }

  if (g_detail_layout.show_up && ty >= g_detail_layout.up_y && ty < g_detail_layout.up_y + SCROLL_ARROW_H) {
    g_detail_scroll = max(0, g_detail_scroll - g_detail_layout.page_step);
    drawRecipeDetail(g_recipe_detail_index);
    return;
  }
  if (g_detail_layout.show_down && ty >= g_detail_layout.down_y && ty < g_detail_layout.down_y + SCROLL_ARROW_H) {
    g_detail_scroll = min(g_detail_line_count, g_detail_scroll + g_detail_layout.page_step);
    drawRecipeDetail(g_recipe_detail_index);
    return;
  }
}
