# Parameters

All hard-coded, compile-time parameters live in a `parameters.h` file inside each ESP32 sketch, each with an inline comment explaining what it does. To change a parameter, edit the value there and re-flash the board.

* `ESP32/SmartFridge_ESP32_CH/parameters.h` — display / controller board
* `ESP32/SmartFridge_ESP32_CAM/parameters.h` — camera board

## Key parameters — controller / display board

| Parameter | Meaning | Default |
|---|---|---|
| `FRIDGE_ID` | Unique ID per physical fridge (must match the CAM board) | `"fridge1"` |
| `NUM_ROOFS` | Number of ESP32-CAM boards (one per shelf) | `2` |
| `DISPLAY_ROTATION` | TFT orientation (1/3 = landscape) | `1` |
| `WIFI_AP_NAME` | Setup hotspot name for first-time WiFi config | `"SmartFridge_Display_Setup"` |
| `HALL_PIN` | Hall-effect door-sensor pin | `25` |
| `DOOR_OPEN_ALERT_MS` | Buzz after the door is left open this long | `30000` (30 s) |
| `BUZZER_PIN` | Passive buzzer pin | `14` |
| `GM65_RX_PIN` / `GM65_TX_PIN` | GM65 barcode-scanner UART pins | `33` / `32` |
| `ESPNOW_CHANNEL` | ESP-NOW radio channel (must match the CAM boards) | `1` |
| `CAM_MAC_ADDRS` | MAC address of each CAM board | per device |
| `MAX_RECIPES` | Maximum recipe suggestions shown | `3` |

## Key parameters — camera board

See `ESP32/SmartFridge_ESP32_CAM/parameters.h` for the camera pin map, `CAMERA_ROOF` (which shelf this board is), the WS2811 LED-strip pin/length, the DHT11 pin, the temperature read interval, and the Gemini / OpenAI endpoints.

## Credentials

API keys, Firebase IDs and other secrets are **not** stored here — they live in a gitignored `SECRETS.h`. See `ESP32/SECRETS_TEMPLATE.h` for the list of fields and how to set them up.
