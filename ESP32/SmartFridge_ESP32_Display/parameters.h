// ============================================================================
// SmartFridge ESP32 + ILI9488 Display — user parameters
// ============================================================================
//
// This sketch is a *companion* to SmartFridge_ESP32_CAM. The CAM device
// writes the current inventory snapshot to Firestore at:
//
//     fridges/{FRIDGE_ID}/inventory/current
//
// This device polls that document and renders the items on an ILI9488 TFT.
//
// IMPORTANT — TFT_eSPI configuration
// ----------------------------------
// TFT_eSPI is configured via its own User_Setup.h file (inside the library
// folder), NOT via #defines here. After installing TFT_eSPI in the Arduino
// IDE, edit:
//     <Arduino libs>/TFT_eSPI/User_Setup_Select.h
// and uncomment a setup that matches: ILI9488 + ESP32 + your wiring.
//
// Wiring used by this project (ESP32 ↔ ILI9488, VSPI):
//   Display pin     ESP32 GPIO   Function
//   ----------      ----------   --------
//   SDI / MOSI  ->  GPIO 23      MOSI (data to display)
//   SCK         ->  GPIO 18      SCK  (clock)
//   CS          ->  GPIO 15      CS   (chip select)
//   RESET       ->  GPIO  4      RST
//   DC / RS     ->  GPIO  2      DC   (data/command)
//   SDO / MISO  ->  GPIO 19      MISO (optional)
//   VCC + LED   ->  3V3          power (backlight tied to 3V3 — always on)
//   GND         ->  GND
//
// ============================================================================

#pragma once

// ----------------------------------------------------------------------------
// FRIDGE IDENTITY — must match the value used by SmartFridge_ESP32_CAM
// ----------------------------------------------------------------------------
#define FRIDGE_ID  "fridge1"

// ----------------------------------------------------------------------------
// DISPLAY ROTATION
// ----------------------------------------------------------------------------
// 0/2 = portrait (320x480), 1/3 = landscape (480x320)
#define DISPLAY_ROTATION  1

// Backlight tied directly to 3V3 — no software control.
#define TFT_BL_PIN  -1
#define TFT_BL_ON   HIGH

// ----------------------------------------------------------------------------
// REFRESH BEHAVIOUR
// ----------------------------------------------------------------------------
#define INVENTORY_POLL_INTERVAL_MS  15000   // poll Firestore every 15 s
#define WIFI_PORTAL_TIMEOUT_S       180

// ----------------------------------------------------------------------------
// WIFIMANAGER SETUP PORTAL
// ----------------------------------------------------------------------------
#define WIFI_AP_NAME       "SmartFridge_Display_Setup"
#define RESET_BUTTON_PIN   0       // BOOT button — hold at power-on to wipe creds
#define RESET_HOLD_MS      3000

// ----------------------------------------------------------------------------
// TIMEZONE — used to format the "updated at" footer
// ----------------------------------------------------------------------------
#define TIMEZONE "IST-2IDT,M3.4.4/26,M10.5.0"

// ----------------------------------------------------------------------------
// LIST RENDERING
// ----------------------------------------------------------------------------
#define HEADER_HEIGHT_PX   40
#define FOOTER_HEIGHT_PX   24
#define ROW_HEIGHT_PX      56
#define SIDE_PADDING_PX    12
#define MAX_ITEMS_DISPLAYED 32

// ----------------------------------------------------------------------------
// FIREBASE STORAGE — item icons
// ----------------------------------------------------------------------------
// Icons are JPEGs stored at <bucket>/<ICON_PATH_PREFIX><slug><ICON_EXTENSION>,
// where <slug> is the item name lowercased with non-alphanumerics replaced by
// dashes (e.g. "Soft Drink" -> "soft-drink").
//
// Make the icons folder publicly readable in Storage Rules:
//   match /icons/{file=**} { allow read: if true; }
//
// Recommended icon size: 28x28 JPEG (matches ICON_SIZE_PX). Larger images will
// still render but use more bandwidth and heap.
#define FIREBASE_STORAGE_BUCKET  "smartfridge-79217.firebasestorage.app"
#define ICON_PATH_PREFIX         "icons/"
#define ICON_EXTENSION           ".jpg"
#define ICON_SIZE_PX             48
