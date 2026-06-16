/*
 * UART Test — ESP32-CAM (receiver)
 *
 * Listens on Serial2 for "PING\n" from the CH board. One-way only — no reply.
 * This is the first step toward receiving a door-trigger command from the CH.
 *
 * Wiring (2 wires only):
 *   CAM GPIO 13 (RX) ◄────────── CH GPIO 17 (TX2)
 *   CAM GND          ─────────── CH GND
 *
 * GPIO 13 — freed up because the hall sensor is moving to the CH board.
 */

#define UART_RX_PIN  13   // CAM RX ← CH TX (GPIO 17)
#define UART_BAUD    9600

void setup() {
  Serial.begin(115200);
  // TX pin set to -1 — we only receive, no transmit
  Serial2.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, -1);
  Serial.println("[CAM] UART test started — waiting for PING");
}

void loop() {
  while (Serial2.available()) {
    String msg = Serial2.readStringUntil('\n');
    msg.trim();
    if (msg.length() == 0) continue;

    Serial.print("[CAM] << received: ");
    Serial.println(msg);
  }
}
