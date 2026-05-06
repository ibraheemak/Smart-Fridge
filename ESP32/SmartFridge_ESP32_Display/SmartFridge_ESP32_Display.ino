/*
 * Smart Fridge — ESP32 + ILI9488 Display
 *
 * Companion device to SmartFridge_ESP32_CAM. Polls the Firestore document
 * fridges/{FRIDGE_ID}/inventory/current and renders the current items on
 * an ILI9488 TFT using TFT_eSPI.
 *
 * Configure TFT_eSPI's User_Setup.h to match your wiring before flashing.
 * See parameters.h for recommended pinout.
 */

#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include "time.h"

#include "SECRETS.h"
#include "parameters.h"

// ============================================================================
// GLOBALS
// ============================================================================
TFT_eSPI tft = TFT_eSPI();

struct InventoryItem {
  String name;
  String quantity;
  String confidence;
};

InventoryItem g_items[MAX_ITEMS_DISPLAYED];
int           g_item_count = 0;
String        g_updated_at = "";
String        g_last_signature = "";   // hash-ish string used to skip redraws
unsigned long g_last_poll_ms   = 0;

// ============================================================================
// DISPLAY HELPERS
// ============================================================================
void backlightOn() {
  if (TFT_BL_PIN >= 0) {
    pinMode(TFT_BL_PIN, OUTPUT);
    digitalWrite(TFT_BL_PIN, TFT_BL_ON);
  }
}

void drawHeader() {
  int w = tft.width();
  tft.fillRect(0, 0, w, HEADER_HEIGHT_PX, TFT_NAVY);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextSize(2);
  tft.drawString("Smart Fridge", SIDE_PADDING_PX, HEADER_HEIGHT_PX / 2);

  tft.setTextDatum(MR_DATUM);
  tft.setTextSize(1);
  String count = String(g_item_count) + " items";
  tft.drawString(count, w - SIDE_PADDING_PX, HEADER_HEIGHT_PX / 2);
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

// ============================================================================
// ICON FETCH & RENDER (Firebase Storage)
// ============================================================================
// Decode JPEG into an off-screen RGB565 buffer, then area-average resample
// down to ICON_SIZE_PX. This produces much sharper icons than TJpg's built-in
// ÷2/÷4/÷8 block downscale. Source image is intended to be larger than the
// icon (e.g. 256x256) so we have detail to filter from.

// Direct-to-TFT callback (used as a tiny-source fallback path)
bool tftJpgOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  if (y >= tft.height()) return 0;
  tft.pushImage(x, y, w, h, bitmap);
  return 1;
}

// Buffer the decoder writes into; consulted by decodeBufOutput().
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

// Area-average downscale (box filter) in RGB565. Each destination pixel
// averages all source pixels covering its area — anti-aliased and sharp.
// Requires sw >= dw and sh >= dh (downscale only).
void resampleAreaRGB565(const uint16_t* src, int sw, int sh,
                        uint16_t* dst, int dw, int dh) {
  for (int dy = 0; dy < dh; dy++) {
    int iy0 = (long)dy * sh / dh;
    int iy1 = (long)(dy + 1) * sh / dh;
    if (iy1 <= iy0) iy1 = iy0 + 1;
    if (iy1 > sh) iy1 = sh;

    for (int dx = 0; dx < dw; dx++) {
      int ix0 = (long)dx * sw / dw;
      int ix1 = (long)(dx + 1) * sw / dw;
      if (ix1 <= ix0) ix1 = ix0 + 1;
      if (ix1 > sw) ix1 = sw;

      uint32_t sR = 0, sG = 0, sB = 0, n = 0;
      for (int y = iy0; y < iy1; y++) {
        const uint16_t* row = src + (long)y * sw;
        for (int x = ix0; x < ix1; x++) {
          uint16_t p = row[x];
          sR += (p >> 11) & 0x1F;
          sG += (p >> 5)  & 0x3F;
          sB +=  p        & 0x1F;
          n++;
        }
      }
      uint16_t R = (uint16_t)(sR / n);
      uint16_t G = (uint16_t)(sG / n);
      uint16_t B = (uint16_t)(sB / n);
      dst[(long)dy * dw + dx] = (R << 11) | (G << 5) | B;
    }
  }
}

// Percent-encode a Firebase Storage object path. Unreserved chars pass through;
// '/' becomes %2F, spaces become %20, etc. Item names are used as-is.
String encodeStoragePath(const String& p) {
  static const char* hex = "0123456789ABCDEF";
  String out;
  for (size_t i = 0; i < p.length(); i++) {
    unsigned char c = (unsigned char) p[i];
    bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                      (c >= '0' && c <= '9') ||
                      c == '-' || c == '_' || c == '.' || c == '~';
    if (unreserved) {
      out += (char)c;
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 0x0F];
    }
  }
  return out;
}

// Download icon JPEG into a heap buffer. Caller frees on success.
// Uses the item name verbatim — file in Storage must match (e.g. "soft drink.jpg").
bool fetchIconJpeg(const String& item_name, uint8_t** out_buf, size_t* out_len) {
  if (item_name.length() == 0) return false;

  String path    = String(ICON_PATH_PREFIX) + item_name + ICON_EXTENSION;
  String enc     = encodeStoragePath(path);
  String url     = "https://firebasestorage.googleapis.com/v0/b/" +
                   String(FIREBASE_STORAGE_BUCKET) + "/o/" + enc + "?alt=media";
  Serial.printf("[ICON] GET %s\n", url.c_str());

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(client, url)) return false;

  int code = http.GET();
  if (code != 200) {
    Serial.printf("[ICON] %s -> %d\n", item_name.c_str(), code);
    http.end();
    return false;
  }

  int len = http.getSize();
  if (len <= 0 || len > 256 * 1024) {   // sanity cap — allow large hi-res icons
    Serial.printf("[ICON] %s bad size %d\n", item_name.c_str(), len);
    http.end();
    return false;
  }

  uint8_t* buf = (uint8_t*) malloc(len);
  if (!buf) {
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  int read = 0;
  unsigned long t0 = millis();
  while (http.connected() && read < len && millis() - t0 < 8000) {
    size_t avail = stream->available();
    if (avail) {
      int n = stream->readBytes(buf + read, min((int)avail, len - read));
      if (n <= 0) break;
      read += n;
    } else {
      delay(2);
    }
  }
  http.end();

  if (read != len) {
    Serial.printf("[ICON] %s short read %d/%d\n", item_name.c_str(), read, len);
    free(buf);
    return false;
  }

  *out_buf = buf;
  *out_len = (size_t)len;
  return true;
}

// Cap on the off-screen JPEG decode buffer. ~96KB fits in PSRAM comfortably and
// in DRAM at the edge — TJpg will be told to halve until we're under this.
static const size_t ICON_MAX_SRC_BYTES = 96 * 1024;

void drawIcon(const String& name, int x, int y, uint16_t bg) {
  tft.fillRect(x, y, ICON_SIZE_PX, ICON_SIZE_PX, bg);

  uint8_t* jbuf = nullptr;
  size_t   jlen = 0;
  if (!fetchIconJpeg(name, &jbuf, &jlen)) {
    tft.drawRect(x, y, ICON_SIZE_PX, ICON_SIZE_PX, TFT_DARKGREY);
    return;
  }

  uint16_t jw = 0, jh = 0;
  if (TJpgDec.getJpgSize(&jw, &jh, jbuf, jlen) != 0 || jw == 0 || jh == 0) {
    Serial.printf("[ICON] %s: getJpgSize failed (bad JPEG?)\n", name.c_str());
    free(jbuf);
    tft.drawRect(x, y, ICON_SIZE_PX, ICON_SIZE_PX, TFT_RED);
    return;
  }
  Serial.printf("[ICON] %s native %ux%u\n", name.c_str(), jw, jh);

  // Pick TJpg decode scale: halve until source buffer fits, but keep source
  // >= target so we always downscale (never upscale) at the resample step.
  uint8_t dec_scale = 1;
  while (dec_scale < 8) {
    int sw = jw / dec_scale;
    int sh = jh / dec_scale;
    size_t bytes = (size_t)sw * sh * 2;
    if (bytes <= ICON_MAX_SRC_BYTES) break;
    dec_scale <<= 1;
  }
  // If even at scale=8 the source is below the icon size, fall back to TJpg's
  // direct-to-TFT path (rare — would need a tiny source).
  int sw = jw / dec_scale;
  int sh = jh / dec_scale;
  if (sw < ICON_SIZE_PX || sh < ICON_SIZE_PX) {
    Serial.printf("[ICON] %s: source %dx%d too small, drawing direct\n",
                  name.c_str(), sw, sh);
    TJpgDec.setJpgScale(1);
    TJpgDec.setCallback(tftJpgOutput);
    TJpgDec.drawJpg(x, y, jbuf, jlen);
    free(jbuf);
    return;
  }

  // Allocate decode buffer — prefer PSRAM since icons can be large
  size_t sbytes = (size_t)sw * sh * 2;
  uint16_t* sbuf = nullptr;
  if (psramFound()) sbuf = (uint16_t*) ps_malloc(sbytes);
  if (!sbuf)        sbuf = (uint16_t*) malloc(sbytes);
  if (!sbuf) {
    Serial.printf("[ICON] %s: alloc %u failed\n", name.c_str(), (unsigned)sbytes);
    free(jbuf);
    tft.drawRect(x, y, ICON_SIZE_PX, ICON_SIZE_PX, TFT_RED);
    return;
  }

  g_dec.px = sbuf;
  g_dec.w  = sw;
  g_dec.h  = sh;
  TJpgDec.setJpgScale(dec_scale);
  TJpgDec.setCallback(decodeBufOutput);
  int rc = TJpgDec.drawJpg(0, 0, jbuf, jlen);   // 0,0 because we offset in callback
  free(jbuf);
  g_dec.px = nullptr;

  if (rc != 0) {
    Serial.printf("[ICON] %s decode failed rc=%d (progressive/CMYK?)\n", name.c_str(), rc);
    free(sbuf);
    tft.drawRect(x, y, ICON_SIZE_PX, ICON_SIZE_PX, TFT_RED);
    return;
  }

  // Area-average resample sw×sh -> ICON_SIZE_PX × ICON_SIZE_PX
  size_t dbytes = (size_t)ICON_SIZE_PX * ICON_SIZE_PX * 2;
  uint16_t* dbuf = (uint16_t*) malloc(dbytes);
  if (!dbuf) {
    Serial.printf("[ICON] %s: dst alloc failed\n", name.c_str());
    free(sbuf);
    return;
  }
  resampleAreaRGB565(sbuf, sw, sh, dbuf, ICON_SIZE_PX, ICON_SIZE_PX);
  free(sbuf);

  tft.pushImage(x, y, ICON_SIZE_PX, ICON_SIZE_PX, dbuf);
  free(dbuf);
}

void drawItemRow(int index, int y) {
  int w = tft.width();
  uint16_t bg = (index % 2 == 0) ? TFT_BLACK : 0x18E3;  // very dark blue/grey
  tft.fillRect(0, y, w, ROW_HEIGHT_PX, bg);

  // Confidence color bar on the left edge
  tft.fillRect(0, y, 4, ROW_HEIGHT_PX, confidenceColor(g_items[index].confidence));

  // Icon from Firebase Storage
  int icon_x = SIDE_PADDING_PX;
  int icon_y = y + (ROW_HEIGHT_PX - ICON_SIZE_PX) / 2;
  drawIcon(g_items[index].name, icon_x, icon_y, bg);

  // Item name
  int text_x = icon_x + ICON_SIZE_PX + 10;
  int text_cy = y + ROW_HEIGHT_PX / 2;
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(TFT_WHITE, bg);
  tft.setTextSize(2);
  tft.drawString(g_items[index].name, text_x, text_cy);

  // Quantity (right-aligned)
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(TFT_CYAN, bg);
  tft.drawString(g_items[index].quantity, w - SIDE_PADDING_PX, text_cy);
}

void drawEmptyState() {
  int w = tft.width();
  int h = tft.height();
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("No items found", w / 2, h / 2);
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
    for (int i = 0; i < rows; i++) {
      drawItemRow(i, list_top + i * ROW_HEIGHT_PX);
    }
    if (g_item_count > max_rows) {
      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(TFT_ORANGE, TFT_BLACK);
      tft.setTextSize(1);
      String more = "+ " + String(g_item_count - max_rows) + " more";
      tft.drawString(more, tft.width() / 2, list_bottom);
    }
  }

  String footer = g_updated_at.length() > 0
                    ? "Updated " + g_updated_at
                    : "Waiting for first scan...";
  drawFooter(footer);
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
// FIRESTORE FETCH
// ============================================================================
// Builds a stable signature string for the current inventory so we only
// repaint the screen when something actually changed.
String buildSignature() {
  String sig = g_updated_at + "|";
  for (int i = 0; i < g_item_count; i++) {
    sig += g_items[i].name + ":" + g_items[i].quantity + ";";
  }
  return sig;
}

bool fetchInventory() {
  if (WiFi.status() != WL_CONNECTED) return false;

  String url =
    "https://firestore.googleapis.com/v1/projects/" +
    String(FIREBASE_PROJECT_ID) +
    "/databases/(default)/documents/fridges/" +
    String(FRIDGE_ID) +
    "/inventory/current?key=" +
    String(FIREBASE_API_KEY);

  WiFiClientSecure client;
  client.setInsecure();   // Firestore cert pinning is out of scope for this device

  HTTPClient http;
  http.setTimeout(10000);
  if (!http.begin(client, url)) {
    Serial.println("[FIREBASE] http.begin failed");
    return false;
  }

  int code = http.GET();
  if (code != 200) {
    Serial.printf("[FIREBASE] GET inventory/current -> %d\n", code);
    http.end();
    return false;
  }

  String body = http.getString();
  http.end();

  DynamicJsonDocument doc(8192);
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.printf("[FIREBASE] JSON parse error: %s\n", err.c_str());
    return false;
  }

  JsonObject fields = doc["fields"];
  if (fields.isNull()) {
    Serial.println("[FIREBASE] No 'fields' in document");
    return false;
  }

  g_updated_at = fields["updatedAt"]["stringValue"].as<String>();

  JsonArray values = fields["items"]["arrayValue"]["values"];
  g_item_count = 0;
  for (JsonObject v : values) {
    if (g_item_count >= MAX_ITEMS_DISPLAYED) break;
    JsonObject mf = v["mapValue"]["fields"];
    g_items[g_item_count].name       = mf["name"]["stringValue"].as<String>();
    g_items[g_item_count].quantity   = mf["quantity"]["stringValue"].as<String>();
    g_items[g_item_count].confidence = mf["confidence"]["stringValue"].as<String>();
    g_item_count++;
  }

  Serial.printf("[FIREBASE] Fetched %d items, updatedAt=%s\n",
                g_item_count, g_updated_at.c_str());
  return true;
}

// ============================================================================
// WIFI (WiFiManager — same model as the CAM sketch)
// ============================================================================
void checkResetButton() {
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
  if (digitalRead(RESET_BUTTON_PIN) != LOW) return;

  Serial.println("[WIFI] BOOT held — keep holding to wipe credentials...");
  unsigned long t0 = millis();
  while (digitalRead(RESET_BUTTON_PIN) == LOW) {
    if (millis() - t0 >= RESET_HOLD_MS) {
      WiFiManager wm;
      wm.resetSettings();
      Serial.println("[WIFI] Credentials wiped, restarting...");
      delay(1000);
      ESP.restart();
    }
    delay(50);
  }
}

void initWiFi() {
  showStatus("Connecting WiFi", "AP: " WIFI_AP_NAME);

  WiFiManager wm;
  wm.setConnectTimeout(20);
  wm.setConfigPortalTimeout(WIFI_PORTAL_TIMEOUT_S);

  wm.setAPCallback([](WiFiManager*) {
    Serial.printf("[WIFI] Open AP \"%s\" -> http://192.168.4.1\n", WIFI_AP_NAME);
    showStatus("Setup needed", "Join " WIFI_AP_NAME);
  });

  if (!wm.autoConnect(WIFI_AP_NAME)) {
    Serial.println("[WIFI] Portal timed out, restarting");
    showStatus("WiFi failed", "Restarting...");
    delay(3000);
    ESP.restart();
  }

  Serial.printf("[WIFI] Connected: %s (%s)\n",
                WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
}

void configureTime() {
  configTzTime(TIMEZONE, "pool.ntp.org", "time.nist.gov");
  time_t now = time(nullptr);
  unsigned long t0 = millis();
  while (now < 24 * 3600 && millis() - t0 < 10000) {
    delay(250);
    now = time(nullptr);
  }
}

// ============================================================================
// SETUP / LOOP
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[BOOT] SmartFridge Display starting");

  backlightOn();
  tft.init();
  tft.setRotation(DISPLAY_ROTATION);
  tft.fillScreen(TFT_BLACK);

  showStatus("Smart Fridge", "Booting...");

  checkResetButton();
  initWiFi();
  configureTime();

  showStatus("Loading inventory", "");

  if (fetchInventory()) {
    g_last_signature = buildSignature();
    renderInventory();
  } else {
    showStatus("No data yet", "Waiting for fridge scan");
  }
  g_last_poll_ms = millis();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[LOOP] WiFi lost — restarting");
    showStatus("WiFi lost", "Restarting...");
    delay(2000);
    ESP.restart();
  }

  if (millis() - g_last_poll_ms >= INVENTORY_POLL_INTERVAL_MS) {
    g_last_poll_ms = millis();
    if (fetchInventory()) {
      String sig = buildSignature();
      if (sig != g_last_signature) {
        g_last_signature = sig;
        renderInventory();
      }
    }
  }

  delay(50);
}
