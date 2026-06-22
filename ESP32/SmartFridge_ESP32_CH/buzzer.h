#pragma once

#include "parameters.h"

// ============================================================================
// Active buzzer — door-open alert (US #10)
//
// Wiring:
//   Buzzer +  ->  GPIO BUZZER_PIN (14)
//   Buzzer -  ->  GND
//
// Pulsed mode: beeps ON/OFF repeatedly for BUZZER_DURATION_MS total.
// Non-blocking — call updateBuzzer() every loop().
// ============================================================================

static unsigned long buzzerEndAt    = 0;   // when the whole alert ends (0 = idle)
static unsigned long buzzerNextAt   = 0;   // when to toggle next
static bool          buzzerPinHigh  = false;

void initBuzzer() {
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  Serial.printf("[BUZZER] init — GPIO%d\n", BUZZER_PIN);
}

// Start pulsed buzzing for durationMs total. Safe to call while already buzzing.
void buzzFor(unsigned long durationMs) {
  buzzerEndAt   = millis() + durationMs;
  buzzerNextAt  = millis();   // start first beep immediately
  buzzerPinHigh = false;      // updateBuzzer() will flip to HIGH on first call
  Serial.printf("[BUZZER] pulsed alert for %lu ms\n", durationMs);
}

// Call every loop() — handles ON/OFF toggling and final cutoff.
void updateBuzzer() {
  if (buzzerEndAt == 0) return;   // idle

  unsigned long now = millis();

  // Time's up — ensure pin is LOW and reset state.
  if (now >= buzzerEndAt) {
    digitalWrite(BUZZER_PIN, LOW);
    buzzerEndAt   = 0;
    buzzerPinHigh = false;
    Serial.println("[BUZZER] alert done");
    return;
  }

  // Toggle at the right moment.
  if (now >= buzzerNextAt) {
    buzzerPinHigh = !buzzerPinHigh;
    digitalWrite(BUZZER_PIN, buzzerPinHigh ? HIGH : LOW);
    buzzerNextAt = now + (buzzerPinHigh ? BUZZER_BEEP_ON_MS : BUZZER_BEEP_OFF_MS);
  }
}
