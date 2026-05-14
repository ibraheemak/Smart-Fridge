// ============================================================================
// SmartFridge ESP32-CAM Combined — user parameters
// ============================================================================
//
// Single-board: ESP32-CAM runs both camera scanning and ILI9488 TFT display.
//
// IMPORTANT — TFT_eSPI User_Setup.h must have:
//   #define ILI9488_DRIVER
//   #define TFT_MOSI  13
//   #define TFT_SCLK  14
//   #define TFT_CS    15
//   #define TFT_DC    12   // GPIO 4 = camera flash; GPIO 12 floats LOW at boot (safe)
//   #define TFT_RST   -1   // RST tied to 3.3V
//   #define SPI_FREQUENCY  20000000
//   // USE_HSPI_PORT — leave commented (VSPI with GPIO matrix remapping)
//   // TFT_MISO — leave undefined (not connected)
//
// Wiring (ESP32-CAM ↔ ILI9488):
//   TFT SDI/MOSI  ->  GPIO 13   SPI data
//   TFT SCK       ->  GPIO 14   SPI clock
//   TFT CS        ->  GPIO 15   SPI chip select
//   TFT DC/RS     ->  GPIO 12   Data/command (strapping pin but safe — floats LOW at boot)
//   TFT RST       ->  3V3       Tie high — do NOT use GPIO 12 for RST (only for DC)
//   TFT VCC+LED   ->  3V3
//   TFT GND       ->  GND
//
//   LED strip     ->  GPIO 2    WS2811 data (moved from GPIO 14)
//   Camera flash  ->  GPIO 4    PWM via LEDC (unchanged)
//
// ============================================================================

#pragma once

// ----------------------------------------------------------------------------
// FRIDGE IDENTITY
// ----------------------------------------------------------------------------
#define FRIDGE_ID  "fridge1"

// ----------------------------------------------------------------------------
// DISPLAY
// ----------------------------------------------------------------------------
#define DISPLAY_ROTATION    1        // 1/3 = landscape (480x320)
#define TFT_BL_PIN         -1        // Backlight tied to 3V3 — no software control
#define TFT_BL_ON          HIGH

// ----------------------------------------------------------------------------
// LIST RENDERING
// ----------------------------------------------------------------------------
#define HEADER_HEIGHT_PX    40
#define FOOTER_HEIGHT_PX    24
#define ROW_HEIGHT_PX       56
#define SIDE_PADDING_PX     12
#define MAX_ITEMS_DISPLAYED 32

// ----------------------------------------------------------------------------
// FIREBASE STORAGE — item icons
// ----------------------------------------------------------------------------
#define FIREBASE_STORAGE_BUCKET  "smartfridge-79217.firebasestorage.app"
#define ICON_PATH_PREFIX         "icons/"
#define ICON_EXTENSION           ".jpg"
#define ICON_SIZE_PX             48

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
#define LED_DATA_PIN       2         // WS2811 LED strip (moved from GPIO 14)
#define LED_NUM_LEDS       4
#define LED_BRIGHTNESS     200

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
// DISPLAY TIMING
// ----------------------------------------------------------------------------
#define INVENTORY_POLL_INTERVAL_MS 60000  // background Firestore poll every 60 s

// ----------------------------------------------------------------------------
// WIFI
// ----------------------------------------------------------------------------
#define WIFI_AP_NAME          "SmartFridge_Setup"
#define WIFI_PORTAL_TIMEOUT_S 180
#define RESET_BUTTON_PIN        0    // BOOT button — hold at power-on to wipe creds
#define RESET_HOLD_MS        3000

// ----------------------------------------------------------------------------
// GEMINI
// ----------------------------------------------------------------------------
#define GEMINI_REQUEST_TIMEOUT_MS 30000
#define GEMINI_MAX_RETRIES            2

// ----------------------------------------------------------------------------
// TIMEZONE
// ----------------------------------------------------------------------------
#define TIMEZONE "IST-2IDT,M3.4.4/26,M10.5.0"
