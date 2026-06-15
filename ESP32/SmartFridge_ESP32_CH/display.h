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
struct InventoryItem {
  String name;
  String quantity;
  String confidence;
  String expiry;  // "YYYY-MM-DD" or "" if not set
};

InventoryItem g_items[MAX_ITEMS_DISPLAYED];
int    g_item_count = 0;
String g_updated_at = "";

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
// Inventory rendering
// ============================================================================
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
  tft.drawString(g_items[index].name, text_x, text_cy);
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(TFT_CYAN, bg);
  tft.drawString(g_items[index].quantity, text_right, text_cy);

  if (g_items[index].expiry.length() == 10) {
    const String& e = g_items[index].expiry;  // "YYYY-MM-DD"
    String ddmmyyyy = e.substring(8, 10) + "-" + e.substring(5, 7) + "-" + e.substring(0, 4);
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
  drawHeader();

  int list_top    = HEADER_HEIGHT_PX + 4;
  int list_bottom = tft.height() - FOOTER_HEIGHT_PX - 4;
  int max_rows    = (list_bottom - list_top) / ROW_HEIGHT_PX;

  if (g_item_count == 0) {
    drawEmptyState();
  } else {
    int rows = min(g_item_count, max_rows);
    for (int i = 0; i < rows; i++)
      drawItemRow(i, list_top + i * ROW_HEIGHT_PX);
    if (g_item_count > max_rows) {
      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(TFT_ORANGE, TFT_BLACK);
      tft.setTextSize(1);
      tft.drawString("+ " + String(g_item_count - max_rows) + " more",
                     tft.width() / 2, list_bottom);
    }
  }

  drawFooter(g_updated_at.length() > 0
               ? "Updated " + g_updated_at
               : "Waiting for first scan...");
}
