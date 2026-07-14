#pragma once

#include "esp_camera.h"
#include "parameters.h"

// latest captured frame — served by /latest.jpg web endpoint
uint8_t* latest_jpeg      = nullptr;
size_t   latest_jpeg_size = 0;

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
    config.frame_size  = FRAMESIZE_SVGA;
    config.fb_count    = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
  } else {
    config.frame_size  = FRAMESIZE_CIF;
    config.fb_count    = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  esp_err_t err = ESP_FAIL;
  for (int attempt = 1; attempt <= 5 && err != ESP_OK; attempt++) {
    err = esp_camera_init(&config);
    if (err != ESP_OK) { esp_camera_deinit(); delay(500 * attempt); }
  }
  if (err != ESP_OK) {
    Serial.printf("[CAMERA] Failed: 0x%x — restarting\n", err);
    delay(2000);
    ESP.restart();
  }

  sensor_t* s = esp_camera_sensor_get();
  s->set_brightness(s, 0);   s->set_contrast(s, 0);      s->set_saturation(s, 0);
  s->set_special_effect(s, 0); s->set_whitebal(s, 1);    s->set_awb_gain(s, 1);
  s->set_wb_mode(s, 0);        s->set_exposure_ctrl(s, 1); s->set_aec_value(s, 400);
  s->set_aec2(s, 1);           s->set_agc_gain(s, 2);    s->set_gainceiling(s, GAINCEILING_8X);
  s->set_dcw(s, 1);            s->set_bpc(s, 0);         s->set_wpc(s, 1);
  s->set_raw_gma(s, 1);        s->set_lenc(s, 1);        s->set_hmirror(s, 0);
  s->set_vflip(s, 1);          s->set_colorbar(s, 0);
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

void initFlash()  { ledcAttach(FLASH_GPIO_NUM, 50000, 8); ledcWrite(FLASH_GPIO_NUM, 0); }
void turnOnFlash()  { ledcWrite(FLASH_GPIO_NUM, FLASH_PWM_DUTY); }
void turnOffFlash() { ledcWrite(FLASH_GPIO_NUM, 0); }

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

// Live View snapshot — temporarily switches the sensor to a smaller frame
// size / lower quality (see LIVEVIEW_FRAMESIZE/LIVEVIEW_JPEG_QUALITY in
// parameters.h) so the JPEG fits through ESP-NOW in a reasonable number of
// chunks, then restores the AI-scan settings. Reuses capturePhoto() as-is
// for the actual flash-strobe + stale-frame-discard capture sequence.
uint8_t* capturePhotoForLiveView(size_t* photo_size) {
  sensor_t* s = esp_camera_sensor_get();
  framesize_t origFramesize = s->status.framesize;
  int origQuality = s->status.quality;

  s->set_framesize(s, LIVEVIEW_FRAMESIZE);
  s->set_quality(s, LIVEVIEW_JPEG_QUALITY);
  delay(150);   // let AEC/AWB settle after the framesize/quality change

  uint8_t* data = capturePhoto(photo_size);

  s->set_framesize(s, origFramesize);
  s->set_quality(s, origQuality);
  return data;
}
