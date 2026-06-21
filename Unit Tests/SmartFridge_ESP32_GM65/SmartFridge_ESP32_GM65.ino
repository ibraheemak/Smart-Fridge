/*
 * SmartFridge — GM65 Barcode Scanner (isolated UART test)
 *
 * Manual Product Scanner (US #15). Standalone sketch: NO camera, NO WiFi,
 * NO display. Purpose: confirm UART wiring + baud rate and read out raw
 * barcode payloads from the GM65 before integrating into
 * SmartFridge_ESP32_CH.
 *
 * GM65 default mode is continuous auto-scan: it decodes whatever barcode
 * is in front of it and pushes the result out its TX pin terminated with
 * CR LF, with no command needed from the ESP32.
 *
 * Wiring (GM65 UART mode, module's onboard DIP/jumper set to UART):
 *   GM65 VCC  -> 5V
 *   GM65 GND  -> GND
 *   GM65 TX   -> ESP32 GPIO 33 (RX2)   — barcode data into the ESP32
 *   GM65 RX   -> ESP32 GPIO 32 (TX2)   — only needed to send config commands
 *
 * Picked GPIO 32/33 because they're free on the CH devkit (see
 * parameters.h / CLAUDE.md GPIO map) and don't touch the TFT/touch/UART
 * link/hall-sensor pins already in use there.
 *
 * Default GM65 baud is 9600. Open the serial monitor at 115200 and scan
 * a barcode — the decoded string should print on its own line.
 */

#define GM65_RX_PIN   33   // ESP32 RX2 <- GM65 TX
#define GM65_TX_PIN   32   // ESP32 TX2 -> GM65 RX
#define GM65_BAUD     9600

#define GM65_IDLE_FLUSH_MS  100  // flush buffer if no new byte arrives for this long

HardwareSerial GM65(2);

static String lineBuf;
static unsigned long lastByteMs = 0;

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[BOOT] GM65 barcode scanner test");
  Serial.printf("[BOOT] Listening on GPIO%d (RX) / GPIO%d (TX) at %d baud\n",
                GM65_RX_PIN, GM65_TX_PIN, GM65_BAUD);

  GM65.begin(GM65_BAUD, SERIAL_8N1, GM65_RX_PIN, GM65_TX_PIN);
  lineBuf.reserve(64);
}

void loop() {
  while (GM65.available()) {
    char c = GM65.read();
    lastByteMs = millis();

    // Raw dump first — proves bytes are physically arriving regardless of
    // framing, even if they're garbage from a baud/wiring mismatch.
    Serial.printf("[RAW] 0x%02X ('%c')\n", (uint8_t)c, (c >= 32 && c < 127) ? c : '.');

    if (c == '\r' || c == '\n') continue;  // strip terminator if present
    lineBuf += c;
  }

  // Some GM65 configs don't send a CR/LF terminator at all — flush on
  // idle gap instead of waiting for one.
  if (lineBuf.length() > 0 && (millis() - lastByteMs) >= GM65_IDLE_FLUSH_MS) {
    Serial.printf("[GM65] Barcode: %s\n", lineBuf.c_str());
    lineBuf = "";
  }
}
