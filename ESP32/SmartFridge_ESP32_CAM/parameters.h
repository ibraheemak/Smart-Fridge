// ============================================================================
// SmartFridge ESP32-CAM — user parameters
// ============================================================================
//
// This board does camera scanning only. The ILI9488 TFT now runs on the
// separate CH devkit (SmartFridge_ESP32_CH) — there is NO display on this board.
//
// Wiring (ESP32-CAM):
//   Camera        ->  AI Thinker pinout (see CAMERA PINS section below)
//   LED strip     ->  GPIO 2    WS2811 data
//   Camera flash  ->  GPIO 4    PWM via LEDC
//   Hall door DO  ->  GPIO 13   door-close auto scan (see DOOR SENSOR section)
//
// ⚠️ GPIO 12 is the flash-voltage strapping pin — never wire an input there that
//    can be HIGH at boot, or the board may fail to start.
// ============================================================================

#pragma once

// ----------------------------------------------------------------------------
// FRIDGE IDENTITY
// ----------------------------------------------------------------------------
#define FRIDGE_ID  "fridge1"

// ----------------------------------------------------------------------------
// CAMERA PINS (AI Thinker ESP32-CAM)
// ----------------------------------------------------------------------------
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ----------------------------------------------------------------------------
// FLASH & LED STRIP
// ----------------------------------------------------------------------------
#define FLASH_GPIO_NUM     4         // Camera flash (PWM via LEDC)
#define LED_DATA_PIN       2         // WS2811 LED strip
#define LED_NUM_LEDS       4
#define LED_BRIGHTNESS     200

// ----------------------------------------------------------------------------
// ESP-NOW LINK TO CH BOARD (US #10) — receives SCAN_TRIGGER on door close
// ----------------------------------------------------------------------------
// The hall door sensor lives on the CH board; it sends SCAN_TRIGGER to this
// board over ESP-NOW (no wiring — see espnow_link.h). Must match
// ESPNOW_CHANNEL on the CH board.
#define ESPNOW_CHANNEL          1

// ----------------------------------------------------------------------------
// DHT11 TEMPERATURE / HUMIDITY SENSOR (US #8, #9)
// ----------------------------------------------------------------------------
// 3-pin module (onboard pull-up). "S" -> GPIO14, "+" -> 3.3V, "-" -> GND.
#define DHT_PIN                14     // free pin, no strapping issues
#define TEMP_READ_INTERVAL_MS  60000  // read + publish once per minute

// ----------------------------------------------------------------------------
// DEBUG
// ----------------------------------------------------------------------------
#define DEBUG_MODE 1   // 1 = ask Gemini for image quality description + camera warmup

// ----------------------------------------------------------------------------
// CAMERA CAPTURE
// ----------------------------------------------------------------------------
#define CAMERA_JPEG_QUALITY      20
#define CAMERA_XCLK_FREQ    5000000
#define CAMERA_LEDC_CHANNEL       1  // LEDC_CHANNEL_1 avoids conflicts
#define CAMERA_WARMUP_DELAY_MS  500  // warmup frames delay (DEBUG_MODE only)
#define FLASH_DURATION_MS       600
#define FLASH_PWM_DUTY           80  // 0-255; 80 ≈ 31% brightness

// ----------------------------------------------------------------------------
// WIFI
// ----------------------------------------------------------------------------
#define WIFI_AP_NAME          "SmartFridge_CAM_Setup"
#define WIFI_PORTAL_TIMEOUT_S 180  // time to join the AP and fill the captive portal form
#define RESET_BUTTON_PIN        0    // BOOT button — hold at power-on to wipe creds
#define RESET_HOLD_MS        3000
#define WIFI_RECONNECT_INTERVAL_MS 30000  // retry WiFi.begin() this often while offline

// ----------------------------------------------------------------------------
// GEMINI
// ----------------------------------------------------------------------------
#define GEMINI_REQUEST_TIMEOUT_MS 30000
#define GEMINI_MAX_RETRIES            2

// ----------------------------------------------------------------------------
// TIMEZONE
// ----------------------------------------------------------------------------
#define TIMEZONE "IST-2IDT,M3.4.4/26,M10.5.0"
