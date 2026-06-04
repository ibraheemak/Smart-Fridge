# Smart Fridge — Group #8 | Technion ICST

## Two-board architecture (IMPORTANT)

The system runs on **two separate ESP32 boards** that talk only through Firestore:

| Board | Sketch folder | Responsibility |
|---|---|---|
| **ESP32-CAM** (AI Thinker) | `SmartFridge_ESP32_CAM` | Camera → Gemini AI → Firestore, WS2811 LED strip, **hall door sensor** |
| **ESP32 devkit** (CH9102 USB) | `SmartFridge_ESP32_CH` | ILI9488 TFT + XPT2046 touch, polls Firestore and renders inventory |

> The TFT display used to live on the CAM board (old `SmartFridge_ESP32_Combined`,
> now removed). It now runs on the separate CH devkit, which **freed GPIO 12/13/14
> on the CAM board** for sensors.

## What's already working (DO NOT break)

| Component | Board / Sketch | Status |
|---|---|---|
| ESP32-CAM camera | CAM `SmartFridge_ESP32_CAM` | ✅ Working |
| WS2811 LED strip (GPIO 2) | CAM `SmartFridge_ESP32_CAM` | ✅ Working |
| WiFi + Firebase Firestore | both boards | ✅ Working |
| Gemini AI food recognition | CAM `SmartFridge_ESP32_CAM` | ✅ Working |
| Hall door sensor → auto scan | CAM `SmartFridge_ESP32_CAM` | ✅ Working (US #10) |
| ILI9488 TFT display (480×320) | CH `SmartFridge_ESP32_CH` | ✅ Working |
| Inventory display on screen | CH `SmartFridge_ESP32_CH` | ✅ Working |

## GPIO map — ESP32-CAM (AI Thinker) — TAKEN pins

> The TFT is on the separate CH board now, so GPIO 12/13/14 are free on the CAM.

```
GPIO  0  — Camera XCLK + BOOT button (WiFi reset)
GPIO  2  — WS2811 LED strip data ← OCCUPIED
GPIO  4  — Camera flash PWM       ← OCCUPIED
GPIO  5  — Camera Y2
GPIO 12  — free ✅  ⚠️ strapping (flash-voltage select) — do NOT use for an input that can be HIGH at boot
GPIO 13  — Hall door sensor OUT   ← OCCUPIED (US #10 — door-close auto scan)
GPIO 14  — free ✅
GPIO 15  — free ✅  ⚠️ strapping — silences boot log if LOW at boot (cosmetic)
GPIO 16  — free ✅ (verify per board: PSRAM CS on some CAM revisions)
GPIO 18  — Camera Y3
GPIO 19  — Camera Y4
GPIO 21  — Camera Y5
GPIO 22  — Camera PCLK
GPIO 23  — Camera HREF
GPIO 25  — Camera VSYNC
GPIO 26  — Camera SDA
GPIO 27  — Camera SCL
GPIO 32  — Camera PWDN
GPIO 34  — Camera Y8 (input only)
GPIO 35  — Camera Y9 (input only)
GPIO 36  — Camera Y6 (input only)
GPIO 39  — Camera Y7 (input only)
```

**Free GPIO pins on the CAM board for new sensors:**
- GPIO 14 — free ✅ (safest spare — no strapping)
- GPIO 12 — free but ⚠️ strapping (flash-voltage) — only for outputs / inputs guaranteed LOW at boot
- GPIO 15 — free but ⚠️ strapping (boot-log) — safe, just hides boot serial when LOW
- GPIO 16 — free ✅ (verify per board: PSRAM CS on some revisions)
- GPIO 1 (TX) / GPIO 3 (RX) — serial, use carefully
- GPIO 33 — free ✅ (was used in older sketch for TFT DC — now free)

> ⚠️ GPIOs 34, 35, 36, 39 are INPUT ONLY — no pull-up, no output.

### GPIO map — ESP32 devkit / CH board (display) — TAKEN pins
```
GPIO  4  — TFT RST
GPIO  5  — Touch CS (XPT2046)
GPIO 18  — TFT SCK + Touch CLK (shared)
GPIO 19  — Touch DO (MISO)
GPIO 23  — TFT MOSI + Touch DIN (shared)
GPIO 27  — TFT DC/RS
```
> The CH board is a standard ESP32 devkit, so it has many more free pins than the
> CAM. Sensors that conflict with the camera bus can live here instead.

---

## Git branch strategy — one branch per sensor/feature

**The golden rule: never work directly on `main`.**

```
main
├── feature/dht11-temperature      ← US #8, #9
├── feature/hall-door-sensor       ← US #10
├── feature/hx711-weight           ← US #1, #4
├── feature/ds3231-rtc             ← US #5
├── feature/mp3-audio-alerts       ← US #10 (sound)
├── feature/touch-expiry-input     ← US #5, #15
├── feature/flutter-app            ← US #3, #7, #11, #12, #13, #14
└── feature/recipe-recommendations ← US #7
```

**Workflow for adding a new sensor:**
1. `git checkout main && git pull`
2. `git checkout -b feature/<sensor-name>`
3. Add new `.ino` / `.h` files — do NOT edit `SmartFridge_ESP32_CAM.ino` / `SmartFridge_ESP32_CH.ino` until merging
4. Test the sensor in isolation in its own sketch under `ESP32/SmartFridge_ESP32_<SensorName>/`
5. When stable → open PR to merge into `main` (team reviews)

---

## Project architecture

```
ESP32-CAM board  (SmartFridge_ESP32_CAM)
    ├── Camera → Gemini AI → Firestore (cloud)
    ├── LED strip (illumination during scan)
    ├── Hall door sensor (GPIO 13) → door close auto-triggers a scan
    └── [TODO] more sensors below

ESP32 devkit board  (SmartFridge_ESP32_CH)
    └── TFT display + touch ← polls Firestore inventory/current

Firestore (Firebase)
    ├── fridges/fridge1/inventory/current   ← live inventory
    ├── fridges/fridge1/scans/<timestamp>   ← scan history
    ├── basic-items/basic-items             ← canonical item names
    └── [TODO] fridges/fridge1/sensors/     ← temperature, door, weight

Flutter app (TODO)
    ├── Inventory view (US #3)
    ├── Expiry alerts (US #6)
    ├── Recipe recommendations (US #7)
    ├── Usage analytics graphs (US #12)
    ├── Shopping list (US #13)
    └── User management (US #14)
```

---

## User stories status

| # | Feature | Status | Branch |
|---|---|---|---|
| 1 | Item Detection (weight) | ❌ TODO | `feature/hx711-weight` |
| 2 | Item Recognition (AI) | ✅ Done | main |
| 3 | Inventory View (screen) | ✅ Done | main |
| 3 | Inventory View (app) | ❌ TODO | `feature/flutter-app` |
| 4 | Quantity Tracking | ✅ Done (AI) | main |
| 5 | Expiry Tracking | ❌ TODO | `feature/touch-expiry-input` |
| 6 | Expiry Alerts | ❌ TODO | `feature/flutter-app` |
| 7 | Recipe Recommendations | ❌ TODO | `feature/recipe-recommendations` |
| 8 | Temperature Monitoring | ❌ TODO | `feature/dht11-temperature` |
| 9 | Temperature Alert | ❌ TODO | `feature/dht11-temperature` |
| 10 | Door sensor → auto scan on close | ✅ Done | `feature/hall-door-sensor` |
| 10 | Door Open Alert (notify) | ❌ TODO | `feature/hall-door-sensor` |
| 11 | Live Fridge View | ❌ TODO | `feature/flutter-app` |
| 12 | Usage Analytics | ⚠️ Partial (data saved) | `feature/flutter-app` |
| 13 | Shopping List | ❌ TODO | `feature/flutter-app` |
| 14 | User Management | ❌ TODO | `feature/flutter-app` |
| 15 | Manual Product Scanner | ❌ TODO | `feature/touch-expiry-input` |

---

## Sensor wiring quick reference

### DHT11 — Temperature & Humidity (US #8, #9)
```
DHT11 VCC  → 3.3V
DHT11 GND  → GND
DHT11 DATA → GPIO 33   (free pin)
            + 10kΩ pull-up resistor between DATA and 3.3V
```
Library: `DHT sensor library` by Adafruit

### Hall Effect Sensor — Door Detection (US #10) ✅ DONE
4-pin digital hall module, wired to the **CAM board**:
```
Hall VCC  → 5V
Hall GND  → GND
Hall DO   → GPIO 13   (digital out; free, no strapping issues, INPUT_PULLUP)
Hall AO   → not connected
```
Logic: LOW = magnet present (door closed), HIGH = door open.
- ⚠️ Do NOT use GPIO 12 — it is the flash-voltage strapping pin; HIGH (door open)
  at power-on can stop the CAM from booting.
- Tune the module's pot so DO toggles cleanly at the door gap.
- Implementation: `door.h` (debounced edge detector) + `parameters.h`
  (`DOOR_SENSOR_PIN`, `DOOR_SETTLE_MS`). Door close → `captureAndProcess()`.
- Isolated tester: `ESP32/SmartFridge_ESP32_HallSensor/`.

### HX711 + Load Cell — Weight Detection (US #1, #4)
```
HX711 VCC  → 3.3V
HX711 GND  → GND
HX711 DT   → GPIO 33  (if DHT11 not used) or share careful
HX711 SCK  → GPIO 16  (if Hall not used)
```
> ⚠️ GPIO conflict — coordinate with team which sensors to use on which pins.
Library: `HX711` by bogde

### DS3231 RTC — Real Time Clock (US #5)
```
DS3231 VCC → 3.3V
DS3231 GND → GND
DS3231 SDA → GPIO 26  (shared I2C — same bus as camera!)
DS3231 SCL → GPIO 27  (shared I2C — same bus as camera!)
```
> ⚠️ I2C bus is shared with camera. DS3231 I2C address = 0x68. Test carefully.
Library: `RTClib` by Adafruit

### MP3 Player (OPEN_SMART) — Audio Alerts (US #10)
```
MP3 VCC   → 5V (needs 5V!)
MP3 GND   → GND
MP3 TX    → GPIO 3 (RX)
MP3 RX    → GPIO 1 (TX)   via 1kΩ resistor
```
> ⚠️ Uses Serial0 — disconnect during flash upload.

---

## Secrets file (not in git)

`SECRETS.h` is gitignored. It must contain:
```cpp
#define FIREBASE_PROJECT_ID   "..."
#define FIREBASE_API_KEY      "..."
#define GEMINI_API_KEY        "..."
#define GEMINI_API_ENDPOINT   "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash:generateContent"
```

---

## How to add a new sensor (step by step)

1. Create a new branch: `git checkout -b feature/<sensor>`
2. Create folder: `ESP32/SmartFridge_ESP32_<Sensor>/`
3. Write isolated test sketch — NO camera, NO display, just the sensor
4. Once sensor works alone → add it to the right board sketch on the branch
   (`SmartFridge_ESP32_CAM` for camera-side sensors, `SmartFridge_ESP32_CH` for display-side)
5. Add sensor pin to the matching GPIO map above and mark it OCCUPIED
6. Firestore path for sensor data: `fridges/fridge1/sensors/<sensor_name>`
7. Push branch → team reviews → merge to main

---

## Libraries used

| Library | Purpose |
|---|---|
| `esp_camera.h` | ESP32-CAM camera driver |
| `TFT_eSPI` | ILI9488 display |
| `TJpg_Decoder` | JPEG decode for icons |
| `FastLED` | WS2811 LED strip |
| `ArduinoJson` | JSON parsing |
| `WiFiManager` | WiFi provisioning |
| `HTTPClient` | Firebase + Gemini HTTP calls |
| `mbedtls/base64` | Image encoding for Gemini |
