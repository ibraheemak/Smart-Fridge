#pragma once

// ============================================================================
// Touch screen — item detail page + expiry-date editor
// ============================================================================
//
// Tap an item row on the inventory list to open a detail page showing the
// icon, quantity, confidence and an editable expiry date (+/- buttons per
// field). "Save" writes the date back to Firestore, "< Back" returns to the
// inventory list.
//
// Requires:
//   - Preferences.h (built into the ESP32 core) for storing touch calibration
//   - display.h (TFT instance, InventoryItem, drawIcon, confidenceColor, ...)
//   - SECRETS.h / parameters.h (Firebase project + fridge id)
// ============================================================================

#include <Preferences.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "time.h"

#include "display.h"

// ----------------------------------------------------------------------------
// State
// ----------------------------------------------------------------------------
enum ViewState { VIEW_LIST, VIEW_DETAIL };

ViewState     g_view          = VIEW_LIST;
int           g_detail_index  = -1;
unsigned long g_last_touch_ms = 0;

int g_exp_year = 0, g_exp_month = 0, g_exp_day = 0;

// ----------------------------------------------------------------------------
// Touch calibration (stored in NVS so it persists across reboots)
// ----------------------------------------------------------------------------
uint16_t g_cal_data[5];

// Bump this key whenever the touch wiring/rotation changes, or to discard a
// stale calibration saved before TOUCH_CS was wired into the active TFT_eSPI
// config (those old values were garbage and would otherwise be reused).
#define TOUCH_CAL_KEY "cal_v6"

void initTouch() {
  Preferences prefs;
  prefs.begin("touch", false);

  if (prefs.getBytesLength(TOUCH_CAL_KEY) == sizeof(g_cal_data)) {
    prefs.getBytes(TOUCH_CAL_KEY, g_cal_data, sizeof(g_cal_data));
    tft.setTouch(g_cal_data);
  } else {
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.drawString("Touch each corner", tft.width() / 2, tft.height() / 2 - 12);
    tft.drawString("to calibrate", tft.width() / 2, tft.height() / 2 + 12);
    delay(1000);
    tft.calibrateTouch(g_cal_data, TFT_WHITE, TFT_RED, 15);
    prefs.putBytes(TOUCH_CAL_KEY, g_cal_data, sizeof(g_cal_data));
    tft.setTouch(g_cal_data);
  }

  prefs.end();
}

// ----------------------------------------------------------------------------
// Buttons
// ----------------------------------------------------------------------------
struct BtnRect { int x, y, w, h; };

BtnRect btnBack;       // drawn size
BtnRect btnBackHit;    // larger invisible touch zone (top-left edge is inaccurate)
BtnRect btnDayMinus, btnDayPlus, btnMonMinus, btnMonPlus, btnYearMinus, btnYearPlus;
BtnRect btnSave;

void drawBtn(BtnRect b, const String& label, uint16_t color) {
  tft.fillRoundRect(b.x, b.y, b.w, b.h, 6, color);
  tft.drawRoundRect(b.x, b.y, b.w, b.h, 6, TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, color);
  tft.setTextSize(2);
  tft.drawString(label, b.x + b.w / 2, b.y + b.h / 2);
}

// Extra tap margin around each button, in pixels. Kept small so adjacent
// buttons don't swallow each other's taps (date buttons sit only GROUP_GAP_PX
// apart, so margin must be <= GROUP_GAP_PX / 2).
#define BTN_TAP_MARGIN_PX 5

bool inBtn(const BtnRect& b, int x, int y) {
  return x >= b.x - BTN_TAP_MARGIN_PX && x <= b.x + b.w + BTN_TAP_MARGIN_PX &&
         y >= b.y - BTN_TAP_MARGIN_PX && y <= b.y + b.h + BTN_TAP_MARGIN_PX;
}

void layoutDetailButtons() {
  int w = tft.width();

  btnBack    = {8, 6, 70, HEADER_HEIGHT_PX - 12};
  // Touch is least accurate near screen edges, so give the back button a much
  // larger invisible hit zone than its drawn size — it has no neighbours to
  // worry about overlapping with.
  btnBackHit = {0, 0, 160, 140};

  int bw = 44, bh = 44;
  const int GROUP_GAP_PX = 12;
  int groupW = (w - 2 * GROUP_GAP_PX) / 3;
  int y = 190;

  int g0 = 0;
  int g1 = g0 + groupW + GROUP_GAP_PX;
  int g2 = g1 + groupW + GROUP_GAP_PX;

  btnDayMinus  = {g0,               y, bw, bh};
  btnDayPlus   = {g0 + groupW - bw, y, bw, bh};
  btnMonMinus  = {g1,               y, bw, bh};
  btnMonPlus   = {g1 + groupW - bw, y, bw, bh};
  btnYearMinus = {g2,               y, bw, bh};
  btnYearPlus  = {g2 + groupW - bw, y, bw, bh};

  btnSave = {(w - 140) / 2, y + 80, 140, 44};
}

// ----------------------------------------------------------------------------
// Expiry date helpers
// ----------------------------------------------------------------------------
bool isLeapYear(int y) {
  return (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
}

int daysInMonth(int m, int y) {
  static const int dim[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m == 2 && isLeapYear(y)) return 29;
  return dim[m - 1];
}

void drawValueField(const BtnRect& minusBtn, const BtnRect& plusBtn, const char* fmt, int value) {
  int y = minusBtn.y + minusBtn.h / 2;
  int x0 = minusBtn.x + minusBtn.w;
  int valueW = plusBtn.x - x0;

  char buf[8];
  snprintf(buf, sizeof(buf), fmt, value);

  tft.fillRect(x0, minusBtn.y, valueW, minusBtn.h, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(buf, x0 + valueW / 2, y);
}

void drawExpiryDate() {
  drawValueField(btnDayMinus,  btnDayPlus,  "%02d", g_exp_day);
  drawValueField(btnMonMinus,  btnMonPlus,  "%02d", g_exp_month);
  drawValueField(btnYearMinus, btnYearPlus, "%04d", g_exp_year);
}

void adjustExpiry(int dDay, int dMonth, int dYear) {
  g_exp_year += dYear;

  g_exp_month += dMonth;
  if (g_exp_month < 1)  { g_exp_month = 12; g_exp_year--; }
  if (g_exp_month > 12) { g_exp_month = 1;  g_exp_year++; }

  g_exp_day += dDay;
  if (g_exp_day < 1) {
    g_exp_month--;
    if (g_exp_month < 1) { g_exp_month = 12; g_exp_year--; }
    g_exp_day = daysInMonth(g_exp_month, g_exp_year);
  }
  int dim = daysInMonth(g_exp_month, g_exp_year);
  if (g_exp_day > dim) {
    g_exp_day = 1;
    g_exp_month++;
    if (g_exp_month > 12) { g_exp_month = 1; g_exp_year++; }
  }

  if (g_exp_year < 2024) g_exp_year = 2024;
  if (g_exp_year > 2099) g_exp_year = 2099;

  drawExpiryDate();
}

// ----------------------------------------------------------------------------
// Detail page
// ----------------------------------------------------------------------------
void drawItemDetail(int idx) {
  tft.fillScreen(TFT_BLACK);
  InventoryItem& it = g_items[idx];
  int w = tft.width();

  tft.fillRect(0, 0, w, HEADER_HEIGHT_PX, TFT_NAVY);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextSize(2);
  tft.drawString("Item Details", w / 2, HEADER_HEIGHT_PX / 2);

  layoutDetailButtons();
  drawBtn(btnBack, "< Back", TFT_DARKGREY);

  int icon_x = SIDE_PADDING_PX;
  int icon_y = HEADER_HEIGHT_PX + 16;
  drawIcon(it.name, icon_x, icon_y, TFT_BLACK);

  int text_x = icon_x + ICON_SIZE_PX + 16;
  tft.setTextDatum(ML_DATUM);
  tft.setTextSize(3);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(it.name, text_x, icon_y + 12);

  tft.setTextSize(2);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Qty: " + it.quantity, text_x, icon_y + 48);

  tft.setTextColor(confidenceColor(it.confidence), TFT_BLACK);
  tft.drawString("Confidence: " + it.confidence, text_x, icon_y + 76);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("Expiry Date  (DD / MM / YYYY)", w / 2, 150);

  drawBtn(btnDayMinus, "-", TFT_DARKGREY);
  drawBtn(btnDayPlus, "+", TFT_DARKGREY);
  drawBtn(btnMonMinus, "-", TFT_DARKGREY);
  drawBtn(btnMonPlus, "+", TFT_DARKGREY);
  drawBtn(btnYearMinus, "-", TFT_DARKGREY);
  drawBtn(btnYearPlus, "+", TFT_DARKGREY);
  drawExpiryDate();

  drawBtn(btnSave, "Save", TFT_DARKGREEN);
}

void openItemDetail(int idx) {
  g_detail_index = idx;
  g_view = VIEW_DETAIL;

  InventoryItem& it = g_items[idx];
  if (it.expiry.length() == 10) {
    g_exp_year  = it.expiry.substring(0, 4).toInt();
    g_exp_month = it.expiry.substring(5, 7).toInt();
    g_exp_day   = it.expiry.substring(8, 10).toInt();
  } else {
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    g_exp_year  = t->tm_year + 1900;
    g_exp_month = t->tm_mon + 1;
    g_exp_day   = t->tm_mday;
  }

  drawItemDetail(idx);
}

// ----------------------------------------------------------------------------
// Save expiry date back to Firestore (rewrites the whole "items" array)
// ----------------------------------------------------------------------------
void saveExpiry() {
  char buf[11];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d", g_exp_year, g_exp_month, g_exp_day);
  g_items[g_detail_index].expiry = String(buf);

  drawFooter("Saving expiry date...");

  String url =
    "https://firestore.googleapis.com/v1/projects/" +
    String(FIREBASE_PROJECT_ID) +
    "/databases/(default)/documents/fridges/" +
    String(FRIDGE_ID) +
    "/inventory/current?updateMask.fieldPaths=items&key=" +
    String(FIREBASE_API_KEY);

  DynamicJsonDocument doc(8192);
  JsonArray values = doc["fields"]["items"]["arrayValue"]["values"].to<JsonArray>();
  for (int i = 0; i < g_item_count; i++) {
    JsonObject mf = values.createNestedObject()["mapValue"]["fields"].to<JsonObject>();
    mf["name"]["stringValue"]       = g_items[i].name;
    mf["quantity"]["stringValue"]   = g_items[i].quantity;
    mf["confidence"]["stringValue"] = g_items[i].confidence;
    mf["expiry"]["stringValue"]     = g_items[i].expiry;
  }

  String body;
  serializeJson(doc, body);

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(10000);
  if (http.begin(client, url)) {
    http.addHeader("Content-Type", "application/json");
    int code = http.PATCH(body);
    Serial.printf("[FIREBASE] PATCH expiry -> %d\n", code);
    http.end();
  }

  g_view = VIEW_LIST;
  renderInventory();
}

// ----------------------------------------------------------------------------
// Secondary Y correction — even after tft.setTouch() calibration, reported Y
// is compressed relative to actual Y. Measured: a tap near the center of row
// 0's arrow (actual y~79) reports y=16; row 1's arrow (actual y~155) reports
// y=116. That fits reported = 1.316*actual - 88, inverted below.
// ----------------------------------------------------------------------------
const float TOUCH_Y_SCALE  = 100.0f / 76.0f;
const float TOUCH_Y_OFFSET = 16.0f - TOUCH_Y_SCALE * 79.0f;

int correctTouchY(int rawY) {
  int y = (int)roundf((rawY - TOUCH_Y_OFFSET) / TOUCH_Y_SCALE);
  if (y < 0) y = 0;
  if (y > tft.height() - 1) y = tft.height() - 1;
  return y;
}

// ----------------------------------------------------------------------------
// Touch dispatch — call from loop()
// ----------------------------------------------------------------------------
void handleTouch() {
  uint16_t tx, ty;
  if (!tft.getTouch(&tx, &ty, TOUCH_PRESSURE_THRESHOLD)) return;
  ty = correctTouchY(ty);

  unsigned long now = millis();
  if (now - g_last_touch_ms < TOUCH_DEBOUNCE_MS) return;
  g_last_touch_ms = now;

  Serial.printf("[TOUCH] x=%d y=%d (corrected) view=%d\n", tx, ty, g_view);

  if (g_view == VIEW_LIST) {
    int list_top    = HEADER_HEIGHT_PX + 4;
    int list_bottom = tft.height() - FOOTER_HEIGHT_PX - 4;

    // Touch y-readings near the top edge come in lower than the actual tap
    // position, so row 0's hit zone is extended up into the header (which
    // has nothing tappable in list view) to compensate.
    if (ty < list_top - TOP_ROW_HIT_EXTEND_PX || ty > list_bottom) return;

    if (tx < tft.width() - ROW_ARROW_ZONE_PX) return;  // only the right-edge arrow opens details

    int max_rows = (list_bottom - list_top) / ROW_HEIGHT_PX;
    int rel, row;
    if (ty < list_top) {
      row = 0;
      rel = 0;
    } else {
      rel = (ty - list_top) % ROW_HEIGHT_PX;
      row = (ty - list_top) / ROW_HEIGHT_PX;
    }
    if (row < 0 || row >= g_item_count || row >= max_rows) return;
    if (rel >= ROW_HEIGHT_PX - ROW_TAP_DEADZONE_PX) return;  // tapped the gap/border between rows

    openItemDetail(row);
    return;
  }

  // VIEW_DETAIL
  if (inBtn(btnBackHit, tx, ty))   { g_view = VIEW_LIST; renderInventory(); return; }
  if (inBtn(btnDayMinus, tx, ty))  { adjustExpiry(-1, 0, 0); return; }
  if (inBtn(btnDayPlus, tx, ty))   { adjustExpiry(1, 0, 0);  return; }
  if (inBtn(btnMonMinus, tx, ty))  { adjustExpiry(0, -1, 0); return; }
  if (inBtn(btnMonPlus, tx, ty))   { adjustExpiry(0, 1, 0);  return; }
  if (inBtn(btnYearMinus, tx, ty)) { adjustExpiry(0, 0, -1); return; }
  if (inBtn(btnYearPlus, tx, ty))  { adjustExpiry(0, 0, 1);  return; }
  if (inBtn(btnSave, tx, ty))      { saveExpiry(); return; }
}
