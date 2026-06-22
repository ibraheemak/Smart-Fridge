#pragma once

#include "parameters.h"

// ============================================================================
// Active buzzer — door-open alert (US #10)
//
// Wiring:
//   Buzzer +  ->  GPIO BUZZER_PIN (14)
//   Buzzer -  ->  GND
//
// Active buzzer: pulling the pin HIGH makes it beep.
// If you have a passive buzzer, replace digitalWrite with tone()/noTone().
// ============================================================================

static unsigned long buzzerOffAt = 0;   // millis() when to stop buzzing (0 = idle)

void initBuzzer() {
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  Serial.printf("[BUZZER] init — GPIO%d\n", BUZZER_PIN);
}

// Start a timed buzz. Safe to call while already buzzing (resets the timer).
void buzzFor(unsigned long durationMs) {
  digitalWrite(BUZZER_PIN, HIGH);
  buzzerOffAt = millis() + durationMs;
  Serial.printf("[BUZZER] ON for %lu ms\n", durationMs);
}

// Call every loop() to cut the buzzer off when time elapses.
void updateBuzzer() {
  if (buzzerOffAt && millis() >= buzzerOffAt) {
    digitalWrite(BUZZER_PIN, LOW);
    buzzerOffAt = 0;
    Serial.println("[BUZZER] OFF");
  }
}
