/*
 * SmartFridge ESP32-CAM Combined
 *
 * Camera scanning (Gemini AI → Firestore) and ILI9488 TFT inventory display
 * running on a single ESP32-CAM board.
 *
 * GPIO changes vs the separate sketches:
 *   TFT DC   : GPIO 4  → GPIO 33  (GPIO 4 is the camera flash)
 *   LED strip: GPIO 14 → GPIO 2   (GPIO 14 freed for TFT SCK)
 *
 * After each scan the display updates immediately from the scan result.
 * A background poll (INVENTORY_POLL_INTERVAL_MS) catches external Firestore edits.
 */

#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include "time.h"
#include <WebServer.h>
#include "mbedtls/base64.h"
#include "SECRETS.h"
#include "parameters.h"
#include "led_strip.h"

// ============================================================================
// GLOBALS
// ============================================================================
TFT_eSPI tft = TFT_eSPI();

struct InventoryItem { String name, quantity, confidence; };
InventoryItem g_items[MAX_ITEMS_DISPLAYED];
int           g_item_count     = 0;
String        g_updated_at     = "";
String        g_last_signature = "";
unsigned long g_last_poll_ms   = 0;

WebServer webServer(80);
uint8_t*  latest_jpeg      = nullptr;
size_t    latest_jpeg_size = 0;

// ============================================================================
// WEB SERVER — /latest.jpg debug endpoint
// ============================================================================
void handleLatestJpeg() {
  WiFiClient client = webServer.client();
  if (latest_jpeg && latest_jpeg_size > 0) {
    String header = "HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\n";
    header += "Content-Length: " + String(latest_jpeg_size) + "\r\n";
    header += "Cache-Control: no-cache\r\nConnection: close\r\n\r\n";
    client.print(header);
    client.write(latest_jpeg, latest_jpeg_size);
    client.flush();
  } else {
    webServer.send(404, "text/plain", "No image captured yet.");
  }
}

// ============================================================================
// DISPLAY HELPERS
// ============================================================================
void backlightOn() {
  if (TFT_BL_PIN >= 0) { pinMode(TFT_BL_PIN, OUTPUT); digitalWrite(TFT_BL_PIN, TFT_BL_ON); }
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

uint16_t confidenceColor(const String& c) {
  if (c.equalsIgnoreCase("high"))   return TFT_GREEN;
  if (c.equalsIgnoreCase("medium")) return TFT_YELLOW;
  if (c.equalsIgnoreCase("low"))    return TFT_RED;
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
// ICON FETCH & RENDER (Firebase Storage, area-average downscale)
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
    int copy_w = min((int)w, g_dec.w - dx0);
    if (copy_w <= 0) continue;
    memcpy(&g_dec.px[dy * g_dec.w + dx0], &bitmap[row * w], copy_w * 2);
  }
  return 1;
}

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
          p = (p >> 8) | (p << 8);
          sR += (p >> 11) & 0x1F;
          sG += (p >> 5)  & 0x3F;
          sB +=  p        & 0x1F;
          n++;
        }
      }
      uint16_t out = ((uint16_t)(sR/n) << 11) | ((uint16_t)(sG/n) << 5) | (uint16_t)(sB/n);
      dst[(long)dy * dw + dx] = (out >> 8) | (out << 8);
    }
  }
}

String encodeStoragePath(const String& p) {
  static const char* hex = "0123456789ABCDEF";
  String out;
  for (size_t i = 0; i < p.length(); i++) {
    unsigned char c = (unsigned char)p[i];
    bool unreserved = (c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||
                      c=='-'||c=='_'||c=='.'||c=='~';
    if (unreserved) out += (char)c;
    else { out += '%'; out += hex[c>>4]; out += hex[c&0x0F]; }
  }
  return out;
}

bool fetchIconJpeg(const String& item_name, uint8_t** out_buf, size_t* out_len) {
  if (item_name.length() == 0) return false;
  String path = String(ICON_PATH_PREFIX) + item_name + ICON_EXTENSION;
  String url  = "https://firebasestorage.googleapis.com/v0/b/" +
                String(FIREBASE_STORAGE_BUCKET) + "/o/" + encodeStoragePath(path) + "?alt=media";

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(client, url)) return false;

  int code = http.GET();
  if (code != 200) { http.end(); return false; }

  int len = http.getSize();
  const int MAX_ICON_BYTES = 256 * 1024;
  if (len > MAX_ICON_BYTES) { http.end(); return false; }

  int buf_size = (len > 0) ? len : MAX_ICON_BYTES;
  uint8_t* buf = (uint8_t*)malloc(buf_size);
  if (!buf) { http.end(); return false; }

  WiFiClient* stream = http.getStreamPtr();
  int read = 0, target = (len > 0) ? len : buf_size;
  unsigned long t0 = millis();
  while (http.connected() && read < target && millis() - t0 < 8000) {
    size_t avail = stream->available();
    if (avail) {
      int n = stream->readBytes(buf + read, min((int)avail, target - read));
      if (n <= 0) break;
      read += n;
    } else { delay(2); }
  }
  http.end();

  if ((len > 0 && read != len) || read < 2 || buf[0] != 0xFF || buf[1] != 0xD8) {
    free(buf); return false;
  }
  *out_buf = buf;
  *out_len = (size_t)(len > 0 ? len : read);
  return true;
}

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
    TJpgDec.setJpgScale(1);
    TJpgDec.setCallback(tftJpgOutput);
    if (TJpgDec.drawJpg(x, y, jbuf, jlen) != 0)
      tft.drawRect(x, y, ICON_SIZE_PX, ICON_SIZE_PX, TFT_RED);
    free(jbuf);
    return;
  }

  uint8_t dec_scale = 1;
  while (dec_scale < 8 &&
         (size_t)(jw/dec_scale) * (jh/dec_scale) * 2 > ICON_MAX_SRC_BYTES)
    dec_scale <<= 1;

  int sw = jw / dec_scale, sh = jh / dec_scale;
  if (sw < ICON_SIZE_PX || sh < ICON_SIZE_PX) {
    TJpgDec.setJpgScale(1);
    TJpgDec.setCallback(tftJpgOutput);
    TJpgDec.drawJpg(x, y, jbuf, jlen);
    free(jbuf);
    return;
  }

  size_t sbytes = (size_t)sw * sh * 2;
  uint16_t* sbuf = psramFound() ? (uint16_t*)ps_malloc(sbytes) : nullptr;
  if (!sbuf) sbuf = (uint16_t*)malloc(sbytes);
  if (!sbuf) {
    free(jbuf);
    tft.drawRect(x, y, ICON_SIZE_PX, ICON_SIZE_PX, TFT_RED);
    return;
  }

  g_dec = {sbuf, sw, sh};
  TJpgDec.setJpgScale(dec_scale);
  TJpgDec.setCallback(decodeBufOutput);
  int rc = TJpgDec.drawJpg(0, 0, jbuf, jlen);
  free(jbuf);
  g_dec.px = nullptr;

  if (rc != 0) {
    free(sbuf);
    tft.drawRect(x, y, ICON_SIZE_PX, ICON_SIZE_PX, TFT_RED);
    return;
  }

  size_t dbytes = (size_t)ICON_SIZE_PX * ICON_SIZE_PX * 2;
  uint16_t* dbuf = (uint16_t*)malloc(dbytes);
  if (!dbuf) { free(sbuf); return; }
  resampleAreaRGB565(sbuf, sw, sh, dbuf, ICON_SIZE_PX, ICON_SIZE_PX);
  free(sbuf);
  tft.pushImage(x, y, ICON_SIZE_PX, ICON_SIZE_PX, dbuf);
  free(dbuf);
}

void drawItemRow(int index, int y) {
  int w = tft.width();
  uint16_t bg = (index % 2 == 0) ? TFT_BLACK : 0x18E3;
  tft.fillRect(0, y, w, ROW_HEIGHT_PX, bg);
  tft.fillRect(0, y, 4, ROW_HEIGHT_PX, confidenceColor(g_items[index].confidence));
  int icon_x = SIDE_PADDING_PX;
  int icon_y = y + (ROW_HEIGHT_PX - ICON_SIZE_PX) / 2;
  drawIcon(g_items[index].name, icon_x, icon_y, bg);
  int text_x  = icon_x + ICON_SIZE_PX + 10;
  int text_cy = y + ROW_HEIGHT_PX / 2;
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(TFT_WHITE, bg);
  tft.setTextSize(2);
  tft.drawString(g_items[index].name, text_x, text_cy);
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(TFT_CYAN, bg);
  tft.drawString(g_items[index].quantity, w - SIDE_PADDING_PX, text_cy);
}

void renderInventory() {
  tft.fillScreen(TFT_BLACK);
  drawHeader();
  int list_top    = HEADER_HEIGHT_PX + 4;
  int list_bottom = tft.height() - FOOTER_HEIGHT_PX - 4;
  int max_rows    = (list_bottom - list_top) / ROW_HEIGHT_PX;
  if (g_item_count == 0) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setTextSize(2);
    tft.drawString("No items found", tft.width() / 2, tft.height() / 2);
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
  String footer = g_updated_at.length() > 0
                    ? "Updated " + g_updated_at
                    : "Waiting for first scan...";
  drawFooter(footer);
}

String buildSignature() {
  String sig = g_updated_at + "|";
  for (int i = 0; i < g_item_count; i++)
    sig += g_items[i].name + ":" + g_items[i].quantity + ";";
  return sig;
}

// Populate display directly from a scan result — no Firestore round-trip needed.
void updateDisplayFromItems(JsonDocument& items_doc, const String& timestamp) {
  g_updated_at = timestamp;
  g_item_count = 0;
  if (items_doc.containsKey("items")) {
    for (JsonObject item : items_doc["items"].as<JsonArray>()) {
      if (g_item_count >= MAX_ITEMS_DISPLAYED) break;
      g_items[g_item_count].name       = item["name"].as<String>();
      g_items[g_item_count].quantity   = item["quantity"].as<String>();
      g_items[g_item_count].confidence = item["confidence"].as<String>();
      g_item_count++;
    }
  }
  g_last_signature = buildSignature();
  g_last_poll_ms   = millis();
  renderInventory();
}

// ============================================================================
// CAMERA
// ============================================================================
void initCamera() {
  Serial.println("[CAMERA] Initializing...");
  camera_config_t config = {};
  config.ledc_channel  = (ledc_channel_t)CAMERA_LEDC_CHANNEL;
  config.ledc_timer    = LEDC_TIMER_0;
  config.pin_d0        = Y2_GPIO_NUM;
  config.pin_d1        = Y3_GPIO_NUM;
  config.pin_d2        = Y4_GPIO_NUM;
  config.pin_d3        = Y5_GPIO_NUM;
  config.pin_d4        = Y6_GPIO_NUM;
  config.pin_d5        = Y7_GPIO_NUM;
  config.pin_d6        = Y8_GPIO_NUM;
  config.pin_d7        = Y9_GPIO_NUM;
  config.pin_xclk      = XCLK_GPIO_NUM;
  config.pin_pclk      = PCLK_GPIO_NUM;
  config.pin_vsync     = VSYNC_GPIO_NUM;
  config.pin_href      = HREF_GPIO_NUM;
  config.pin_sccb_sda  = SIOD_GPIO_NUM;
  config.pin_sccb_scl  = SIOC_GPIO_NUM;
  config.pin_pwdn      = PWDN_GPIO_NUM;
  config.pin_reset     = RESET_GPIO_NUM;
  config.xclk_freq_hz  = CAMERA_XCLK_FREQ;
  config.pixel_format  = PIXFORMAT_JPEG;
  config.jpeg_quality  = CAMERA_JPEG_QUALITY;
  config.grab_mode     = CAMERA_GRAB_WHEN_EMPTY;
  if (psramFound()) {
    config.frame_size   = FRAMESIZE_SVGA;
    config.fb_count     = 2;
    config.fb_location  = CAMERA_FB_IN_PSRAM;
  } else {
    config.frame_size   = FRAMESIZE_CIF;
    config.fb_count     = 1;
    config.fb_location  = CAMERA_FB_IN_DRAM;
  }

  esp_err_t err = ESP_FAIL;
  for (int attempt = 1; attempt <= 5 && err != ESP_OK; attempt++) {
    err = esp_camera_init(&config);
    if (err != ESP_OK) { esp_camera_deinit(); delay(500 * attempt); }
  }
  if (err != ESP_OK) {
    Serial.printf("[CAMERA] Failed: 0x%x\n", err);
    showStatus("Camera failed", "Restarting...");
    delay(2000);
    ESP.restart();
  }

  sensor_t* s = esp_camera_sensor_get();
  s->set_brightness(s, 0);   s->set_contrast(s, 0);     s->set_saturation(s, 0);
  s->set_special_effect(s, 0); s->set_whitebal(s, 1);   s->set_awb_gain(s, 1);
  s->set_wb_mode(s, 0);       s->set_exposure_ctrl(s, 1); s->set_aec_value(s, 400);
  s->set_aec2(s, 1);          s->set_agc_gain(s, 2);    s->set_gainceiling(s, GAINCEILING_8X);
  s->set_dcw(s, 1);           s->set_bpc(s, 0);         s->set_wpc(s, 1);
  s->set_raw_gma(s, 1);       s->set_lenc(s, 1);        s->set_hmirror(s, 0);
  s->set_vflip(s, 1);         s->set_colorbar(s, 0);
#if DEBUG_MODE
  for (int i = 0; i < 3; i++) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);
    delay(CAMERA_WARMUP_DELAY_MS);
  }
  Serial.println("[CAMERA] Warmup done");
#endif
  Serial.println("[CAMERA] Ready");
}

void initFlash() {
  ledcAttach(FLASH_GPIO_NUM, 50000, 8);
  ledcWrite(FLASH_GPIO_NUM, 0);
}

void turnOnFlash()  { ledcWrite(FLASH_GPIO_NUM, FLASH_PWM_DUTY); }
void turnOffFlash() { ledcWrite(FLASH_GPIO_NUM, 0); }

// ============================================================================
// CAPTURE
// ============================================================================
uint8_t* capturePhoto(size_t* photo_size) {
  turnOnFlash();
  delay(100);
  for (int i = 0; i < 3; i++) {
    camera_fb_t* stale = esp_camera_fb_get();
    if (stale) esp_camera_fb_return(stale);
    delay(30);
  }
  delay(FLASH_DURATION_MS);

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) { turnOffFlash(); return nullptr; }
  turnOffFlash();

  uint8_t* data = (uint8_t*)malloc(fb->len);
  if (!data) { esp_camera_fb_return(fb); return nullptr; }
  memcpy(data, fb->buf, fb->len);
  *photo_size = fb->len;

  if (latest_jpeg) { free(latest_jpeg); latest_jpeg = nullptr; }
  latest_jpeg = (uint8_t*)malloc(fb->len);
  if (latest_jpeg) { memcpy(latest_jpeg, fb->buf, fb->len); latest_jpeg_size = fb->len; }

  esp_camera_fb_return(fb);
  Serial.printf("[CAPTURE] %d bytes\n", *photo_size);
  return data;
}

// ============================================================================
// GEMINI
// ============================================================================
String sendToGemini(uint8_t* photo_data, size_t photo_size, const String& basic_items) {
  String prompt =
    "Analyze this refrigerator image. Identify all visible food items and estimate quantities.";
  if (basic_items.length() > 0)
    prompt += " When an item matches one of these canonical names use it exactly: [" + basic_items +
              "]. For any item NOT in that list, describe it in lowercase.";
#if DEBUG_MODE
  prompt += " Also add a top-level \\\"description\\\" field with a brief note on image quality and lighting.";
#endif
  prompt += " Return valid JSON only: {\\\"items\\\": [{\\\"name\\\": \\\"item name\\\","
            " \\\"quantity\\\": \\\"amount\\\", \\\"confidence\\\": \\\"high/medium/low\\\"}]}";

  const char* mid  = "\"},{\"inlineData\":{\"mimeType\":\"image/jpeg\",\"data\":\"";
  const char* tail = "\"}}]}]}";
  String prefix_str = String("{\"contents\":[{\"parts\":[{\"text\":\"") + prompt + mid;
  size_t prefix_len = prefix_str.length(), tail_len = strlen(tail);

  size_t enc_len = 0;
  mbedtls_base64_encode(nullptr, 0, &enc_len, photo_data, photo_size);

  char* body = (char*)malloc(prefix_len + enc_len + tail_len + 1);
  if (!body) { Serial.println("[GEMINI] alloc failed"); return ""; }

  memcpy(body, prefix_str.c_str(), prefix_len);
  size_t actual_enc = 0;
  if (mbedtls_base64_encode((unsigned char*)(body + prefix_len), enc_len,
                            &actual_enc, photo_data, photo_size) != 0) {
    free(body); return "";
  }
  memcpy(body + prefix_len + actual_enc, tail, tail_len);
  size_t body_len = prefix_len + actual_enc + tail_len;
  body[body_len] = '\0';

  String url = String(GEMINI_API_ENDPOINT) + "?key=" + GEMINI_API_KEY;
  String response = "";

  for (int attempt = 1; attempt <= GEMINI_MAX_RETRIES + 1; attempt++) {
    HTTPClient http;
    http.begin(url);
    http.setTimeout(GEMINI_REQUEST_TIMEOUT_MS);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST((uint8_t*)body, body_len);
    if (code == 200) {
      response = http.getString();
      http.end();
      break;
    } else if (code == 503 && attempt <= GEMINI_MAX_RETRIES) {
      Serial.printf("[GEMINI] 503 — retrying (%d/%d)\n", attempt, GEMINI_MAX_RETRIES + 1);
      http.end();
      delay(5000);
    } else {
      Serial.printf("[GEMINI] HTTP %d\n", code);
      http.end();
      break;
    }
  }
  free(body);
  return response;
}

bool parseGeminiResponse(String response, JsonDocument& detected_items) {
  StaticJsonDocument<8192> full_response;
  if (deserializeJson(full_response, response)) return false;

  const char* text = full_response["candidates"][0]["content"]["parts"][0]["text"];
  if (!text) return false;

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

  return !deserializeJson(detected_items, json_text);
}

// ============================================================================
// FIREBASE / FIRESTORE
// ============================================================================
String getFormattedTimestamp() {
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  char buf[20];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", t);
  return String(buf);
}

String getISOTimestamp() {
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  char buf[20];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H-%M-%S", t);
  return String(buf);
}

String getWeekId() {
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  char buf[14];
  snprintf(buf, sizeof(buf), "%04d-%02d-W%d", t->tm_year+1900, t->tm_mon+1, (t->tm_mday-1)/7+1);
  return String(buf);
}

String getMonthId() {
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  char buf[8];
  strftime(buf, sizeof(buf), "%Y-%m", t);
  return String(buf);
}

String fetchBasicItems() {
  String url = "https://firestore.googleapis.com/v1/projects/" + String(FIREBASE_PROJECT_ID) +
               "/databases/(default)/documents/basic-items/basic-items?key=" + String(FIREBASE_API_KEY);
  HTTPClient http;
  http.begin(url);
  if (http.GET() != 200) { http.end(); return ""; }
  String body = http.getString();
  http.end();

  StaticJsonDocument<2048> doc;
  if (deserializeJson(doc, body)) return "";
  String result = "";
  for (JsonObject v : doc["fields"]["items"]["arrayValue"]["values"].as<JsonArray>()) {
    if (result.length()) result += ", ";
    result += v["stringValue"].as<String>();
  }
  return result;
}

bool saveToFirebase(JsonDocument& items_doc) {
  if (!items_doc.containsKey("items") || WiFi.status() != WL_CONNECTED) return false;

  StaticJsonDocument<4096> doc;
  JsonObject fields = doc.createNestedObject("fields");
  fields["updatedAt"]["stringValue"] = getFormattedTimestamp();
  fields["source"]["stringValue"]    = "ESP32-CAM";

  JsonArray values = fields["items"]["arrayValue"].createNestedArray("values");
  for (JsonObject item : items_doc["items"].as<JsonArray>()) {
    JsonObject entry  = values.createNestedObject();
    JsonObject mf     = entry["mapValue"].createNestedObject("fields");
    mf["name"]["stringValue"]       = item["name"].as<String>();
    mf["quantity"]["stringValue"]   = item["quantity"].as<String>();
    mf["confidence"]["stringValue"] = item["confidence"].as<String>();
  }

  String payload;
  serializeJson(doc, payload);
  String url = "https://firestore.googleapis.com/v1/projects/" + String(FIREBASE_PROJECT_ID) +
               "/databases/(default)/documents/fridges/" + String(FRIDGE_ID) +
               "/inventory/current?key=" + String(FIREBASE_API_KEY);
  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  int code = http.PATCH(payload);
  http.end();
  Serial.printf("[FIREBASE] save %s (%d items)\n", (code==200||code==201)?"OK":"FAILED",
                (int)items_doc["items"].as<JsonArray>().size());
  return (code == 200 || code == 201);
}

bool saveScanHistory(JsonDocument& items_doc) {
  if (!items_doc.containsKey("items")) return false;

  DynamicJsonDocument doc(6144);
  JsonObject fields = doc.createNestedObject("fields");
  fields["timestamp"]["stringValue"] = getFormattedTimestamp();
  fields["weekId"]["stringValue"]    = getWeekId();
  fields["monthId"]["stringValue"]   = getMonthId();
  fields["source"]["stringValue"]    = "ESP32-CAM";

  JsonArray values = fields["items"]["arrayValue"].createNestedArray("values");
  for (JsonObject item : items_doc["items"].as<JsonArray>()) {
    JsonObject entry = values.createNestedObject();
    JsonObject mf    = entry["mapValue"].createNestedObject("fields");
    mf["name"]["stringValue"]       = item["name"].as<String>();
    mf["quantity"]["stringValue"]   = item["quantity"].as<String>();
    mf["confidence"]["stringValue"] = item["confidence"].as<String>();
  }

  String payload;
  serializeJson(doc, payload);
  doc.clear();

  String url = String("https://firestore.googleapis.com/v1/projects/") + FIREBASE_PROJECT_ID +
               "/databases/(default)/documents/fridges/" + FRIDGE_ID +
               "/scans/" + getISOTimestamp() + "?key=" + FIREBASE_API_KEY;
  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  int code = http.PATCH(payload);
  http.end();
  return (code == 200 || code == 201);
}

bool fetchInventory() {
  if (WiFi.status() != WL_CONNECTED) return false;
  String url = "https://firestore.googleapis.com/v1/projects/" + String(FIREBASE_PROJECT_ID) +
               "/databases/(default)/documents/fridges/" + String(FRIDGE_ID) +
               "/inventory/current?key=" + String(FIREBASE_API_KEY);
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(10000);
  if (!http.begin(client, url)) return false;
  if (http.GET() != 200) { http.end(); return false; }
  String body = http.getString();
  http.end();

  DynamicJsonDocument doc(8192);
  if (deserializeJson(doc, body)) return false;
  JsonObject fields = doc["fields"];
  if (fields.isNull()) return false;

  g_updated_at = fields["updatedAt"]["stringValue"].as<String>();
  g_item_count = 0;
  for (JsonObject v : fields["items"]["arrayValue"]["values"].as<JsonArray>()) {
    if (g_item_count >= MAX_ITEMS_DISPLAYED) break;
    JsonObject mf = v["mapValue"]["fields"];
    g_items[g_item_count].name       = mf["name"]["stringValue"].as<String>();
    g_items[g_item_count].quantity   = mf["quantity"]["stringValue"].as<String>();
    g_items[g_item_count].confidence = mf["confidence"]["stringValue"].as<String>();
    g_item_count++;
  }
  return true;
}

// ============================================================================
// WIFI & TIME
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
      Serial.println("[WIFI] Wiped, restarting...");
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
  while (now < 24 * 3600 && millis() - t0 < 10000) { delay(250); now = time(nullptr); }
}

// ============================================================================
// SCAN CYCLE
// ============================================================================
void captureAndProcessImage() {
  Serial.println("[SCAN] Starting capture cycle...");
  showStatus("Scanning...", "Taking photo");
  ledStripOn();
  delay(20);

  size_t photo_size = 0;
  uint8_t* photo_data = capturePhoto(&photo_size);
  ledStripOff();

  if (!photo_data) {
    Serial.println("[SCAN] Capture failed");
    showStatus("Scan failed", "No photo");
    delay(2000);
    renderInventory();
    return;
  }

  showStatus("Analyzing...", "Gemini AI");
  String basic_items = fetchBasicItems();
  String response    = sendToGemini(photo_data, photo_size, basic_items);
  free(photo_data);

  if (response.length() == 0) {
    Serial.println("[SCAN] No Gemini response");
    showStatus("Scan failed", "Gemini error");
    delay(2000);
    renderInventory();
    return;
  }

  StaticJsonDocument<2048> detected_items;
  if (!parseGeminiResponse(response, detected_items)) {
    Serial.println("[SCAN] Parse failed");
    showStatus("Scan failed", "Parse error");
    delay(2000);
    renderInventory();
    return;
  }

#if DEBUG_MODE
  if (detected_items.containsKey("description")) {
    Serial.printf("[DEBUG] Image description: %s\n",
                  detected_items["description"].as<const char*>());
  }
#endif

  showStatus("Saving...", "Firestore");
  String ts = getFormattedTimestamp();
  saveToFirebase(detected_items);
  saveScanHistory(detected_items);

  Serial.printf("[SCAN] Done — %d items\n", g_item_count);
  updateDisplayFromItems(detected_items, ts);
}

// ============================================================================
// SERIAL COMMANDS
// ============================================================================
void printHelp() {
  Serial.println("\n========================================");
  Serial.println("SmartFridge Combined — Commands");
  Serial.println("SCAN      — Capture, analyze, update display");
  Serial.println("LED ON    — LED strip on (test)");
  Serial.println("LED OFF   — LED strip off (test)");
  Serial.println("STATUS    — System status");
  Serial.println("WIFIRESET — Wipe WiFi credentials");
  Serial.println("HELP      — This menu");
  Serial.println("========================================\n");
}

void processSerialCommand(String cmd) {
  cmd.trim();
  cmd.toUpperCase();
  if      (cmd == "SCAN")     captureAndProcessImage();
  else if (cmd == "LED ON")   ledStripOn();
  else if (cmd == "LED OFF")  ledStripOff();
  else if (cmd == "STATUS") {
    Serial.printf("[STATUS] WiFi: %s  IP: %s  Items: %d  Heap: %u\n",
                  WiFi.status() == WL_CONNECTED ? "OK" : "DISCONNECTED",
                  WiFi.localIP().toString().c_str(),
                  g_item_count,
                  (unsigned)ESP.getFreeHeap());
  }
  else if (cmd == "WIFIRESET") {
    WiFiManager wm;
    wm.resetSettings();
    Serial.println("[WIFI] Wiped, restarting...");
    delay(1000);
    ESP.restart();
  }
  else if (cmd == "HELP")     printHelp();
  else if (cmd.length() > 0)  Serial.printf("[CMD] Unknown: %s\n", cmd.c_str());
}

// ============================================================================
// SETUP / LOOP
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[BOOT] SmartFridge Combined starting");
  Serial.printf("[TFT] MOSI=%d SCLK=%d CS=%d DC=%d RST=%d Bus=%s\n",
                TFT_MOSI, TFT_SCLK, TFT_CS, TFT_DC, TFT_RST,
#ifdef USE_HSPI_PORT
                "HSPI"
#else
                "VSPI"
#endif
                );

  backlightOn();
  tft.init();
  tft.setRotation(DISPLAY_ROTATION);
  tft.fillScreen(TFT_BLACK);
  TJpgDec.setSwapBytes(true);
  showStatus("Smart Fridge", "Booting...");

  checkResetButton();
  initWiFi();
  configureTime();

  showStatus("Init camera...", "");
  initFlash();
  initCamera();
  initLEDStrip();

  webServer.on("/latest.jpg", HTTP_GET, handleLatestJpeg);
  webServer.begin();
  Serial.printf("[WEB] http://%s/latest.jpg\n", WiFi.localIP().toString().c_str());

  showStatus("Loading...", "Fetching inventory");
  if (fetchInventory()) {
    g_last_signature = buildSignature();
    renderInventory();
  } else {
    showStatus("Ready", "Waiting for first scan");
  }

  g_last_poll_ms = millis();

  printHelp();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    showStatus("WiFi lost", "Restarting...");
    delay(2000);
    ESP.restart();
  }

  webServer.handleClient();

  unsigned long now = millis();

  // Background poll — catches Firestore edits made outside this device
  if (now - g_last_poll_ms >= INVENTORY_POLL_INTERVAL_MS) {
    g_last_poll_ms = now;
    if (fetchInventory()) {
      String sig = buildSignature();
      if (sig != g_last_signature) {
        g_last_signature = sig;
        renderInventory();
      }
    }
  }

  if (Serial.available())
    processSerialCommand(Serial.readStringUntil('\n'));

  delay(50);
}
