/*
 * Smart Fridge ESP32-CAM
 * Captures photos of fridge contents, sends to Gemini for food detection,
 * and saves results to Firebase
 */

#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "time.h"
#include <WebServer.h>
#include "mbedtls/base64.h"
#include "SECRETS.h"
#include "parameters.h"
#include "led_strip.h"

// ============================================================================
// GLOBALS FOR LATEST JPEG & WEB SERVER
// ============================================================================
WebServer webServer(80);
uint8_t* latest_jpeg = nullptr;
size_t latest_jpeg_size = 0;
// ============================================================================
// HTTP HANDLER FOR /latest.jpg
// ============================================================================
void handleLatestJpeg() {
  WiFiClient client = webServer.client();
  if (latest_jpeg && latest_jpeg_size > 0) {
    String header = "HTTP/1.1 200 OK\r\n";
    header += "Content-Type: image/jpeg\r\n";
    header += "Content-Length: " + String(latest_jpeg_size) + "\r\n";
    header += "Cache-Control: no-cache, no-store, must-revalidate\r\n";
    header += "Pragma: no-cache\r\n";
    header += "Expires: 0\r\n";
    header += "Connection: close\r\n";
    header += "\r\n";

    client.print(header);
    client.write(latest_jpeg, latest_jpeg_size);
    client.flush();
  } else {
    webServer.send(404, "text/plain", "No image captured yet.");
  }
}


// ============================================================================
// CAMERA CONFIGURATION FOR AI THINKER ESP32-CAM
// ============================================================================
void initCamera() {
  Serial.println("\n[CAMERA] Initializing camera...");
  
  camera_config_t config = {};
  // Use LEDC_CHANNEL_1 to avoid conflicts with CHANNEL_0
  config.ledc_channel = (ledc_channel_t)CAMERA_LEDC_CHANNEL;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = CAMERA_XCLK_FREQ;
  config.pixel_format = PIXFORMAT_JPEG;
  config.jpeg_quality = CAMERA_JPEG_QUALITY;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

  // Frame size and buffer location depend on PSRAM availability
  if (psramFound()) {
    config.frame_size   = FRAMESIZE_SVGA;   // 800x600 — enough detail, conserves heap
    config.fb_count     = 2;
    config.fb_location  = CAMERA_FB_IN_PSRAM;
    Serial.println("[CAMERA] PSRAM found — using PSRAM frame buffer");
  } else {
    config.frame_size   = FRAMESIZE_CIF;    // 400x296 — fits in DRAM
    config.fb_count     = 1;
    config.fb_location  = CAMERA_FB_IN_DRAM;
    Serial.println("[CAMERA] No PSRAM — using DRAM frame buffer (reduced resolution)");
  }
  
  // Initialize camera with retry loop — LEDC timer conflicts clear after 1–2 attempts
  esp_err_t err = ESP_FAIL;
  for (int attempt = 1; attempt <= 5; attempt++) {
    err = esp_camera_init(&config);
    if (err == ESP_OK) break;
    Serial.printf("[CAMERA] Init attempt %d/5 failed: 0x%x, retrying...\n", attempt, err);
    esp_camera_deinit();
    delay(500 * attempt);
  }
  if (err != ESP_OK) {
    Serial.printf("[CAMERA] Camera failed after 5 attempts: 0x%x\n", err);
    delay(1000);
    ESP.restart();
  }

  Serial.println("[CAMERA] Camera initialized successfully");
  
  // Adjust camera settings
  #if DEBUG_MODE
  Serial.println("[CAMERA] Applying camera settings...");
  #endif
  sensor_t * s = esp_camera_sensor_get();
  s->set_brightness(s, 0);
  s->set_contrast(s, 0);
  s->set_saturation(s, 0);
  s->set_special_effect(s, 0);
  s->set_whitebal(s, 1);
  s->set_awb_gain(s, 1);
  s->set_wb_mode(s, 0);
  s->set_exposure_ctrl(s, 1);
  s->set_aec_value(s, 400);     // higher base exposure for flash photography
  s->set_aec2(s, 1);            // double-pass AEC — smoother convergence
  s->set_agc_gain(s, 2);        // small initial gain so dark areas lift slightly
  s->set_gainceiling(s, GAINCEILING_8X); // allow sensor to boost gain in shadows
  s->set_dcw(s, 1);
  s->set_bpc(s, 0);
  s->set_wpc(s, 1);
  s->set_raw_gma(s, 1);
  s->set_lenc(s, 1);
  s->set_hmirror(s, 0);
  s->set_vflip(s, 1);
  s->set_colorbar(s, 0);
  
  #if DEBUG_MODE
  Serial.println("[CAMERA] Camera settings applied. Warming up camera...");
  // Take a warmup shot to initialize camera sensor
  for (int i = 0; i < 3; i++) {
    camera_fb_t * fb = esp_camera_fb_get();
    if (fb) {
      esp_camera_fb_return(fb);
    }
    delay(CAMERA_WARMUP_DELAY_MS);
  }
  Serial.println("[CAMERA] Warmup complete, ready for capture");
  #endif
}

// ============================================================================
// CAMERA FLASH CONTROL
// ============================================================================
void initFlash() {
  ledcAttach(FLASH_GPIO_NUM, 50000, 8);  // 50 kHz — above camera line-scan, prevents banding
  ledcWrite(FLASH_GPIO_NUM, 0);
}

void turnOnFlash() {
  Serial.printf("[FLASH] Turning on flash (duty %d/255)\n", FLASH_PWM_DUTY);
  ledcWrite(FLASH_GPIO_NUM, FLASH_PWM_DUTY);
}

void turnOffFlash() {
  Serial.println("[FLASH] Turning off flash");
  ledcWrite(FLASH_GPIO_NUM, 0);
}

// ============================================================================
// CAPTURE PHOTO
// ============================================================================
uint8_t* capturePhoto(size_t* photo_size, bool update_latest = true) {
  Serial.println("[CAPTURE] Taking photo...");

  turnOnFlash();
  delay(100); // let flash LED reach full brightness

  // Flush frames captured in darkness before the flash was on.
  // With GRAB_WHEN_EMPTY the buffers are pre-filled; returning them lets the
  // camera immediately refill with flash-lit frames.
  for (int i = 0; i < 3; i++) {
    camera_fb_t* stale = esp_camera_fb_get();
    if (stale) esp_camera_fb_return(stale);
    delay(30);
  }

  // Wait for AEC to converge on the now-lit scene
  delay(FLASH_DURATION_MS);

  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("[CAPTURE] Failed to get frame buffer");
    turnOffFlash();
    return NULL;
  }

  turnOffFlash();

  // Copy frame buffer to heap memory (frame buffer will be freed)
  uint8_t* photo_data = (uint8_t*) malloc(fb->len);
  if (!photo_data) {
    Serial.println("[CAPTURE] Failed to allocate memory for photo");
    esp_camera_fb_return(fb);
    return NULL;
  }

  memcpy(photo_data, fb->buf, fb->len);
  *photo_size = fb->len;

  // Store latest JPEG in memory for web endpoint
  if (update_latest) {
    if (latest_jpeg) {
      free(latest_jpeg);
      latest_jpeg = nullptr;
      latest_jpeg_size = 0;
    }
    latest_jpeg = (uint8_t*) malloc(fb->len);
    if (latest_jpeg) {
      memcpy(latest_jpeg, fb->buf, fb->len);
      latest_jpeg_size = fb->len;
    }
  }

  esp_camera_fb_return(fb);

  Serial.printf("[CAPTURE] Photo captured successfully, size: %d bytes\n", *photo_size);
  return photo_data;
}


// ============================================================================
// SEND TO GEMINI API
// ============================================================================
String sendToGemini(uint8_t* photo_data, size_t photo_size, const String& basic_items) {
  Serial.println("[GEMINI] Preparing to send image to Gemini...");

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[GEMINI] WiFi not connected!");
    return "";
  }

  // Build prompt — inject canonical product list when available
  String prompt =
    "Analyze this refrigerator image. Identify all visible food items and estimate quantities.";

  if (basic_items.length() > 0) {
    prompt += " When an item matches one of these canonical names use it exactly: [" + basic_items + "]."
              " For any item NOT in that list, describe it in lowercase.";
  }

  #if DEBUG_MODE
  prompt += " Also add a top-level \\\"description\\\" field with a brief note on image quality and lighting.";
  #endif

  prompt += " Return valid JSON only: {\\\"items\\\": [{\\\"name\\\": \\\"item name\\\","
            " \\\"quantity\\\": \\\"amount\\\", \\\"confidence\\\": \\\"high/medium/low\\\"}]}";
  prompt += " For example: {\\\"items\\\": [{\\\"name\\\": \\\"milk\\\", \\\"quantity\\\": \\\"1\\\","
            " \\\"confidence\\\": \\\"high\\\"}]}";

  // Build JSON prefix and suffix as fixed strings
  const char* mid  = "\"},{\"inlineData\":{\"mimeType\":\"image/jpeg\",\"data\":\"";
  const char* tail = "\"}}]}]}";
  // prefix = {"contents":[{"parts":[{"text":"<prompt>
  String prefix_str = String("{\"contents\":[{\"parts\":[{\"text\":\"") + prompt + mid;
  const char* prefix = prefix_str.c_str();
  size_t prefix_len  = prefix_str.length();
  size_t tail_len    = strlen(tail);

  // Calculate base64 output size without encoding yet
  size_t enc_len = 0;
  mbedtls_base64_encode(nullptr, 0, &enc_len, photo_data, photo_size);

  size_t total_size = prefix_len + enc_len + tail_len;
  Serial.printf("[GEMINI] Free heap before request: %u\n", (unsigned)ESP.getFreeHeap());
  Serial.printf("[GEMINI] Allocating body buffer: %u bytes\n", (unsigned)total_size);

  // Single allocation for the entire body — avoids double-buffering the 82 KB base64 string
  char* body = (char*)malloc(total_size + 1);
  if (!body) {
    Serial.println("[GEMINI] Failed to allocate request body buffer");
    return "";
  }

  // Assemble body in-place: prefix | base64(image) | tail
  memcpy(body, prefix, prefix_len);
  size_t actual_enc = 0;
  if (mbedtls_base64_encode((unsigned char*)(body + prefix_len), enc_len,
                             &actual_enc, photo_data, photo_size) != 0) {
    Serial.println("[GEMINI] Base64 encoding failed");
    free(body);
    return "";
  }
  memcpy(body + prefix_len + actual_enc, tail, tail_len);
  size_t body_len = prefix_len + actual_enc + tail_len;
  body[body_len] = '\0';

  Serial.printf("[GEMINI] Final JSON payload size: %u bytes\n", (unsigned)body_len);

  // Build URL with API key
  String url = String(GEMINI_API_ENDPOINT) + "?key=" + GEMINI_API_KEY;

  Serial.printf("[GEMINI] Sending request to: %s\n", GEMINI_API_ENDPOINT);

  String response = "";
  for (int attempt = 1; attempt <= GEMINI_MAX_RETRIES + 1; attempt++) {
    HTTPClient http;
    http.begin(url);
    http.setTimeout(GEMINI_REQUEST_TIMEOUT_MS);
    http.addHeader("Content-Type", "application/json");

    int httpResponseCode = http.POST((uint8_t*)body, body_len);

    if (httpResponseCode == 200) {
      response = http.getString();
      http.end();
      Serial.println("[GEMINI] Response received successfully");
      free(body);
      return response;
    } else if (httpResponseCode == 503 && attempt <= GEMINI_MAX_RETRIES) {
      Serial.printf("[GEMINI] 503 overloaded, retrying in 5s (attempt %d/%d)...\n",
                    attempt, GEMINI_MAX_RETRIES + 1);
      http.end();
      delay(5000);
    } else if (httpResponseCode > 0) {
      Serial.printf("[GEMINI] Response code: %d\n", httpResponseCode);
      Serial.printf("[GEMINI] Error response: %s\n", http.getString().c_str());
      http.end();
      break;
    } else {
      Serial.printf("[GEMINI] HTTP Error: %s\n", http.errorToString(httpResponseCode).c_str());
      http.end();
      break;
    }
  }

  free(body);
  return "";
}

// ============================================================================
// PARSE GEMINI RESPONSE
// ============================================================================
bool parseGeminiResponse(String response, JsonDocument& detected_items) {
  Serial.println("[PARSE] Parsing Gemini response...");
  
  // Find the JSON part in the response
  StaticJsonDocument<8192> full_response;
  DeserializationError error = deserializeJson(full_response, response);
  
  if (error) {
    Serial.printf("[PARSE] JSON deserialization error: %s\n", error.c_str());
    return false;
  }
  
  // Extract the text content from Gemini response
  const char* text_content = full_response["candidates"][0]["content"]["parts"][0]["text"];
  
  if (!text_content) {
    Serial.println("[PARSE] No text content found in response");
    return false;
  }
  
  Serial.printf("[PARSE] Extracted text: %s\n", text_content);
  
  // Gemini may wrap the JSON in markdown fences. Extract the first JSON object.
  String json_text = String(text_content);
  json_text.trim();

  if (json_text.startsWith("```")) {
    int first_newline = json_text.indexOf('\n');
    if (first_newline >= 0) {
      json_text = json_text.substring(first_newline + 1);
    }
    int fence_pos = json_text.lastIndexOf("```");
    if (fence_pos >= 0) {
      json_text = json_text.substring(0, fence_pos);
    }
    json_text.trim();
  }

  int first_brace = json_text.indexOf('{');
  int last_brace = json_text.lastIndexOf('}');
  if (first_brace >= 0 && last_brace > first_brace) {
    json_text = json_text.substring(first_brace, last_brace + 1);
  }

  Serial.printf("[PARSE] Sanitized JSON: %s\n", json_text.c_str());

  // Parse the JSON from sanitized text content
  DeserializationError parse_error = deserializeJson(detected_items, json_text);
  
  if (parse_error) {
    Serial.printf("[PARSE] Failed to parse food items JSON: %s\n", parse_error.c_str());
    return false;
  }
  
  Serial.println("[PARSE] Successfully parsed food items");
  return true;
}

// ============================================================================
// FIREBASE INTEGRATION
// ============================================================================

// Human-readable local timestamp: "2024-05-15 14:30:00"
String getFormattedTimestamp() {
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  char buf[20];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", t);
  return String(buf);
}

// Document-safe ISO timestamp (no colons): "2024-05-15T14-30-00"
String getISOTimestamp() {
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  char buf[20];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H-%M-%S", t);
  return String(buf);
}

// Week-of-month ID: "2024-05-W1" … "2024-05-W5"
// Days 1-7 = W1, 8-14 = W2, 15-21 = W3, 22-28 = W4, 29-31 = W5
String getWeekId() {
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  int weekOfMonth = (t->tm_mday - 1) / 7 + 1;
  char buf[14];
  snprintf(buf, sizeof(buf), "%04d-%02d-W%d",
           t->tm_year + 1900, t->tm_mon + 1, weekOfMonth);
  return String(buf);
}

// Month ID: "2024-05"
String getMonthId() {
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  char buf[8];
  strftime(buf, sizeof(buf), "%Y-%m", t);
  return String(buf);
}

// Fetch the canonical product list from Firestore basic-items/basic-items
// Returns a comma-separated string like "milk, soft drink, soft drink - sugar free"
String fetchBasicItems() {
  String url =
    "https://firestore.googleapis.com/v1/projects/" +
    String(FIREBASE_PROJECT_ID) +
    "/databases/(default)/documents/basic-items/basic-items?key=" +
    String(FIREBASE_API_KEY);

  HTTPClient http;
  http.begin(url);
  int code = http.GET();

  if (code != 200) {
    Serial.printf("[FIREBASE] fetchBasicItems failed: %d\n", code);
    http.end();
    return "";
  }

  String body = http.getString();
  http.end();

  StaticJsonDocument<2048> doc;
  if (deserializeJson(doc, body)) {
    Serial.println("[FIREBASE] fetchBasicItems: JSON parse error");
    return "";
  }

  String result = "";
  JsonArray values = doc["fields"]["items"]["arrayValue"]["values"];
  for (JsonObject v : values) {
    if (result.length() > 0) result += ", ";
    result += v["stringValue"].as<String>();
  }

  Serial.printf("[FIREBASE] Basic items: %s\n", result.c_str());
  return result;
}

// Save all detected items as a single document at fridges/{id}/inventory/current
bool saveToFirebase(JsonDocument& items_doc) {
  Serial.println("[FIREBASE] Saving to Firestore...");

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[FIREBASE] WiFi not connected!");
    return false;
  }

  if (!items_doc.containsKey("items")) {
    Serial.println("[FIREBASE] No items found");
    return false;
  }

  // Build Firestore document: { fields: { updatedAt, source, items: { arrayValue: { values: [...] } } } }
  StaticJsonDocument<4096> doc;
  JsonObject fields = doc.createNestedObject("fields");
  fields["updatedAt"]["stringValue"] = getFormattedTimestamp();
  fields["source"]["stringValue"]    = "ESP32-CAM";

  JsonArray values = fields["items"]["arrayValue"].createNestedArray("values");
  for (JsonObject item : items_doc["items"].as<JsonArray>()) {
    JsonObject entry   = values.createNestedObject();
    JsonObject mfields = entry["mapValue"].createNestedObject("fields");
    mfields["name"]["stringValue"]       = item["name"].as<String>();
    mfields["quantity"]["stringValue"]   = item["quantity"].as<String>();
    mfields["confidence"]["stringValue"] = item["confidence"].as<String>();
  }

  String payload;
  serializeJson(doc, payload);

  String url =
    "https://firestore.googleapis.com/v1/projects/" +
    String(FIREBASE_PROJECT_ID) +
    "/databases/(default)/documents/fridges/" +
    String(FRIDGE_ID) +
    "/inventory/current?key=" +
    String(FIREBASE_API_KEY);

  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  int code = http.PATCH(payload);
  if (code == 200 || code == 201) {
    Serial.printf("[FIREBASE] Saved %d items to inventory/current\n",
                  items_doc["items"].as<JsonArray>().size());
  } else {
    Serial.printf("[FIREBASE] Save failed, code: %d\n", code);
    Serial.println(http.getString());
  }
  http.end();

  return (code == 200 || code == 201);
}

// ============================================================================
// SCAN HISTORY — one new Firestore document per scan
// ============================================================================

// Saves this scan to fridges/{id}/scans/{timestamp} (new document every scan).
bool saveScanHistory(JsonDocument& items_doc) {
  if (!items_doc.containsKey("items")) return false;

  String ts     = getFormattedTimestamp();
  String scanId = getISOTimestamp();
  String weekId = getWeekId();
  String monthId = getMonthId();

  DynamicJsonDocument doc(6144);
  JsonObject fields = doc.createNestedObject("fields");
  fields["timestamp"]["stringValue"] = ts;
  fields["weekId"]["stringValue"]    = weekId;
  fields["monthId"]["stringValue"]   = monthId;
  fields["source"]["stringValue"]    = "ESP32-CAM";

  JsonArray values = fields["items"]["arrayValue"].createNestedArray("values");
  for (JsonObject item : items_doc["items"].as<JsonArray>()) {
    JsonObject entry   = values.createNestedObject();
    JsonObject mfields = entry["mapValue"].createNestedObject("fields");
    mfields["name"]["stringValue"]       = item["name"].as<String>();
    mfields["quantity"]["stringValue"]   = item["quantity"].as<String>();
    mfields["confidence"]["stringValue"] = item["confidence"].as<String>();
  }

  String payload;
  serializeJson(doc, payload);
  doc.clear();

  String url = String("https://firestore.googleapis.com/v1/projects/") +
               FIREBASE_PROJECT_ID +
               "/databases/(default)/documents/fridges/" +
               FRIDGE_ID + "/scans/" + scanId +
               "?key=" + FIREBASE_API_KEY;

  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  int code = http.PATCH(payload);
  http.end();

  if (code == 200 || code == 201) {
    Serial.printf("[HISTORY] scans/%s saved\n", scanId.c_str());
    return true;
  }
  Serial.printf("[HISTORY] scans save failed: %d\n", code);
  return false;
}

// ============================================================================
// WIFI CONNECTION
// ============================================================================
void connectToWiFi() {
  Serial.println("\n[WIFI] Starting WiFi connection...");
  Serial.printf("[WIFI] Connecting to: %s\n", WIFI_SSID);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < WIFI_MAX_ATTEMPTS) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WIFI] Connected successfully!");
    Serial.printf("[WIFI] IP address: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("[WIFI] RSSI: %d dBm\n", WiFi.RSSI());
  } else {
    Serial.println("\n[WIFI] Failed to connect!");
    delay(2000);
    ESP.restart();
  }
}

// ============================================================================
// CONFIGURE TIME WITH NTP
// ============================================================================
void configureTime() {
  Serial.println("[TIME] Configuring time with NTP server...");
  configTzTime(TIMEZONE, "pool.ntp.org", "time.nist.gov");
  
  time_t now = time(nullptr);
  while (now < 24 * 3600) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  
  Serial.println();
  struct tm timeinfo = *localtime(&now);
  Serial.printf("[TIME] Current time: %s\n", asctime(&timeinfo));
}

// ============================================================================
// MAIN CAPTURE AND PROCESS FUNCTION
// ============================================================================
void captureAndProcessImage() {
  Serial.println("\n[MAIN] Starting capture and process cycle...");
  ledStripOn();
  // WS2811 LEDs latch their color after show() — RMT goes idle ~1ms later.
  // A short pause ensures RMT is silent before the camera reads its data bus.
  delay(20);

  size_t photo_size = 0;
  uint8_t* photo_data = capturePhoto(&photo_size, true);

  if (!photo_data) {
    Serial.println("[MAIN] Failed to capture photo");
    ledStripOff();
    return;
  }
  
  // Fetch canonical product list from Firestore, then send image to Gemini
  String basic_items = fetchBasicItems();
  String gemini_response = sendToGemini(photo_data, photo_size, basic_items);
  free(photo_data);
  
  if (gemini_response.length() == 0) {
    Serial.println("[MAIN] Failed to get Gemini response");
    return;
  }
  
  // Parse response
  StaticJsonDocument<2048> detected_items;
  if (!parseGeminiResponse(gemini_response, detected_items)) {
    Serial.println("[MAIN] Failed to parse Gemini response");
    return;
  }
  
  // Print detected items
  Serial.println("[MAIN] Detected food items:");
  
  #if DEBUG_MODE
  if (detected_items.containsKey("description")) {
    Serial.printf("[DEBUG] Image description: %s\n", detected_items["description"].as<const char*>());
  }
  #endif
  
  if (detected_items.containsKey("items")) {
    JsonArray items = detected_items["items"];
    for (JsonObject item : items) {
      Serial.printf("  - %s: %s (confidence: %s)\n",
        item["name"].as<const char*>(),
        item["quantity"].as<const char*>(),
        item["confidence"].as<const char*>()
      );
    }
  }

  // Print URL for latest image
  Serial.printf("[MAIN] Latest image available at: http://%s/latest.jpg\n", WiFi.localIP().toString().c_str());

  // Save current inventory snapshot
  saveToFirebase(detected_items);

  // Save per-scan history + append to weekly/monthly accumulation docs
  if (saveScanHistory(detected_items)) {
    Serial.println("[MAIN] Cycle completed successfully!");
  } else {
    Serial.println("[MAIN] Cycle done (some history saves may have failed — check serial)");
  }

  ledStripOff();
}

// ============================================================================
// PRINT HELP MENU
// ============================================================================
void printHelp() {
  Serial.println("\n========================================");
  Serial.println("Smart Fridge ESP32-CAM - Available Commands");
  Serial.println("========================================");
  Serial.println("SCAN   - Capture image, send to Gemini, save to Firestore");
  Serial.println("LED ON  - Turn LED strip on (test)");
  Serial.println("LED OFF - Turn LED strip off (test)");
  Serial.println("HELP   - Show this help menu");
  Serial.println("STATUS - Show system status");
  Serial.println("========================================\n");
}

// ============================================================================
// PROCESS SERIAL COMMANDS
// ============================================================================
void processSerialCommand(String command) {
  command.trim();
  command.toUpperCase();
  
  if (command == "SCAN") {
    Serial.println("[COMMAND] SCAN received - Starting capture cycle...");
    captureAndProcessImage();
  }
  else if (command == "LED ON") {
    Serial.println("[COMMAND] LED ON");
    ledStripOn();
  }
  else if (command == "LED OFF") {
    Serial.println("[COMMAND] LED OFF");
    ledStripOff();
  }
  else if (command == "HELP") {
    printHelp();
  }
  else if (command == "STATUS") {
    Serial.println("\n[STATUS] System Status:");
    Serial.printf("[STATUS] WiFi Status: %s\n", WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("[STATUS] IP Address: %s\n", WiFi.localIP().toString().c_str());
      Serial.printf("[STATUS] Signal Strength: %d dBm\n", WiFi.RSSI());
    }
    Serial.println();
  }
  else if (command.length() > 0) {
    Serial.printf("[COMMAND] Unknown command: %s\n", command.c_str());
    Serial.println("[COMMAND] Type 'HELP' for available commands\n");
  }
}

// ============================================================================
// SETUP
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("\n\n========================================");
  Serial.println("Smart Fridge ESP32-CAM Starting Up");
  Serial.println("========================================\n");
  
  // Connect to WiFi
  connectToWiFi();
  
  // Configure time
  configureTime();
  
  // Initialize flash PWM before camera so the pin is configured
  initFlash();

  // Initialize camera
  initCamera();

  // Initialize LED strip
  initLEDStrip();
  
  Serial.println("\n[SETUP] All systems initialized. Ready for commands.");
  printHelp();

  // Start web server for debug endpoint
  webServer.on("/latest.jpg", HTTP_GET, handleLatestJpeg);
  webServer.begin();
  Serial.println("[WEB] Debug web server started on port 80");
}

// ============================================================================
// LOOP
// ============================================================================
void loop() {
  // Check WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[LOOP] WiFi disconnected. Attempting to reconnect...");
    connectToWiFi();
  }

  // Handle web server
  webServer.handleClient();

  // Read serial input
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    processSerialCommand(command);
  }

  // Small delay to prevent overwhelming the serial buffer
  delay(100);
}
