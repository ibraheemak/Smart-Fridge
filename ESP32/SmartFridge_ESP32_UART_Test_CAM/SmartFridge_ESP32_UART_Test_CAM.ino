/*
 * UART + Scan Trigger Test — ESP32-CAM (receiver)
 *
 * Listens on Serial2 (GPIO 13) for "SCAN_TRIGGER\n" from the CH board.
 * Prints a confirmation — the real sketch will call captureAndProcess() here.
 *
 * Wiring (2 wires only):
 *   CAM GPIO 13 (RX) ◄────────── CH GPIO 17 (TX2)
 *   CAM GND          ─────────── CH GND
 *
 * GPIO 13 — freed up because the hall sensor moved to the CH board.
 */

#define UART_RX_PIN  13
#define UART_BAUD    9600

void setup() {
  Serial.begin(115200);
  Serial2.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, -1);
  Serial.println("[CAM] Ready — waiting for SCAN_TRIGGER");
}

void loop() {
  while (Serial2.available()) {
    String msg = Serial2.readStringUntil('\n');
    msg.trim();
    if (msg.length() == 0) continue;

    Serial.print("[CAM] << received: ");
    Serial.println(msg);

    if (msg == "SCAN_TRIGGER") {
      Serial.println("[CAM] Scan triggered! (would call captureAndProcess() here)");
    }
  }
}
