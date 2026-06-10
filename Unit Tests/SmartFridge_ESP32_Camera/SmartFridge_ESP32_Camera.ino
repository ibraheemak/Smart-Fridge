/*
 * SmartFridge — ESP32-CAM Camera unit test (isolated)
 *
 * Validates the OV2640 camera + flash on the AI Thinker ESP32-CAM, in
 * isolation from WiFiManager/Firestore/Gemini/LED/door logic.
 *
 * NO Firestore, NO Gemini, NO LED strip, NO door sensor.
 *
 * What it does:
 *   - Connects to WiFi (edit WIFI_SSID / WIFI_PASSWORD below)
 *   - Initializes the camera + flash
 *   - On boot and on serial command "c", captures a JPEG, prints its size,
 *     and flashes the onboard LED briefly
 *   - Serves the most recent capture at http://<ip>/latest.jpg so you can
 *     view it in a browser
 *
 * Serial commands (115200 baud):
 *   c  — capture a new photo
 *   ?  — print status (IP, last capture size)
 */

#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>

#include "camera_pins.h"
#include "SECRETS.h"

// ----------------------------------------------------------------------------
// CAMERA TUNABLES
// ----------------------------------------------------------------------------
#define CAMERA_XCLK_FREQ    5000000
#define CAMERA_JPEG_QUALITY      20
#define CAMERA_LEDC_CHANNEL       1
#define FLASH_DURATION_MS       600
#define FLASH_PWM_DUTY           80   // 0-255

WebServer webServer(80);

uint8_t* latest_jpeg      = nullptr;
size_t   latest_jpeg_size = 0;

// ----------------------------------------------------------------------------
// CAMERA INIT
// ----------------------------------------------------------------------------
void initCamera() {
  Serial.println("[CAMERA] Initializing...");
  camera_config_t config = {};
  config.ledc_channel = (ledc_channel_t)CAMERA_LEDC_CHANNEL;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = CAMERA_XCLK_FREQ;
  config.pixel_format = PIXFORMAT_JPEG;
  config.jpeg_quality = CAMERA_JPEG_QUALITY;
  config.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;

  if (psramFound()) {
    config.frame_size  = FRAMESIZE_SVGA;
    config.fb_count    = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
  } else {
    config.frame_size  = FRAMESIZE_CIF;
    config.fb_count    = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[CAMERA] Init failed: 0x%x — restarting\n", err);
    delay(2000);
    ESP.restart();
  }
  Serial.println("[CAMERA] Ready");
}

void initFlash() {
  ledcAttach(FLASH_GPIO_NUM, 50000, 8);
  ledcWrite(FLASH_GPIO_NUM, 0);
}

void capturePhoto() {
  ledcWrite(FLASH_GPIO_NUM, FLASH_PWM_DUTY);
  delay(FLASH_DURATION_MS);

  camera_fb_t* fb = esp_camera_fb_get();
  ledcWrite(FLASH_GPIO_NUM, 0);

  if (!fb) {
    Serial.println("[CAPTURE] FAILED — esp_camera_fb_get() returned null");
    return;
  }

  if (latest_jpeg) { free(latest_jpeg); latest_jpeg = nullptr; }
  latest_jpeg = (uint8_t*)malloc(fb->len);
  if (latest_jpeg) {
    memcpy(latest_jpeg, fb->buf, fb->len);
    latest_jpeg_size = fb->len;
  }
  Serial.printf("[CAPTURE] OK — %u bytes (%dx%d)\n", fb->len, fb->width, fb->height);

  esp_camera_fb_return(fb);
}

// ----------------------------------------------------------------------------
// WEB SERVER — /latest.jpg
// ----------------------------------------------------------------------------
void handleLatestJpeg() {
  if (!latest_jpeg || latest_jpeg_size == 0) {
    webServer.send(404, "text/plain", "No image captured yet. Send 'c' over serial.");
    return;
  }
  WiFiClient client = webServer.client();
  client.print("HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\nContent-Length: ");
  client.print(latest_jpeg_size);
  client.print("\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n");
  size_t sent = 0;
  while (sent < latest_jpeg_size) {
    size_t chunk = min((size_t)1024, latest_jpeg_size - sent);
    client.write(latest_jpeg + sent, chunk);
    sent += chunk;
  }
  client.flush();
}

// ----------------------------------------------------------------------------
// SETUP / LOOP
// ----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[BOOT] ESP32-CAM camera unit test");

  initFlash();
  initCamera();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[WIFI] Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.printf("\n[WIFI] Connected — IP: %s\n", WiFi.localIP().toString().c_str());

  webServer.on("/latest.jpg", HTTP_GET, handleLatestJpeg);
  webServer.begin();
  Serial.printf("[WEB] http://%s/latest.jpg\n", WiFi.localIP().toString().c_str());

  capturePhoto(); // initial capture so /latest.jpg has something on first load

  Serial.println("[READY] Send 'c' to capture, '?' for status");
}

void loop() {
  webServer.handleClient();

  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 'c') {
      capturePhoto();
    } else if (cmd == '?') {
      Serial.printf("[STATUS] IP=%s last_capture=%u bytes\n",
                     WiFi.localIP().toString().c_str(), (unsigned)latest_jpeg_size);
    }
  }
}
