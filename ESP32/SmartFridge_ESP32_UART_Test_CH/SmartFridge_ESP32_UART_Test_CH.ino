/*
 * UART Test — ESP32-CH (sender)
 *
 * Sends "PING\n" over Serial2 every 2 seconds. One-way only — no reply expected.
 * This is the first step toward CH→CAM door-trigger messaging.
 *
 * Wiring (2 wires only):
 *   CH GPIO 17 (TX2) ──────────► CAM GPIO 14 (RX2)
 *   CH GND           ─────────── CAM GND
 *
 * Both boards must share GND. No level shifter needed — both run at 3.3V.
 */

#define UART_TX_PIN  17   // CH TX → CAM RX (GPIO 14)
#define UART_BAUD    9600

#define PING_INTERVAL_MS  2000

static unsigned long lastPingMs = 0;

void setup() {
  Serial.begin(115200);
  // RX pin set to -1 — we only transmit, no receive
  Serial2.begin(UART_BAUD, SERIAL_8N1, -1, UART_TX_PIN);
  Serial.println("[CH] UART test started — sending PING every 2 s");
}

void loop() {
  unsigned long now = millis();

  if (now - lastPingMs >= PING_INTERVAL_MS) {
    lastPingMs = now;
    Serial2.println("PING");
    Serial.println("[CH] >> PING sent");
  }
}
