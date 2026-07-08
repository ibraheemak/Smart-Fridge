#pragma once

// Force TFT_eSPI to use this sketch's own pin/touch config (tft_setup.h)
// instead of the library's globally-installed User_Setup.h, which is set up
// for the CAM board (TFT_CS=-1, TFT_RST=-1, no TOUCH_CS at all). Without this,
// touch is silently compiled out.
#define USER_SETUP_LOADED
#include "tft_setup.h"

#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "parameters.h"
#include "SECRETS.h"

// ============================================================================
// TFT instance
// ============================================================================
TFT_eSPI tft = TFT_eSPI();

// ============================================================================
// Inventory data — written by Firebase fetch, read by draw functions
// ============================================================================
// Maximum number of expiry dates stored per item (one per unit).
#define MAX_EXPIRIES_PER_ITEM 20

struct InventoryItem {
  String name;
  String quantity;
  String confidence;
  String expiries[MAX_EXPIRIES_PER_ITEM];  // one per unit, "YYYY-MM-DD" or ""
  int    expiry_count;                      // how many slots are filled
};

InventoryItem g_items[MAX_ITEMS_DISPLAYED];
int    g_item_count = 0;
String g_updated_at = "";

// Index of the first visible row in the inventory list — lets the list
// scroll via the up/down arrow strips drawn by renderInventory().
int g_list_scroll = 0;

// ============================================================================
// Hebrew product name rendering
//
// TFT_eSPI's built-in fonts have no Hebrew glyphs, so a Hebrew name would
// just render blank. Rather than embedding a Unicode font (which blew past
// the flash partition size), Hebrew names show a placeholder on the TFT —
// the real name is still stored correctly in Firestore and shown as-is in
// the phone app.
// ============================================================================

// True if the UTF-8 string contains a Hebrew-block codepoint (U+0590-05FF),
// i.e. a lead byte of 0xD6 or 0xD7 followed by a UTF-8 continuation byte.
bool containsHebrew(const String& s) {
  for (size_t i = 0; i + 1 < s.length(); i++) {
    uint8_t b0 = s[i], b1 = s[i + 1];
    if ((b0 == 0xD6 || b0 == 0xD7) && (b1 & 0xC0) == 0x80) return true;
  }
  return false;
}

#define ITEM_NAME_MAX_CHARS 25

// Caps name at ITEM_NAME_MAX_CHARS characters (including spaces) with a
// trailing "..." so it never overruns into whatever is drawn to its right
// (quantity, count badge, etc).
String truncateItemName(const String& name) {
  if (name.length() <= ITEM_NAME_MAX_CHARS) return name;
  return name.substring(0, ITEM_NAME_MAX_CHARS) + "...";
}

// Drop-in replacement for tft.drawString(name, x, y) — shows a placeholder
// for Hebrew names (no glyphs available on this display) instead of
// drawing the raw text, which would render blank/garbage. Long names are
// capped at ITEM_NAME_MAX_CHARS with "..." so they never overlap whatever
// is drawn to their right (e.g. the quantity/count text).
void drawItemName(const String& name, int x, int y) {
  if (containsHebrew(name)) {
    tft.drawString(truncateItemName("Hebrew name item - see in the phone app"), x, y);
    return;
  }
  tft.drawString(truncateItemName(name), x, y);
}

// ============================================================================
// Backlight
// ============================================================================
void backlightOn() {
  if (TFT_BL_PIN >= 0) {
    pinMode(TFT_BL_PIN, OUTPUT);
    digitalWrite(TFT_BL_PIN, TFT_BL_ON);
  }
}

// ============================================================================
// Header / footer / helpers
// ============================================================================
void drawHeader() {
  int w = tft.width();
  tft.fillRect(0, 0, w, HEADER_HEIGHT_PX, TFT_NAVY);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextSize(2);
  tft.drawString("Smart Fridge", SIDE_PADDING_PX, HEADER_HEIGHT_PX / 2);
  tft.setTextDatum(MR_DATUM);
  tft.setTextSize(1);
  tft.drawString(String(g_item_count) + " items", w - SIDE_PADDING_PX, HEADER_HEIGHT_PX / 2);
}

void drawFooter(const String& msg) {
  int w = tft.width();
  int y = tft.height() - FOOTER_HEIGHT_PX;
  tft.fillRect(0, y, w, FOOTER_HEIGHT_PX, TFT_DARKGREY);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.setTextSize(1);
  tft.drawString(msg, SIDE_PADDING_PX, y + FOOTER_HEIGHT_PX / 2);
}

uint16_t confidenceColor(const String& conf) {
  if (conf.equalsIgnoreCase("high"))   return TFT_GREEN;
  if (conf.equalsIgnoreCase("medium")) return TFT_YELLOW;
  if (conf.equalsIgnoreCase("low"))    return TFT_RED;
  return TFT_LIGHTGREY;
}

void showStatus(const String& line1, const String& line2 = "") {
  tft.fillScreen(TFT_BLACK);
  drawHeader();
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString(line1, tft.width() / 2, tft.height() / 2 - 12);
  if (line2.length() > 0) {
    tft.setTextSize(1);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString(line2, tft.width() / 2, tft.height() / 2 + 16);
  }
}

// ============================================================================
// Icon fetch & render (Firebase Storage)
// ============================================================================
bool tftJpgOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  if (y >= tft.height()) return 0;
  tft.pushImage(x, y, w, h, bitmap);
  return 1;
}

struct DecodeBuf { uint16_t* px; int w; int h; };
static DecodeBuf g_dec = {nullptr, 0, 0};

bool decodeBufOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  if (!g_dec.px) return 0;
  for (int row = 0; row < h; row++) {
    int dy = y + row;
    if (dy < 0 || dy >= g_dec.h) continue;
    int dx0 = x;
    if (dx0 >= g_dec.w) continue;
    int copy_w = w;
    if (dx0 + copy_w > g_dec.w) copy_w = g_dec.w - dx0;
    if (copy_w <= 0) continue;
    memcpy(&g_dec.px[dy * g_dec.w + dx0], &bitmap[row * w], copy_w * 2);
  }
  return 1;
}

// Area-average (box filter) downscale in RGB565. sw >= dw and sh >= dh required.
void resampleAreaRGB565(const uint16_t* src, int sw, int sh,
                        uint16_t* dst, int dw, int dh) {
  for (int dy = 0; dy < dh; dy++) {
    int iy0 = (long)dy * sh / dh;
    int iy1 = (long)(dy + 1) * sh / dh;
    if (iy1 <= iy0) iy1 = iy0 + 1;
    if (iy1 > sh)   iy1 = sh;
    for (int dx = 0; dx < dw; dx++) {
      int ix0 = (long)dx * sw / dw;
      int ix1 = (long)(dx + 1) * sw / dw;
      if (ix1 <= ix0) ix1 = ix0 + 1;
      if (ix1 > sw)   ix1 = sw;
      uint32_t sR = 0, sG = 0, sB = 0, n = 0;
      for (int y = iy0; y < iy1; y++) {
        const uint16_t* row = src + (long)y * sw;
        for (int x = ix0; x < ix1; x++) {
          uint16_t p = row[x];
          p = (p >> 8) | (p << 8);   // un-swap big-endian RGB565 from TJpgDec
          sR += (p >> 11) & 0x1F;
          sG += (p >> 5)  & 0x3F;
          sB +=  p        & 0x1F;
          n++;
        }
      }
      uint16_t out = ((uint16_t)(sR/n) << 11) | ((uint16_t)(sG/n) << 5) | (uint16_t)(sB/n);
      dst[(long)dy * dw + dx] = (out >> 8) | (out << 8);  // re-swap for pushImage
    }
  }
}

String encodeStoragePath(const String& p) {
  static const char* hex = "0123456789ABCDEF";
  String out;
  for (size_t i = 0; i < p.length(); i++) {
    unsigned char c = (unsigned char) p[i];
    bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                      (c >= '0' && c <= '9') ||
                      c == '-' || c == '_' || c == '.' || c == '~';
    if (unreserved) { out += (char)c; }
    else { out += '%'; out += hex[c >> 4]; out += hex[c & 0x0F]; }
  }
  return out;
}

bool fetchIconJpeg(const String& item_name, uint8_t** out_buf, size_t* out_len) {
  if (item_name.length() == 0) return false;
  String enc = encodeStoragePath(String(ICON_PATH_PREFIX) + item_name + ICON_EXTENSION);
  String url = "https://firebasestorage.googleapis.com/v0/b/" +
               String(FIREBASE_STORAGE_BUCKET) + "/o/" + enc + "?alt=media";
  Serial.printf("[ICON] GET %s\n", url.c_str());

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(client, url)) return false;

  int code = http.GET();
  Serial.printf("[ICON] %s HTTP %d\n", item_name.c_str(), code);
  if (code != 200) { http.end(); return false; }

  int len = http.getSize();
  const int MAX_ICON_BYTES = 256 * 1024;
  if (len > MAX_ICON_BYTES) { http.end(); return false; }

  int buf_size = (len > 0) ? len : MAX_ICON_BYTES;
  uint8_t* buf = (uint8_t*) malloc(buf_size);
  if (!buf) { http.end(); return false; }

  WiFiClient* stream = http.getStreamPtr();
  int bytes_read = 0;
  unsigned long t0 = millis();
  int target = (len > 0) ? len : buf_size;
  while (http.connected() && bytes_read < target && millis() - t0 < 8000) {
    size_t avail = stream->available();
    if (avail) {
      int n = stream->readBytes(buf + bytes_read, min((int)avail, target - bytes_read));
      if (n <= 0) break;
      bytes_read += n;
      if (len < 0 && !http.connected()) break;
    } else { delay(2); }
  }
  http.end();

  if ((len > 0 && bytes_read != len) || bytes_read == 0 ||
      bytes_read < 2 || buf[0] != 0xFF || buf[1] != 0xD8) {
    free(buf);
    return false;
  }

  *out_buf = buf;
  *out_len = (size_t)bytes_read;
  return true;
}

static const size_t ICON_MAX_SRC_BYTES = 96 * 1024;

// Drawn when an icon can't be decoded (e.g. unsupported/progressive JPEG) or
// fetched at all: a neutral box with the item's first letter.
void drawIconPlaceholder(const String& name, int x, int y, uint16_t bg) {
  tft.fillRect(x, y, ICON_SIZE_PX, ICON_SIZE_PX, bg);
  tft.drawRect(x, y, ICON_SIZE_PX, ICON_SIZE_PX, TFT_DARKGREY);
  if (name.length() == 0) return;
  String letter = String(name[0]);
  letter.toUpperCase();
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_LIGHTGREY, bg);
  tft.setTextSize(2);
  tft.drawString(letter, x + ICON_SIZE_PX / 2, y + ICON_SIZE_PX / 2);
}

void drawIcon(const String& name, int x, int y, uint16_t bg) {
  tft.fillRect(x, y, ICON_SIZE_PX, ICON_SIZE_PX, bg);

  uint8_t* jbuf = nullptr;
  size_t   jlen = 0;
  if (!fetchIconJpeg(name, &jbuf, &jlen)) {
    tft.drawRect(x, y, ICON_SIZE_PX, ICON_SIZE_PX, TFT_DARKGREY);
    return;
  }

  uint16_t jw = 0, jh = 0;
  int size_rc = TJpgDec.getJpgSize(&jw, &jh, jbuf, jlen);
  if (size_rc != 0 || jw == 0 || jh == 0) {
    TJpgDec.setJpgScale(1);
    TJpgDec.setCallback(tftJpgOutput);
    int rc1 = TJpgDec.drawJpg(x, y, jbuf, jlen);
    Serial.printf("[ICON] %s getJpgSize rc=%d (jw=%u jh=%u) -> fallback drawJpg rc=%d\n",
                   name.c_str(), size_rc, jw, jh, rc1);
    if (rc1 != 0)
      drawIconPlaceholder(name, x, y, bg);
    free(jbuf);
    return;
  }

  uint8_t dec_scale = 1;
  while (dec_scale < 8) {
    if ((size_t)(jw/dec_scale) * (jh/dec_scale) * 2 <= ICON_MAX_SRC_BYTES) break;
    dec_scale <<= 1;
  }
  int sw = jw / dec_scale, sh = jh / dec_scale;
  if (sw < ICON_SIZE_PX || sh < ICON_SIZE_PX) {
    TJpgDec.setJpgScale(1);
    TJpgDec.setCallback(tftJpgOutput);
    TJpgDec.drawJpg(x, y, jbuf, jlen);
    free(jbuf);
    return;
  }

  uint16_t* sbuf = nullptr;
  if (psramFound()) sbuf = (uint16_t*) ps_malloc((size_t)sw * sh * 2);
  if (!sbuf)        sbuf = (uint16_t*) malloc((size_t)sw * sh * 2);
  if (!sbuf) {
    Serial.printf("[ICON] %s sbuf malloc failed (%dx%d, %u bytes, psram=%d)\n",
                   name.c_str(), sw, sh, (unsigned)((size_t)sw*sh*2), psramFound());
    free(jbuf); drawIconPlaceholder(name, x, y, bg); return;
  }

  g_dec = {sbuf, sw, sh};
  TJpgDec.setJpgScale(dec_scale);
  TJpgDec.setCallback(decodeBufOutput);
  int rc = TJpgDec.drawJpg(0, 0, jbuf, jlen);
  free(jbuf);
  g_dec.px = nullptr;

  if (rc != 0) {
    Serial.printf("[ICON] %s drawJpg rc=%d (jw=%u jh=%u dec_scale=%u sw=%d sh=%d)\n",
                   name.c_str(), rc, jw, jh, dec_scale, sw, sh);
    free(sbuf); drawIconPlaceholder(name, x, y, bg); return;
  }

  uint16_t* dbuf = (uint16_t*) malloc((size_t)ICON_SIZE_PX * ICON_SIZE_PX * 2);
  if (!dbuf) { free(sbuf); return; }
  resampleAreaRGB565(sbuf, sw, sh, dbuf, ICON_SIZE_PX, ICON_SIZE_PX);
  free(sbuf);
  tft.pushImage(x, y, ICON_SIZE_PX, ICON_SIZE_PX, dbuf);
  free(dbuf);
}

// ============================================================================
// Home screen
// ============================================================================

// Live sensor values — fetched from Firestore sensors/temperature; -1 = no data yet.
float g_temp_c    = -1.0f;
float g_humidity  = -1.0f;

// Fetch latest temperature & humidity from Firestore.
// Path: fridges/{FRIDGE_ID}/sensors/temperature
// Fields: temperature (doubleValue), humidity (doubleValue)
bool fetchTemperature() {
  if (WiFi.status() != WL_CONNECTED) return false;

  String url =
    "https://firestore.googleapis.com/v1/projects/" +
    String(FIREBASE_PROJECT_ID) +
    "/databases/(default)/documents/fridges/" +
    String(FRIDGE_ID) +
    "/sensors/temperature?key=" +
    String(FIREBASE_API_KEY);

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(client, url)) return false;

  int code = http.GET();
  if (code != 200) { http.end(); return false; }

  String body = http.getString();
  http.end();

  DynamicJsonDocument doc(512);
  if (deserializeJson(doc, body)) return false;

  JsonObject fields = doc["fields"];
  if (fields.isNull()) return false;

  g_temp_c   = fields["temperature"]["doubleValue"].as<float>();
  g_humidity = fields["humidity"]["doubleValue"].as<float>();
  Serial.printf("[TEMP] %.1f C  %.0f%%\n", g_temp_c, g_humidity);
  return true;
}

// Tile IDs — used by handleTouch() to know which tile was tapped.
#define HOME_TILE_INVENTORY  0
#define HOME_TILE_SCAN       1
#define HOME_TILE_STATS      2

struct HomeTile { int x, y, w, h; };
HomeTile g_home_tiles[3];

// Draw one home-screen tile.  bg is the fill colour; icon is a short emoji/
// ASCII string shown large above the label.
void drawHomeTile(HomeTile t, const char* icon, const char* label,
                  const char* sub, uint16_t bg, uint16_t fg) {
  tft.fillRoundRect(t.x + 4, t.y + 4, t.w - 8, t.h - 8, 12, bg);
  tft.drawRoundRect(t.x + 4, t.y + 4, t.w - 8, t.h - 8, 12, fg);

  int cx = t.x + t.w / 2;
  int cy = t.y + t.h / 2;

  // Icon (large)
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(fg, bg);
  tft.setTextSize(3);
  tft.drawString(icon, cx, cy - 22);

  // Label
  tft.setTextSize(2);
  tft.drawString(label, cx, cy + 12);

  // Sub-label (small)
  if (sub && sub[0]) {
    tft.setTextSize(1);
    tft.setTextColor(TFT_LIGHTGREY, bg);
    tft.drawString(sub, cx, cy + 32);
  }
}

void renderHomeScreen() {
  int W = tft.width();   // 480
  int H = tft.height();  // 320

  tft.fillScreen(TFT_BLACK);

  // ── Header ────────────────────────────────────────────────────────────────
  const int HDR = 50;
  tft.fillRect(0, 0, W, HDR, TFT_NAVY);

  // Title
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextSize(2);
  tft.drawString("Smart Fridge", SIDE_PADDING_PX, HDR / 2);

  // Temperature
  tft.setTextDatum(MR_DATUM);
  tft.setTextSize(1);
  if (g_temp_c >= 0) {
    char tbuf[12];
    snprintf(tbuf, sizeof(tbuf), "%.1f C", g_temp_c);
    tft.setTextColor(TFT_CYAN, TFT_NAVY);
    tft.drawString(tbuf, W - 80, HDR / 2);
  } else {
    tft.setTextColor(TFT_DARKGREY, TFT_NAVY);
    tft.drawString("--.- C", W - 80, HDR / 2);
  }

  // Humidity
  if (g_humidity >= 0) {
    char hbuf[12];
    snprintf(hbuf, sizeof(hbuf), "%.0f%%", g_humidity);
    tft.setTextColor(TFT_SKYBLUE, TFT_NAVY);
    tft.drawString(hbuf, W - SIDE_PADDING_PX, HDR / 2);
  } else {
    tft.setTextColor(TFT_DARKGREY, TFT_NAVY);
    tft.drawString("--%", W - SIDE_PADDING_PX, HDR / 2);
  }

  // ── Footer ────────────────────────────────────────────────────────────────
  const int FTR = 36;
  int fy = H - FTR;
  tft.fillRect(0, fy, W, FTR, 0x1082);  // very dark grey

  time_t now = time(nullptr);
  struct tm* tm_info = localtime(&now);

  char date_buf[32], time_buf[12];
  strftime(date_buf, sizeof(date_buf), "%A, %d %b %Y", tm_info);
  strftime(time_buf, sizeof(time_buf),  "%H:%M:%S",      tm_info);

  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(TFT_LIGHTGREY, 0x1082);
  tft.setTextSize(1);
  tft.drawString(date_buf, SIDE_PADDING_PX, fy + FTR / 2);

  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(TFT_WHITE, 0x1082);
  tft.drawString(time_buf, W - SIDE_PADDING_PX, fy + FTR / 2);

  // ── Tiles ─────────────────────────────────────────────────────────────────
  // Two columns, two rows — bottom-right tile is empty for now.
  int tile_area_h = fy - HDR;
  int col0 = 0,       col1 = W / 2;
  int colW = W / 2;
  int row0 = HDR,     row1 = HDR + tile_area_h / 2;
  int rowH = tile_area_h / 2;

  g_home_tiles[HOME_TILE_INVENTORY] = {col0, row0, colW, rowH};
  g_home_tiles[HOME_TILE_SCAN]      = {col1, row0, colW, rowH};
  g_home_tiles[HOME_TILE_STATS]     = {col0, row1, colW, rowH};

  // Tile backgrounds
  char inv_sub[20];
  snprintf(inv_sub, sizeof(inv_sub), "%d items", g_item_count);

  drawHomeTile(g_home_tiles[HOME_TILE_INVENTORY],
               "[]", "INVENTORY", inv_sub, 0x0949, TFT_CYAN);
  drawHomeTile(g_home_tiles[HOME_TILE_SCAN],
               "|>", "SCAN", "Barcode scanner", 0x0820, TFT_GREENYELLOW);
  drawHomeTile(g_home_tiles[HOME_TILE_STATS],
               "##", "THIS MONTH", "Statistics", 0x4800, TFT_ORANGE);

  // Empty bottom-right — subtle placeholder
  tft.fillRoundRect(col1 + 4, row1 + 4, colW - 8, rowH - 8, 12, 0x0841);
  tft.drawRoundRect(col1 + 4, row1 + 4, colW - 8, rowH - 8, 12, 0x2104);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(0x2104, 0x0841);
  tft.setTextSize(1);
  tft.drawString("Coming soon", col1 + colW / 2, row1 + rowH / 2);
}

// ============================================================================
// Inventory rendering
// ============================================================================

// Layout of the scrollable list area — shared by renderInventory() (drawing)
// and handleTouch() (hit-testing), so the two never disagree about where the
// up/down arrow strips and rows actually are.
struct ListLayout {
  int  top, bottom;     // full list area, between header and footer
  bool show_up;         // scrolled past the first item
  int  up_y;             // up-arrow strip top (only valid if show_up)
  int  rows_y0;          // first item row's top
  int  rows_visible;     // how many rows fit given the arrow strips shown
  bool show_down;        // more items below the visible window
  int  down_y;           // down-arrow strip top (only valid if show_down)
};

ListLayout computeListLayout() {
  ListLayout l;
  l.top    = HEADER_HEIGHT_PX + 4;
  l.bottom = tft.height() - FOOTER_HEIGHT_PX - 4;

  int remaining = max(0, g_item_count - g_list_scroll);

  l.show_up = g_list_scroll > 0;
  int avail = l.bottom - l.top - (l.show_up ? SCROLL_ARROW_H : 0);
  int capacity = max(0, avail / ROW_HEIGHT_PX);
  l.show_down = remaining > capacity;
  if (l.show_down) {
    capacity = max(0, (avail - SCROLL_ARROW_H) / ROW_HEIGHT_PX);
    l.show_down = remaining > capacity;
  }
  // Never draw more rows than there actually are items left — capacity is
  // just the screen-space ceiling, not the real count.
  l.rows_visible = min(capacity, remaining);

  l.up_y     = l.top;
  l.rows_y0  = l.top + (l.show_up ? SCROLL_ARROW_H : 0);
  l.down_y   = l.rows_y0 + l.rows_visible * ROW_HEIGHT_PX;
  return l;
}

void drawScrollArrow(int y, bool pointingUp) {
  int w = tft.width();
  tft.fillRect(0, y, w, SCROLL_ARROW_H, 0x2104);
  tft.drawFastHLine(0, y, w, TFT_DARKGREY);
  tft.drawFastHLine(0, y + SCROLL_ARROW_H - 1, w, TFT_DARKGREY);
  int cx = w / 2, cy = y + SCROLL_ARROW_H / 2;
  if (pointingUp) tft.fillTriangle(cx - 10, cy + 6, cx + 10, cy + 6, cx, cy - 6, TFT_LIGHTGREY);
  else             tft.fillTriangle(cx - 10, cy - 6, cx + 10, cy - 6, cx, cy + 6, TFT_LIGHTGREY);
}

void drawItemRow(int index, int y) {
  int w = tft.width();
  int rowH = ROW_HEIGHT_PX - ROW_GAP_PX;
  uint16_t bg = (index % 2 == 0) ? TFT_BLACK : 0x18E3;

  // Gap below the row (between this row and the next) stays plain black.
  tft.fillRect(0, y, w, ROW_HEIGHT_PX, TFT_BLACK);
  tft.fillRect(0, y, w, rowH, bg);
  tft.fillRect(0, y, 4, rowH, confidenceColor(g_items[index].confidence));

  int icon_x = SIDE_PADDING_PX;
  int icon_y = y + (rowH - ICON_SIZE_PX) / 2;
  drawIcon(g_items[index].name, icon_x, icon_y, bg);

  int arrow_x   = w - ROW_ARROW_ZONE_PX;
  int text_x    = icon_x + ICON_SIZE_PX + 10;
  int text_right = arrow_x - 4;
  int text_cy   = y + rowH / 2;
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(TFT_WHITE, bg);
  tft.setTextSize(2);
  drawItemName(g_items[index].name, text_x, text_cy);
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(TFT_CYAN, bg);
  tft.drawString(g_items[index].quantity, text_right, text_cy);

  // Show the earliest expiry date among all units.
  String earliest = "";
  for (int j = 0; j < g_items[index].expiry_count; j++) {
    const String& e = g_items[index].expiries[j];
    if (e.length() == 10 && (earliest == "" || e < earliest))
      earliest = e;
  }
  if (earliest.length() == 10) {
    String ddmmyyyy = earliest.substring(8, 10) + "-" + earliest.substring(5, 7) + "-" + earliest.substring(0, 4);
    tft.setTextSize(1);
    tft.setTextColor(TFT_ORANGE, bg);
    tft.drawString("Exp " + ddmmyyyy, text_right, text_cy + 14);
  }

  // Tappable "open details" arrow, right edge of the row.
  tft.drawFastVLine(arrow_x, y + 6, rowH - 12, TFT_DARKGREY);
  int ax = arrow_x + (ROW_ARROW_ZONE_PX - 18) / 2;
  int ay = y + rowH / 2;
  tft.fillTriangle(ax, ay - 12, ax, ay + 12, ax + 18, ay, TFT_LIGHTGREY);

  tft.drawRect(0, y, w, rowH, TFT_DARKGREY);
}

void drawEmptyState() {
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("No items found", tft.width() / 2, tft.height() / 2);
}

void renderInventory() {
  tft.fillScreen(TFT_BLACK);

  // Header with a "< Back" button in the top-left (replaces the old footer
  // "< Home"): the footer button sat in a screen corner where the touch
  // panel reads least accurately, so it was hard to hit. This mirrors the
  // stats / item-detail header back button. Drawn inline (not via touch.h's
  // drawBtn) because display.h is included before touch.h.
  int wHdr = tft.width();
  tft.fillRect(0, 0, wHdr, HEADER_HEIGHT_PX, TFT_NAVY);
  int bx = 8, by = 6, bw = 70, bh = HEADER_HEIGHT_PX - 12;
  tft.fillRoundRect(bx, by, bw, bh, 6, TFT_DARKGREY);
  tft.drawRoundRect(bx, by, bw, bh, 6, TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.setTextSize(2);
  tft.drawString("< Back", bx + bw / 2, by + bh / 2);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextSize(2);
  tft.drawString("Inventory", wHdr / 2, HEADER_HEIGHT_PX / 2);
  tft.setTextDatum(MR_DATUM);
  tft.setTextSize(1);
  tft.drawString(String(g_item_count) + " items", wHdr - SIDE_PADDING_PX, HEADER_HEIGHT_PX / 2);

  // Clamp scroll in case the list shrank since the last render. Step down
  // one at a time rather than computing this in one shot — rows_visible
  // itself depends on whether the up/down arrows are shown, which depends
  // on the scroll position, so a single formula can under/overshoot.
  while (g_list_scroll > 0 && computeListLayout().rows_visible == 0) g_list_scroll--;

  ListLayout l = computeListLayout();

  if (g_item_count == 0) {
    drawEmptyState();
  } else {
    if (l.show_up) drawScrollArrow(l.up_y, true);
    for (int i = 0; i < l.rows_visible; i++)
      drawItemRow(g_list_scroll + i, l.rows_y0 + i * ROW_HEIGHT_PX);
    if (l.show_down) drawScrollArrow(l.down_y, false);
  }

  // Footer: last-updated timestamp only (the "< Home" button moved up to a
  // "< Back" button in the header — see the header block above).
  int w2 = tft.width();
  int fy = tft.height() - FOOTER_HEIGHT_PX;
  tft.fillRect(0, fy, w2, FOOTER_HEIGHT_PX, TFT_DARKGREY);
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(TFT_LIGHTGREY, TFT_DARKGREY);
  tft.setTextSize(1);
  String upd = g_updated_at.length() > 0 ? "Updated " + g_updated_at : "Waiting for scan...";
  tft.drawString(upd, w2 - SIDE_PADDING_PX, fy + FOOTER_HEIGHT_PX / 2);
}
