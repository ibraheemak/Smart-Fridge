## Smart Fridge — Project by: Ibraheem Akaree, Ameed Houssen, Mohamad Asi

**Detect. Track. Alert.**

Smart Fridge is an IoT-enabled refrigerator assistant that helps users reduce food waste and manage food inventory more efficiently. Whenever the door closes, two ESP32-CAM boards photograph the upper and lower shelves, a vision AI (Google Gemini) identifies the products, and the resulting inventory is stored in Firebase. A touchscreen ESP32 mounted on the fridge door and a companion Flutter mobile app show the live inventory, per-item expiry dates, temperature and humidity, and alerts.

## Our project in detail

The system is split across two ESP32 boards that talk to each other over ESP-NOW, a Firebase backend (Firestore + Realtime Database), and a Flutter phone app:

* **Automatic inventory detection** — on door-close, each ESP32-CAM captures a photo of its shelf ("roof") and Google Gemini identifies the items; results from all shelves are merged into a single live inventory.
* **Live display** — an ILI9488 touchscreen shows the current inventory, quantities and per-unit expiry dates, refreshing instantly through a Realtime Database stream (no polling).
* **Mobile app (Flutter)** — view and edit inventory, manage settings and users, and receive push notifications.
* **Expiry tracking & alerts** — warns about expired and soon-to-expire units.
* **Barcode scanning (GM65)** — manually add a product by barcode, looked up via Open Food Facts.
* **Recipe suggestions** — Gemini suggests recipes from the current inventory.
* **Temperature & humidity monitoring** — DHT11 sensor with out-of-range alerts.
* **Door monitoring** — Hall-effect sensor with a "door left open" buzzer alert.
* **Live camera view** — request a live snapshot from any shelf camera.
* **Offline resilience** — photos and barcodes are buffered when WiFi drops and replayed on reconnect.

### Walkthrough of the main user stories

*Detection & inventory (US #1–#4).* Closing the door trips the Hall sensor on the controller board, which broadcasts a scan trigger over ESP-NOW. Each ESP32-CAM photographs its shelf, Gemini identifies the products with a quantity and confidence, and every camera writes its own result document. The controller merges all shelves into one live inventory, shown on the fridge touchscreen and in the app in real time.

*Expiry (US #5–#6).* When a new unit appears, the touchscreen prompts for its expiry date; dates can also be set from the app. Each unit is tracked individually, classified as expired / expiring soon / fresh, and surfaced as on-device notifications.

*Environment & door (US #8–#10).* The DHT11 on roof 1 reports temperature and humidity every 60 s, pushed to the controller over ESP-NOW for instant display. Readings outside the configured safe range raise an alert, and a door left open past the configured delay triggers the buzzer and a phone notification.

*Live view (US #11).* Either shelf camera can be asked for a fresh snapshot on demand, from the fridge screen or the app.

*Manual entry (US #15).* A GM65 barcode scanner resolves a product name via Open Food Facts and adds it to the inventory — the fallback when the camera misses something.

*Recipes, analytics, shopping (US #7, #12, #13).* Gemini suggests recipes from the current contents; monthly purchase counters drive usage analytics; and a shared shopping list is synced through Firebase.

*Users & settings (US #14).* Each account links to a fridge by its device ID. The first person to connect becomes the manager; everyone after joins as a member once the manager approves them. All alert thresholds and toggles are shared two-way in real time between the fridge screen and the app.

> **Current limitations (as of submission):**
> * Item **removal** is not yet fully automatic — items are removed manually from the app or the screen. Camera-driven auto-removal is outstanding reconciliation work (see [docs/INVENTORY_RECONCILIATION_PLAN.md](docs/INVENTORY_RECONCILIATION_PLAN.md)).
> * **Recipe suggestions (US #7)** are implemented but currently return an error, because the Gemini text model this project's API key uses was retired.

## Folder description

* **ESP32** — firmware for both boards: `SmartFridge_ESP32_CH` (display/controller), `SmartFridge_ESP32_CAM` (camera + sensors), plus `SmartFridge_ESP32_GetMac` and UART test utilities.
* **flutter_app** — Dart/Flutter source for the mobile app.
* **Documentation** — wiring diagrams and basic operating instructions.
* **Unit Tests** — test sketches for individual hardware components (input/output devices).
* **Parameters** — description of the configurable parameters (see also `parameters.h` inside each ESP32 sketch).
* **Assets** — project poster.
* **docs** — design and planning notes, including the inventory reconciliation plan and the algorithm evaluation.

## Hardware used

| Component | Qty | Notes |
|---|---|---|
| ESP32 devkit (CH9102) | 1 | Controller + touchscreen display board |
| ESP32-CAM (AI-Thinker, OV2640) | 2 | One per shelf ("roof") |
| ILI9488 3.5" TFT 480×320 + XPT2046 touch | 1 | Main display |
| GM65 barcode scanner | 1 | UART, 5 V |
| DHT11 temperature/humidity sensor | 1 | On roof1 (GPIO 14) |
| Hall-effect door sensor (3-pin) + magnet | 1 | Door open/close detection |
| Passive buzzer | 1 | Door-open alert (LEDC PWM, GPIO 14) |
| WS2811 addressable LED strip | 1 | 4 LEDs, brightness 200, on roof1 |
| 5 V USB power supply | 1 | Powers the GM65 and WS2811 (both 5 V) |
| 3D-printed enclosure | 1 | Custom fridge-mounted case |

## ESP32 SDK version used in this project

* **esp32 by Espressif** (Arduino core) — version **3.3.8**

## Arduino/ESP32 libraries used in this project

**Display / controller board (SmartFridge_ESP32_CH):**
* WiFiManager (tzapu) — 2.0.17
* ArduinoJson (bblanchon) — 7.4.3
* TFT_eSPI (Bodmer) — 2.5.43
* TJpg_Decoder (Bodmer) — 1.1.0

**Camera board (SmartFridge_ESP32_CAM):**
* WiFiManager (tzapu) — 2.0.17
* ArduinoJson (bblanchon) — 7.4.3
* FastLED (Daniel Garcia) — 3.10.3
* esp32-camera — bundled with the ESP32 Arduino board package

*(The DHT11 is read with hand-written bit-banged timing, so no external DHT library is required.)*

## Connection diagram

**Controller / display board (ESP32 DevKit):**

![Display board wiring](Documentation/wiring_display.png)

**Camera board (ESP32-CAM + WS2811 LED strip + DHT11):**

![Camera board wiring](Documentation/wiring_camera.png)

Complete pin assignments are also documented at the top of `ESP32/SmartFridge_ESP32_CH/parameters.h` and `ESP32/SmartFridge_ESP32_CAM/parameters.h`.

## Algorithm — performance evaluation

The project's core algorithm is the Gemini vision food-recognition step (photo → item names, quantity, and confidence). It was evaluated on **148 real scan documents** (2026-05-03 → 2026-07-21): 192 total detections, a mean of 1.30 items per scan, **14.2 % of scans returning no items**, and **37 % of detections self-reported as high-confidence**. The dominant failure mode is naming instability (69 distinct names produced for ~10–15 real products), which motivated the canonical-name reconciliation design.

Full methodology, per-algorithm results, reproduction steps, and the outstanding controlled-accuracy experiment are documented in **[docs/ALGORITHM_EVALUATION.md](docs/ALGORITHM_EVALUATION.md)**.

## Project Poster

![Smart Fridge Poster](Assets/SmartFridge_Poster.png)

---

This project is part of ICST - The Interdisciplinary Center for Smart Technologies, Taub Faculty of Computer Science, Technion
https://icst.cs.technion.ac.il/
