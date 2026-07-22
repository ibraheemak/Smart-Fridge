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
// Unique per physical fridge. The app links users to a fridge by this exact
// ID (shown on the CH board's TFT); the first user to connect it becomes its
// manager. For a multi-fridge deployment give each unit a distinct serial
// (e.g. "SF-0001") and flash the SAME value to this fridge's CH + all its CAM
// boards. Must match FRIDGE_ID in SmartFridge_ESP32_CH.
#define FRIDGE_ID  "fridge1"

// ----------------------------------------------------------------------------
// CAMERA / ROOF IDENTITY (multi-camera support, N cameras now N=2)
// ----------------------------------------------------------------------------
// Each ESP32-CAM board sits above its own fridge shelf ("roof") and runs this
// exact same sketch — only this constant changes per physical board before
// flashing. Only roof 1's board is wired with the WS2811 LED strip and DHT11
// temperature sensor (see LED/TEMP sections below); every other roof board
// leaves those pins unconnected and the corresponding init/read code compiled
// out (#if CAMERA_ROOF == 1 in the .ino).
//
// Each board writes its own scan results to its own Firestore document —
// fridges/{FRIDGE_ID}/inventory/roof{CAMERA_ROOF} — instead of a single
// shared "current" doc, so two boards scanning concurrently can never race
// each other. The CH display board merges all roof docs into the combined
// inventory/current doc that it (and the app) actually displays/edits — see
// inventory_merge.h on the CH board.
#define CAMERA_ROOF  2   // 1, 2, 3... — set per physical board
#define STRINGIFY2(x) #x
#define STRINGIFY(x)  STRINGIFY2(x)
#define INVENTORY_DOC_ID  "roof" STRINGIFY(CAMERA_ROOF)

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
#define FLASH_GPIO_NUM     4         // Camera flash, onboard every CAM board (PWM via LEDC)
#define LED_DATA_PIN       2         // WS2811 LED strip — wired on CAMERA_ROOF == 1 only
#define LED_NUM_LEDS       4
#define LED_BRIGHTNESS     200

// ----------------------------------------------------------------------------
// ESP-NOW LINK TO CH BOARD (US #10) — receives SCAN_TRIGGER on door close
// ----------------------------------------------------------------------------
// The hall door sensor lives on the CH board; it sends SCAN_TRIGGER to this
// board over ESP-NOW (no wiring — see espnow_link.h). Must match
// ESPNOW_CHANNEL on the CH board.
#define ESPNOW_CHANNEL          1

// Reverse direction (CAMERA_ROOF == 1 only): pushes DHT11 readings straight
// to the CH board's display over ESP-NOW instead of CH polling Firestore
// for them (see espnowSendTemperature() in espnow_link.h). Read this board's
// own MAC by flashing ESP32/SmartFridge_ESP32_GetMac/ onto the CH devkit
// once (prints WiFi.macAddress() over serial), then reflash it back to
// SmartFridge_ESP32_CH afterward.
static uint8_t CH_MAC_ADDR[6] = { 0xA8, 0x42, 0xE3, 0x46, 0xE0, 0x64 };

// ----------------------------------------------------------------------------
// DHT11 TEMPERATURE / HUMIDITY SENSOR (US #8, #9) — wired on CAMERA_ROOF == 1 only
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

// Live View (US #11) — deliberately lower-res/lower-quality than the AI-scan
// capture above: the frame has to fit through ESP-NOW in a reasonable number
// of small chunks (see espnow_link.h), so a smaller JPEG matters more here
// than image sharpness.
#define LIVEVIEW_FRAMESIZE       FRAMESIZE_QVGA   // 320x240
#define LIVEVIEW_JPEG_QUALITY          35         // higher number = more compression

// ----------------------------------------------------------------------------
// WIFI
// ----------------------------------------------------------------------------
// Prefix only — the roof number is appended at runtime (wifiPortalApName() in
// wifi_portal.h) so each camera's setup AP is distinguishable: roof 1 hosts
// "SmartFridge_CAM_Setup1", roof 2 "SmartFridge_CAM_Setup2", ...
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
// GPT (OpenAI) — fallback when Gemini fails, see detectItemsFromPhoto() in
// gemini.h
// ----------------------------------------------------------------------------
#define OPENAI_REQUEST_TIMEOUT_MS 30000
#define OPENAI_VISION_MODEL       "gpt-4o-mini"
// Debug: 1 = skip Gemini entirely and always call GPT directly, so the
// fallback path itself can be exercised without needing Gemini to fail.
#define AI_FORCE_GPT                  0

// ----------------------------------------------------------------------------
// TIMEZONE
// ----------------------------------------------------------------------------
#define TIMEZONE "IST-2IDT,M3.4.4/26,M10.5.0"
