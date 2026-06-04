/*
 * SmartFridge — Hall Effect Door Sensor (isolated test)
 *
 * US #10 — Door detection. Standalone sketch: NO camera, NO WiFi, NO display.
 * Purpose: confirm wiring + polarity on GPIO 16 and tune the module's
 * threshold pot before integrating into SmartFridge_ESP32_CAM.
 *
 * Wiring (4-pin digital hall module — use the DO/digital output):
 *   Hall VCC → 5V
 *   Hall GND → GND
 *   Hall DO  → GPIO 13   (matches the CAM board's DOOR_SENSOR_PIN)
 *   Hall AO  → not connected
 *
 * Logic (per CLAUDE.md): LOW = magnet near = door CLOSED,
 *                        HIGH = door OPEN.
 * If your module reads inverted, flip DOOR_CLOSED_LEVEL below.
 *
 * Runs on any ESP32 (devkit or CAM). Move a magnet toward/away from the
 * sensor and watch the serial log (115200 baud). Adjust the on-board pot
 * until the toggle is crisp at the gap that represents "door shut".
 */

#define DOOR_SENSOR_PIN     13     // free pin on CAM board, INPUT_PULLUP
#define DOOR_CLOSED_LEVEL   LOW    // LOW = magnet near = door closed
#define DOOR_DEBOUNCE_MS    50     // require a reading to be stable this long

static int  doorState     = HIGH;  // last confirmed (debounced) state
static int  lastReading   = HIGH;  // last raw reading
static unsigned long lastChangeMs = 0;

static void reportDoor(int level) {
  bool closed = (level == DOOR_CLOSED_LEVEL);
  Serial.printf("[DOOR] %s   (raw GPIO%d = %s)\n",
                closed ? "CLOSED (magnet near)" : "OPEN",
                DOOR_SENSOR_PIN,
                level == HIGH ? "HIGH" : "LOW");
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[BOOT] Hall door sensor test");
  Serial.printf("[BOOT] OUT on GPIO%d, INPUT_PULLUP, closed level = %s\n",
                DOOR_SENSOR_PIN, DOOR_CLOSED_LEVEL == LOW ? "LOW" : "HIGH");

  pinMode(DOOR_SENSOR_PIN, INPUT_PULLUP);
  doorState   = digitalRead(DOOR_SENSOR_PIN);
  lastReading = doorState;
  reportDoor(doorState);
}

void loop() {
  int reading = digitalRead(DOOR_SENSOR_PIN);

  // Reset the debounce timer whenever the raw reading changes.
  if (reading != lastReading) {
    lastReading   = reading;
    lastChangeMs  = millis();
  }

  // Accept the new state only once it has been stable long enough.
  if ((millis() - lastChangeMs) >= DOOR_DEBOUNCE_MS && reading != doorState) {
    doorState = reading;
    reportDoor(doorState);
  }

  delay(5);
}
