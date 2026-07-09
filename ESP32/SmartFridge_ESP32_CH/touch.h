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
#include "rtdb_notify.h"

// Defined in stats.h, included after this file — forward-declared here since
// handleTouch() needs to trigger the stats screen on tap.
bool fetchBoughtStats();
void renderStatsScreen();

// Defined in gm65.h, included after this file — forward-declared here since
// handleTouch() needs to arm a barcode scan on tap of the footer "Scan" button.
void triggerGM65Scan();
// Starts a continuous multi-scan session (home "Scan" tile).
void beginGM65ScanSession();
// Cancels an in-progress scan (user tapped "< Back" on the scanning screen).
void cancelGM65Scan();

// Defined in settings.h, included after this file — handleTouch() dispatches
// taps to the Settings screen and the environment-alert screen.
void openSettingsScreen();
void handleSettingsTouch(int x, int y);
void handleAlertTouch(int x, int y);
void openBuzzerScreen();
void handleBuzzerTouch(int x, int y);

// ----------------------------------------------------------------------------
// State
// ----------------------------------------------------------------------------
enum ViewState { VIEW_HOME, VIEW_LIST, VIEW_DETAIL, VIEW_NEW_ITEM, VIEW_STATS, VIEW_SCAN, VIEW_SETTINGS, VIEW_BUZZER, VIEW_ALERT };

ViewState     g_view          = VIEW_HOME;
int           g_detail_index  = -1;
unsigned long g_last_touch_ms = 0;

int g_exp_year = 0, g_exp_month = 0, g_exp_day = 0;

// ----------------------------------------------------------------------------
// Queue of new units that need an expiry date entered by the user.
// Filled by the inventory-diff logic in SmartFridge_ESP32_CH.ino.
// ----------------------------------------------------------------------------
struct PendingExpiry {
  int item_index;   // index into g_items[]
  int expiry_index; // which slot in expiries[] to fill
};

// Slot [MAX_PENDING-1] is reserved as a temp to pass the current item/index to saveExpiry.
#define MAX_PENDING 40
PendingExpiry g_pending[MAX_PENDING];
int           g_pending_count = 0;

// Called by inventory-diff logic to enqueue a new unit.
void enqueuePendingExpiry(int item_idx, int expiry_idx) {
  if (g_pending_count >= MAX_PENDING - 1) return;  // -1: slot MAX_PENDING-1 is reserved as temp
  g_pending[g_pending_count] = {item_idx, expiry_idx};
  g_pending_count++;
}

// Consume the head of the queue and open the notification screen.
// Declared here; defined below after drawNewItemScreen.
void processNextPending();

// Returns the expiry_index currently being edited for VIEW_NEW_ITEM / VIEW_DETAIL.
int currentExpiryIndex() {
  return g_pending[MAX_PENDING - 1].expiry_index;
}

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
BtnRect btnEnterExpiry, btnSkip;  // VIEW_NEW_ITEM notification screen
BtnRect btnUnitPrev, btnUnitNext; // VIEW_DETAIL — step between units of the same item

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

  // Unit prev/next arrows flank the centred Save button on the bottom row.
  // Save spans x=(w-140)/2 .. +140 (170..310 at w=480), so these sit clear of
  // it in the left/right margins.
  int nav_y = y + 80, nav_w = 90, nav_h = 44;
  btnUnitPrev = {8,              nav_y, nav_w, nav_h};
  btnUnitNext = {w - 8 - nav_w,  nav_y, nav_w, nav_h};
}

// How many physical units of this item there are — one expiry slot per unit.
// The .ino inventory diff already grows expiry_count to match quantity, but
// fall back to the quantity string in case an item hasn't been through it yet.
int itemUnitCount(const InventoryItem& it) {
  int n = max((int)it.quantity.toInt(), it.expiry_count);
  if (n < 1) n = 1;
  if (n > MAX_EXPIRIES_PER_ITEM) n = MAX_EXPIRIES_PER_ITEM;
  return n;
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
  drawItemName(it.name, text_x, icon_y + 12);

  tft.setTextSize(2);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Qty: " + it.quantity, text_x, icon_y + 48);

  tft.setTextColor(confidenceColor(it.confidence), TFT_BLACK);
  tft.drawString("Confidence: " + it.confidence, text_x, icon_y + 76);

  int editing_slot = currentExpiryIndex();
  int unit_count   = itemUnitCount(it);
  String slot_label = "Expiry Date  (DD / MM / YYYY)";
  if (unit_count > 1)
    slot_label = "Unit " + String(editing_slot + 1) + " / " + String(unit_count) +
                 "   Expiry (DD / MM / YYYY)";
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString(slot_label, w / 2, 150);

  drawBtn(btnDayMinus, "-", TFT_DARKGREY);
  drawBtn(btnDayPlus, "+", TFT_DARKGREY);
  drawBtn(btnMonMinus, "-", TFT_DARKGREY);
  drawBtn(btnMonPlus, "+", TFT_DARKGREY);
  drawBtn(btnYearMinus, "-", TFT_DARKGREY);
  drawBtn(btnYearPlus, "+", TFT_DARKGREY);
  drawExpiryDate();

  drawBtn(btnSave, "Save", TFT_DARKGREEN);

  // With several units, show left/right arrows flanking Save so the user can
  // step through each unit and set its own expiry date. The "Unit X / N" label
  // above tracks which unit is currently shown.
  if (unit_count > 1) {
    drawBtn(btnUnitPrev, "< Prev", TFT_NAVY);
    drawBtn(btnUnitNext, "Next >", TFT_NAVY);
  }
}

// ----------------------------------------------------------------------------
// "New item detected" notification screen (VIEW_NEW_ITEM)
// ----------------------------------------------------------------------------
void drawNewItemScreen(int item_idx, int expiry_idx) {
  tft.fillScreen(TFT_BLACK);
  int w = tft.width(), h = tft.height();

  // Header
  tft.fillRect(0, 0, w, HEADER_HEIGHT_PX, 0x1967);  // deep teal
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, 0x1967);
  tft.setTextSize(2);
  tft.drawString("New Item Detected", w / 2, HEADER_HEIGHT_PX / 2);

  // Icon
  int icon_x = (w - ICON_SIZE_PX) / 2;
  int icon_y = HEADER_HEIGHT_PX + 20;
  drawIcon(g_items[item_idx].name, icon_x, icon_y, TFT_BLACK);

  // Item name
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  drawItemName(g_items[item_idx].name, w / 2, icon_y + ICON_SIZE_PX + 18);

  // Unit label (unit #N)
  tft.setTextSize(1);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  String unit_label = "Unit #" + String(expiry_idx + 1);
  tft.drawString(unit_label, w / 2, icon_y + ICON_SIZE_PX + 40);

  // Message
  tft.setTextSize(2);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("Set expiry date?", w / 2, icon_y + ICON_SIZE_PX + 70);

  // Buttons
  int btn_y = h - 80;
  int btn_w = 160, btn_h = 50;
  btnEnterExpiry = {(w / 2 - btn_w - 10), btn_y, btn_w, btn_h};
  btnSkip        = {(w / 2 + 10),          btn_y, btn_w, btn_h};

  drawBtn(btnEnterExpiry, "Set Date", TFT_DARKGREEN);
  drawBtn(btnSkip,        "Skip",     TFT_DARKGREY);
}

void processNextPending() {
  if (g_pending_count == 0) {
    g_view = VIEW_LIST;
    renderInventory();
    return;
  }
  PendingExpiry p = g_pending[0];
  // Shift queue
  for (int i = 0; i < g_pending_count - 1; i++) g_pending[i] = g_pending[i + 1];
  g_pending_count--;

  g_detail_index = p.item_index;
  int expiry_idx = p.expiry_index;

  g_view = VIEW_NEW_ITEM;
  drawNewItemScreen(g_detail_index, expiry_idx);

  // Store which expiry slot we're about to fill (reuse g_pending[MAX_PENDING-1] as temp).
  // We embed it in a dedicated variable instead:
  g_pending[MAX_PENDING - 1] = {g_detail_index, expiry_idx};  // temp slot
}

// Open the detail/expiry-editor for item[idx], editing expiry slot [expiry_idx].
// Pass expiry_idx = -1 when opening manually from the list (edits slot 0 for
// backward-compat, or the first empty slot if one exists).
void openItemDetail(int idx, int expiry_idx = -1) {
  g_detail_index = idx;
  g_view = VIEW_DETAIL;

  InventoryItem& it = g_items[idx];

  // Resolve which slot to edit.
  if (expiry_idx < 0) {
    // Manual open from list: find first empty slot, fallback to slot 0.
    expiry_idx = 0;
    for (int i = 0; i < it.expiry_count; i++) {
      if (it.expiries[i].length() == 0) { expiry_idx = i; break; }
    }
  }
  // Record for saveExpiry().
  g_pending[MAX_PENDING - 1] = {idx, expiry_idx};

  const String& e = it.expiries[expiry_idx];
  if (e.length() == 10) {
    g_exp_year  = e.substring(0, 4).toInt();
    g_exp_month = e.substring(5, 7).toInt();
    g_exp_day   = e.substring(8, 10).toInt();
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
// Write the current in-memory g_items[] (names/quantities/expiries) back to
// Firestore. Shared by saveExpiry() and skipExpiry() so a skipped slot is
// persisted as an empty placeholder, not just held in RAM — otherwise it
// would look "new" again (and re-enqueue a prompt) after every reboot.
// ----------------------------------------------------------------------------
void persistItemsToFirestore() {
  String url =
    "https://firestore.googleapis.com/v1/projects/" +
    String(FIREBASE_PROJECT_ID) +
    "/databases/(default)/documents/fridges/" +
    String(FRIDGE_ID) +
    "/inventory/current?key=" +
    String(FIREBASE_API_KEY);

  DynamicJsonDocument doc(8192);
  JsonObject fields = doc["fields"].to<JsonObject>();

  time_t now = time(nullptr);
  char ts[20];
  strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&now));
  fields["updatedAt"]["stringValue"] = ts;
  fields["source"]["stringValue"]    = "ESP32-CH";

  JsonArray values = fields["items"]["arrayValue"]["values"].to<JsonArray>();
  for (int i = 0; i < g_item_count; i++) {
    JsonObject mf = values.createNestedObject()["mapValue"]["fields"].to<JsonObject>();
    mf["name"]["stringValue"]       = g_items[i].name;
    mf["quantity"]["stringValue"]   = g_items[i].quantity;
    mf["confidence"]["stringValue"] = g_items[i].confidence;
    // Serialize the expiries array.
    JsonArray ea = mf["expiries"]["arrayValue"]["values"].to<JsonArray>();
    for (int j = 0; j < g_items[i].expiry_count; j++)
      ea.createNestedObject()["stringValue"] = g_items[i].expiries[j];
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
    Serial.printf("[FIREBASE] PATCH items -> %d\n", code);
    http.end();
    if (code == 200 || code == 201) rtdbNotifyInventoryChanged();
  }
}

// Skip a pending expiry prompt — persists the still-empty slot to Firestore
// so it isn't re-detected as "new" and re-enqueued after a reboot.
void skipExpiry() {
  int expiry_idx = currentExpiryIndex();
  InventoryItem& it = g_items[g_detail_index];
  if (expiry_idx >= it.expiry_count) it.expiry_count = expiry_idx + 1;
  // Leave it.expiries[expiry_idx] as "" — just make sure it's counted.

  persistItemsToFirestore();
  processNextPending();
}

// ----------------------------------------------------------------------------
// Save expiry date back to Firestore (rewrites the whole "items" array)
// ----------------------------------------------------------------------------
void saveExpiry() {
  char buf[11];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d", g_exp_year, g_exp_month, g_exp_day);

  int expiry_idx = currentExpiryIndex();
  InventoryItem& it = g_items[g_detail_index];

  // Grow expiry_count if writing beyond current count.
  if (expiry_idx >= it.expiry_count) it.expiry_count = expiry_idx + 1;
  it.expiries[expiry_idx] = String(buf);

  drawFooter("Saving expiry date...");
  persistItemsToFirestore();

  // If we came from the pending queue, process the next item.
  if (g_pending_count > 0) {
    processNextPending();
  } else {
    g_view = VIEW_LIST;
    renderInventory();
  }
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

  // VIEW_ALERT — CLOSE the out-of-range environment alert.
  if (g_view == VIEW_ALERT)    { handleAlertTouch(tx, ty); return; }

  // VIEW_SETTINGS — buzzer/door-alert/temp/humidity controls.
  if (g_view == VIEW_SETTINGS) { handleSettingsTouch(tx, ty); return; }

  // VIEW_BUZZER — buzzer volume/pitch/duration/melody sub-screen.
  if (g_view == VIEW_BUZZER)   { handleBuzzerTouch(tx, ty); return; }

  // VIEW_HOME — large tile buttons
  if (g_view == VIEW_HOME) {
    // Top-left "Settings" button. Touch readings compress near the top edge,
    // so use a generous corner zone (nothing else is tappable there) rather
    // than the button's exact drawn rect.
    if (tx < 130 && ty < 60) { openSettingsScreen(); return; }

    auto inTile = [](HomeTile t, int x, int y) {
      return x >= t.x && x < t.x + t.w && y >= t.y && y < t.y + t.h;
    };
    if (inTile(g_home_tiles[HOME_TILE_INVENTORY], tx, ty)) {
      g_view = VIEW_LIST;
      renderInventory();
    } else if (inTile(g_home_tiles[HOME_TILE_SCAN], tx, ty)) {
      g_view = VIEW_SCAN;
      beginGM65ScanSession();
    } else if (inTile(g_home_tiles[HOME_TILE_STATS], tx, ty)) {
      g_view = VIEW_STATS;
      showStatus("Loading stats...", "");
      fetchBoughtStats();
      renderStatsScreen();
    }
    return;
  }

  // VIEW_STATS — same header "< Back" button as the item-detail page.
  if (g_view == VIEW_STATS) {
    if (inBtn(btnBackHit, tx, ty)) { g_view = VIEW_HOME; renderHomeScreen(); }
    return;
  }

  // VIEW_SCAN — "< Back" cancels the in-progress scan and returns home.
  if (g_view == VIEW_SCAN) {
    if (inBtn(btnBackHit, tx, ty)) {
      cancelGM65Scan();
      g_view = VIEW_HOME;
      renderHomeScreen();
    }
    return;
  }

  if (g_view == VIEW_LIST) {
    ListLayout l = computeListLayout();

    // Header "< Back" button (top-left) returns to the home screen. Drawn by
    // renderInventory(). Same 140px-tall hit zone as btnBackHit elsewhere
    // (detail/stats/scan) — touch readings compress near the header and land
    // lower than the actual tap (see TOP_ROW_HIT_EXTEND_PX below), so a zone
    // limited to just HEADER_HEIGHT_PX made this button nearly unhittable.
    // Kept narrow on x (0-130) so it doesn't swallow most of the up-arrow
    // scroll strip (full width) — but the strip's left edge still falls
    // inside that x range, so exclude it explicitly when it's on screen.
    BtnRect listBackHit = {0, 0, 130, 140};
    bool onUpArrow = l.show_up && ty >= l.up_y && ty < l.up_y + SCROLL_ARROW_H;
    if (!onUpArrow && inBtn(listBackHit, tx, ty)) {
      g_view = VIEW_HOME;
      renderHomeScreen();
      return;
    }

    if (ty < l.top || ty > l.bottom) return;

    // Up/down scroll strips span the full width, independent of the
    // right-edge "open details" arrow zone used by item rows.
    if (onUpArrow) {
      g_list_scroll = max(0, g_list_scroll - l.rows_visible);
      renderInventory();
      return;
    }
    if (l.show_down && ty >= l.down_y && ty < l.down_y + SCROLL_ARROW_H) {
      g_list_scroll = min(max(0, g_item_count - l.rows_visible), g_list_scroll + l.rows_visible);
      renderInventory();
      return;
    }

    // Touch y-readings near the top edge come in lower than the actual tap
    // position, so the first visible row's hit zone is extended up into the
    // header to compensate — but only when there's no up-arrow strip there.
    if (!l.show_up && ty < l.rows_y0 - TOP_ROW_HIT_EXTEND_PX) return;

    if (tx < tft.width() - ROW_ARROW_ZONE_PX) return;  // only the right-edge arrow opens details

    int rel, row;
    if (ty < l.rows_y0) {
      row = 0;
      rel = 0;
    } else {
      rel = (ty - l.rows_y0) % ROW_HEIGHT_PX;
      row = (ty - l.rows_y0) / ROW_HEIGHT_PX;
    }
    if (row < 0 || row >= l.rows_visible) return;
    int item_idx = g_list_scroll + row;
    if (item_idx >= g_item_count) return;
    if (rel >= ROW_HEIGHT_PX - ROW_TAP_DEADZONE_PX) return;  // tapped the gap/border between rows

    openItemDetail(item_idx);
    return;
  }

  // VIEW_NEW_ITEM — notification screen
  if (g_view == VIEW_NEW_ITEM) {
    int expiry_idx = currentExpiryIndex();
    if (inBtn(btnEnterExpiry, tx, ty)) {
      openItemDetail(g_detail_index, expiry_idx);
    } else if (inBtn(btnSkip, tx, ty)) {
      skipExpiry();
    }
    return;
  }

  // VIEW_DETAIL
  if (inBtn(btnBackHit, tx, ty))   { g_view = VIEW_LIST; renderInventory(); return; }

  // Unit navigation — step to the previous/next unit of this item and load
  // that unit's expiry into the editor (wraps around at the ends). Only active
  // when the item actually has more than one unit.
  {
    InventoryItem& it = g_items[g_detail_index];
    int unit_count = itemUnitCount(it);
    if (unit_count > 1) {
      int cur = currentExpiryIndex();
      if (inBtn(btnUnitPrev, tx, ty)) {
        openItemDetail(g_detail_index, (cur - 1 + unit_count) % unit_count);
        return;
      }
      if (inBtn(btnUnitNext, tx, ty)) {
        openItemDetail(g_detail_index, (cur + 1) % unit_count);
        return;
      }
    }
  }

  if (inBtn(btnDayMinus, tx, ty))  { adjustExpiry(-1, 0, 0); return; }
  if (inBtn(btnDayPlus, tx, ty))   { adjustExpiry(1, 0, 0);  return; }
  if (inBtn(btnMonMinus, tx, ty))  { adjustExpiry(0, -1, 0); return; }
  if (inBtn(btnMonPlus, tx, ty))   { adjustExpiry(0, 1, 0);  return; }
  if (inBtn(btnYearMinus, tx, ty)) { adjustExpiry(0, 0, -1); return; }
  if (inBtn(btnYearPlus, tx, ty))  { adjustExpiry(0, 0, 1);  return; }
  if (inBtn(btnSave, tx, ty))      { saveExpiry(); return; }
}
