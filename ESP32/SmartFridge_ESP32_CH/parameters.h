// ============================================================================
// SmartFridge Display ESP32 — user parameters
// ============================================================================
//
// Runs on a standard ESP32 devkit (CH9102 USB driver).
// Companion to SmartFridge_ESP32_CAM which handles camera scanning.
// The CAM device writes inventory to Firestore at:
//     fridges/{FRIDGE_ID}/inventory/current
// This device listens for changes via a Realtime Database SSE stream
// (rtdb_stream.h) and renders the latest data on the ILI9488 TFT.
//
// TFT_eSPI is configured via tft_setup.h in this sketch folder.
//
// Wiring (ESP32 devkit ↔ ILI9488 + XPT2046, VSPI):
// ----------------------------------------------------------------------------
//   ILI9488 TFT:
//     SDI/MOSI  ->  GPIO 23
//     SCK       ->  GPIO 18
//     CS        ->  GPIO 26   (was GND — moved so TFT_eSPI can deselect the
//                              TFT while reading the XPT2046 touch chip)
//     DC/RS     ->  GPIO 27
//     RST       ->  GPIO 4    reset pulse on boot
//     VCC+LED   ->  3V3
//     SDO       ->  NC        do NOT connect (would conflict with touch MISO)
//
//   XPT2046 Touch:
//     T_CLK     ->  GPIO 18   (shared)
//     T_DIN     ->  GPIO 23   (shared)
//     T_DO      ->  GPIO 19
//     T_CS      ->  GPIO 5
//     T_IRQ     ->  NC
//
// ============================================================================

#pragma once

// ----------------------------------------------------------------------------
// FRIDGE IDENTITY — must match the value used by SmartFridge_ESP32_CAM
// ----------------------------------------------------------------------------
#define FRIDGE_ID  "fridge1"

// ----------------------------------------------------------------------------
// MULTI-CAMERA ("ROOF") SUPPORT
// ----------------------------------------------------------------------------
// Number of ESP32-CAM boards, one per fridge shelf ("roof") — each runs
// SmartFridge_ESP32_CAM with its own CAMERA_ROOF (1..NUM_ROOFS) and writes
// its scan results to fridges/{FRIDGE_ID}/inventory/roof{N}. This board
// merges all of them into fridges/{FRIDGE_ID}/inventory/current (see
// inventory_merge.h), which is what the display/touch-edit/GM65 code below
// actually reads and writes.
#define NUM_ROOFS  2

// ----------------------------------------------------------------------------
// DISPLAY
// ----------------------------------------------------------------------------
#define DISPLAY_ROTATION  1        // 1/3 = landscape (480x320)
#define TFT_BL_PIN       -1        // backlight tied to 3V3 — no software control
#define TFT_BL_ON        HIGH

// ----------------------------------------------------------------------------
// WIFI / PORTAL
// ----------------------------------------------------------------------------
#define WIFI_AP_NAME          "SmartFridge_Display_Setup"
#define WIFI_PORTAL_TIMEOUT_S      180
#define WIFI_RECONNECT_INTERVAL_MS 30000  // retry WiFi.begin() this often while offline
#define RESET_BUTTON_PIN        0  // BOOT button — hold at power-on to wipe creds
#define RESET_HOLD_MS        3000

// ----------------------------------------------------------------------------
// HALL EFFECT DOOR SENSOR (US #10) — moved here from the CAM board
// ----------------------------------------------------------------------------
// 3-pin digital module. Hall DO -> GPIO 25. Magnet near = door closed.
#define HALL_PIN               25
#define DOOR_CLOSED_LEVEL      LOW    // LOW = magnet near = door closed
#define DOOR_DEBOUNCE_MS       50     // require a stable reading this long
#define DOOR_SETTLE_MS       1500     // wait after close before triggering scan
#define DOOR_OPEN_ALERT_MS  30000     // buzz after door open this long (30 s)

// ----------------------------------------------------------------------------
// BUZZER (US #10 — door-open alert)
// ----------------------------------------------------------------------------
// Passive buzzer driven via LEDC PWM (see buzzer.h). Wiring: buzzer S -> GPIO
// 14, buzzer- -> GND. Volume, pitch, total duration and melody are runtime
// settings (Settings > Buzzer, persisted in NVS) — the value below is only the
// factory default for the total alert duration.
#define BUZZER_PIN             14
#define BUZZER_DURATION_MS  10000     // default total alert duration (10 s)

// ----------------------------------------------------------------------------
// ESP-NOW LINK TO CAM BOARDS — sends SCAN_TRIGGER on door close
// ----------------------------------------------------------------------------
// Sent as a unicast to each CAM board's MAC below (see espnow_link.h for why
// this isn't a broadcast). Must match ESPNOW_CHANNEL on every CAM board.
#define ESPNOW_CHANNEL          1

// One entry per roof (must have exactly NUM_ROOFS entries, order doesn't
// matter). Read each board's MAC by flashing ESP32/SmartFridge_ESP32_GetMac/
// onto it once (prints WiFi.macAddress() over serial), then reflash it back
// to SmartFridge_ESP32_CAM afterward.
static uint8_t CAM_MAC_ADDRS[NUM_ROOFS][6] = {
  { 0x7C, 0x9E, 0xBD, 0x06, 0x2B, 0x7C },  // roof1 MAC
  { 0x3C, 0x61, 0x05, 0x30, 0xBF, 0x00 },  // roof2 MAC
};

// Live View (US #11) — how long to wait for a requested roof's JPEG snapshot
// to fully arrive over ESP-NOW before giving up and showing an error.
#define LIVEVIEW_TIMEOUT_MS  6000

// ----------------------------------------------------------------------------
// GM65 BARCODE SCANNER (US #15 — manual product scanner)
// ----------------------------------------------------------------------------
// 4-pin UART module (VCC/GND/TX/RX). GM65 TX -> ESP32 RX, GM65 RX -> ESP32 TX.
// Module must be set to TTL-232 output (scan the "Series Output" config
// barcode from its manual once — it defaults to USB out of the box).
#define GM65_RX_PIN          33     // ESP32 RX2 <- GM65 TX
#define GM65_TX_PIN          32     // ESP32 TX2 -> GM65 RX
#define GM65_BAUD           9600
#define GM65_IDLE_FLUSH_MS   100    // flush the read buffer after this much silence
#define GM65_SCAN_TIMEOUT_MS 10000   // give up waiting for a read after this long (module's own single-read timeout is 5s, see gm65.h)

// ----------------------------------------------------------------------------
// TIMEZONE
// ----------------------------------------------------------------------------
#define TIMEZONE "IST-2IDT,M3.4.4/26,M10.5.0"

// ----------------------------------------------------------------------------
// LIST RENDERING
// ----------------------------------------------------------------------------
#define HEADER_HEIGHT_PX    40
#define FOOTER_HEIGHT_PX    24
// 64px so 3 rows + both up/down scroll buttons still fit the 248px list
// area (44..292 in the 480x320 landscape panel): 3*64 + 2*28 = 248.
#define ROW_HEIGHT_PX       64
#define ROW_GAP_PX           6   // visual gap between item rows
#define ROW_TAP_DEADZONE_PX  2   // touch dead zone near row borders (keep small — a
                                  // larger value rejects taps across most of the row
                                  // when calibration is slightly skewed)
#define ROW_ARROW_ZONE_PX   60   // width of the tappable "open details" arrow area
                                  // on the right edge of each item row
#define TOP_ROW_HIT_EXTEND_PX 40 // extend row 0's tap zone up into the header to
                                  // compensate for touch y-readings being low near
                                  // the top edge of the panel
#define SIDE_PADDING_PX     12
#define MAX_ITEMS_DISPLAYED  32
#define SCROLL_ARROW_H       28  // height of the tappable up/down scroll-button strips
#define SCROLL_ARROW_BOX_W   56  // width of the rounded button drawn inside each strip
                                  // (rest of the strip is blank but still tappable, for
                                  // a bigger/more forgiving touch target)

// ----------------------------------------------------------------------------
// TOUCH (XPT2046)
// ----------------------------------------------------------------------------
#define TOUCH_DEBOUNCE_MS   300   // min ms between accepted touch events
#define TOUCH_PRESSURE_THRESHOLD  200   // lower = lighter taps register (TFT_eSPI default is 600)

// ----------------------------------------------------------------------------
// RECIPE RECOMMENDATIONS (US #7) — Gemini text prompt over current inventory
// ----------------------------------------------------------------------------
#define GEMINI_RECIPES_TIMEOUT_MS  20000  // shorter than the CAM's image-analysis timeout — text-only prompt
#define MAX_RECIPES                  3
#define RECIPE_ROW_H                 60

// ----------------------------------------------------------------------------
// FIREBASE STORAGE — item icons
// ----------------------------------------------------------------------------
#define FIREBASE_STORAGE_BUCKET  "smartfridge-79217.firebasestorage.app"
#define ICON_PATH_PREFIX         "icons/"
#define ICON_EXTENSION           ".jpg"
#define ICON_SIZE_PX             48
