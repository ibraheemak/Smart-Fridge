/*
 * SmartFridge — Buzzer module (isolated test)
 *
 * US #10 — audio alerts (door open / temperature alarm). Standalone
 * sketch: NO camera, NO WiFi, NO display. Purpose: confirm wiring and
 * sound output on GPIO 25 before integrating into a board sketch.
 *
 * Wiring (3-pin KY-006 style passive buzzer module):
 *   "+" / VCC → 3.3V
 *   "-" / GND → GND
 *   "S"       → GPIO 25   (free pin on CH devkit)
 *
 * Runs on any ESP32 (devkit or CAM). Plays a short beep pattern every
 * 2 seconds using tone(); watch the serial log (115200 baud) and listen
 * for the buzzer.
 */

#define BUZZER_PIN  25     // free pin on CH devkit

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[BOOT] Buzzer module test");
  Serial.printf("[BOOT] Signal (S) on GPIO%d\n", BUZZER_PIN);
  pinMode(BUZZER_PIN, OUTPUT);
}

void loop() {
  Serial.println("[BUZZER] Beep beep");
  tone(BUZZER_PIN, 2000, 150);
  delay(250);
  tone(BUZZER_PIN, 2000, 150);
  delay(250);
  noTone(BUZZER_PIN);

  delay(1500);
}
