/*
 * UART + Hall Sensor Test — ESP32-CH (sender)
 *
 * Reads the hall effect door sensor. When the door closes, sends
 * "SCAN_TRIGGER\n" over Serial2 to the CAM board.
 *
 * Wiring — UART (2 wires):
 *   CH GPIO 17 (TX2) ──────────► CAM GPIO 13 (RX)
 *   CH GND           ─────────── CAM GND
 *
 * Wiring — Hall sensor (3 wires):
 *   Hall VCC  → 3.3V
 *   Hall GND  → GND
 *   Hall DO   → GPIO 25
 *
 * Hall logic: LOW = magnet present = door CLOSED
 *             HIGH = no magnet     = door OPEN
 */

#define UART_TX_PIN        17
#define UART_RX_PIN        16   // not connected — declared to avoid Serial2 crash with -1
#define UART_BAUD          9600

#define HALL_PIN           25     // free pin, no conflicts on CH devkit
#define DOOR_CLOSED_LEVEL  LOW
#define DOOR_DEBOUNCE_MS   50
#define DOOR_SETTLE_MS     1500   // wait after close before triggering scan

// -- debounce state --
static int           doorState    = HIGH;
static int           doorReading  = HIGH;
static unsigned long doorChangeMs = 0;

// Returns true exactly once per OPEN→CLOSED transition (debounced).
bool doorJustClosed() {
  int reading = digitalRead(HALL_PIN);

  if (reading != doorReading) {
    doorReading  = reading;
    doorChangeMs = millis();
  }

  if ((millis() - doorChangeMs) >= DOOR_DEBOUNCE_MS && reading != doorState) {
    int prev  = doorState;
    doorState = reading;
    if (prev != DOOR_CLOSED_LEVEL && doorState == DOOR_CLOSED_LEVEL)
      return true;
  }
  return false;
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);

  pinMode(HALL_PIN, INPUT_PULLUP);
  doorState   = digitalRead(HALL_PIN);
  doorReading = doorState;

  Serial.printf("[CH] Ready — door is %s\n",
                doorState == DOOR_CLOSED_LEVEL ? "CLOSED" : "OPEN");
}

void loop() {
  if (doorJustClosed()) {
    Serial.println("[CH] Door closed — waiting settle time...");
    delay(DOOR_SETTLE_MS);
    Serial2.println("SCAN_TRIGGER");
    Serial.println("[CH] >> SCAN_TRIGGER sent");
  }
}
