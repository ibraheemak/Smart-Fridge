# Smart Fridge — Group #8 | Technion ICST

## Two-board architecture (IMPORTANT)

The system runs on **two separate ESP32 boards**. Most data flows through Firestore,
but the door-close scan trigger flows over a direct **UART link** (see below).

| Board | Sketch folder | Responsibility |
|---|---|---|
| **ESP32-CAM** (AI Thinker) | `SmartFridge_ESP32_CAM` | Camera → Gemini AI → Firestore, WS2811 LED strip, receives `SCAN_TRIGGER` over UART |
| **ESP32 devkit** (CH9102 USB) | `SmartFridge_ESP32_CH` | ILI9488 TFT + XPT2046 touch, polls Firestore and renders inventory, **hall door sensor**, sends `SCAN_TRIGGER` over UART |

> The TFT display used to live on the CAM board (old `SmartFridge_ESP32_Combined`,
> now removed). It now runs on the separate CH devkit, which **freed GPIO 12/13/14
> on the CAM board** for sensors.
>
> The hall door sensor used to live on the CAM board too. It has since **moved to
> the CH board** — the CAM's GPIO 13 is now the UART RX pin for the CH→CAM link.
> See `feature/uart-ch-cam`.

## UART link — CH → CAM (door-close scan trigger)

One-way, 2-wire link. CH detects door close (hall sensor) and sends a
`SCAN_TRIGGER\n` command; CAM receives it and calls `captureAndProcess()`.

```
CH  GPIO 17 (TX2) ──────────► CAM GPIO 13 (RX, Serial2)
CH  GND           ─────────── CAM GND
```

Baud: 9600. Implementation: `uart_link.h` on both boards (`uartSendScanTrigger()`
on CH, `uartScanTriggerReceived()` on CAM).

## What's already working (DO NOT break)

| Component | Board / Sketch | Status |
|---|---|---|
| ESP32-CAM camera | CAM `SmartFridge_ESP32_CAM` | ✅ Working |
| WS2811 LED strip (GPIO 2) | CAM `SmartFridge_ESP32_CAM` | ✅ Working |
| WiFi + Firebase Firestore | both boards | ✅ Working |
| Gemini AI food recognition | CAM `SmartFridge_ESP32_CAM` | ✅ Working |
| Hall door sensor → UART → auto scan | CH → CAM (`uart_link.h`) | ✅ Working (US #10) |
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
GPIO 13  — UART RX (Serial2)      ← OCCUPIED (CH→CAM SCAN_TRIGGER link, see uart_link.h)
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
GPIO 17  — UART TX2 (Serial2) ← CH→CAM SCAN_TRIGGER link, see uart_link.h
GPIO 18  — TFT SCK + Touch CLK (shared)
GPIO 19  — Touch DO (MISO)
GPIO 23  — TFT MOSI + Touch DIN (shared)
GPIO 25  — Hall door sensor DO   (US #10 — door-close auto scan, see door.h)
GPIO 27  — TFT DC/RS
GPIO 32  — UART TX2 (Serial2) ← GM65 barcode scanner RX, see gm65.h
GPIO 14  — Buzzer DO (active buzzer, door-open alert — see buzzer.h)
GPIO 33  — UART RX2 (Serial2) ← GM65 barcode scanner TX, see gm65.h
```
> ⚠️ DHT11 was previously planned for GPIO 33 (see Sensor wiring quick
> reference below) — that's now taken by the GM65 scanner. Pick a different
> free pin (e.g. GPIO 14) when wiring up the DHT11.
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
    ├── UART RX (GPIO 13) ← receives SCAN_TRIGGER from CH, auto-triggers a scan
    └── [TODO] more sensors below

ESP32 devkit board  (SmartFridge_ESP32_CH)
    ├── TFT display + touch ← polls Firestore inventory/current
    ├── Hall door sensor (GPIO 25) → door close → sends SCAN_TRIGGER over UART
    └── UART TX (GPIO 17) → CAM GPIO 13

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
| 15 | Manual Product Scanner | ✅ Done | `feature/gm65-barcode-scanner` |

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

### GM65 — Barcode Scanner (US #15) ✅ DONE
UART module, wired to the **CH board**:
```
GM65 VCC  → 5V
GM65 GND  → GND
GM65 TX   → GPIO 33   (ESP32 RX2)
GM65 RX   → GPIO 32   (ESP32 TX2)
```
- Module defaults to **USB** output — scan the "Series Output" config
  barcode from its manual once to switch it to TTL-232/UART (otherwise it
  beeps/decodes but never sends anything over TX). Default baud 9600, 8N1.
- The module doesn't reliably send a CR/LF terminator after each scan, so
  the read loop flushes its buffer on an idle gap (`GM65_IDLE_FLUSH_MS`)
  instead of waiting for one.
- On scan: looks up the barcode via the
  [Open Food Facts API](https://openfoodfacts.github.io/openfoodfacts-server/api/)
  (`world.openfoodfacts.org/api/v2/product/<barcode>.json`), then merges the
  product name into `fridges/{fridge}/inventory/current` — incrementing
  quantity if the item already exists, otherwise appending a new entry.
  Mirrors the CAM board's fetch→rebuild→PATCH pattern so existing expiry
  dates are preserved, and deliberately leaves `g_items` stale so the next
  `fetchInventory()` poll's existing "new unit needs an expiry date" diff
  picks up the scanned item automatically.
- Implementation: `gm65.h` on the CH board. Isolated tester:
  `Unit Tests/SmartFridge_ESP32_GM65/`.

### Hall Effect Sensor — Door Detection (US #10) ✅ DONE
4-pin digital hall module, wired to the **CH board**:
```
Hall VCC  → 3.3V
Hall GND  → GND
Hall DO   → GPIO 25   (digital out; free, no strapping issues, INPUT_PULLUP)
Hall AO   → not connected
```
Logic: LOW = magnet present (door closed), HIGH = door open.
- Tune the module's pot so DO toggles cleanly at the door gap.
- Implementation: `door.h` on the CH board (debounced edge detector) +
  `parameters.h` (`HALL_PIN`, `DOOR_SETTLE_MS`). Door close → waits the
  settle time → `uartSendScanTrigger()` sends `SCAN_TRIGGER` to the CAM over
  UART (see `uart_link.h` on both boards).
- Isolated tester: `ESP32/SmartFridge_ESP32_UART_Test_CH/` +
  `ESP32/SmartFridge_ESP32_UART_Test_CAM/`.

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
