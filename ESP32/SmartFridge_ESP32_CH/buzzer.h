#pragma once

#include "parameters.h"

// ============================================================================
// Passive buzzer (KY-006) — door-open alert (US #10)
//
// Wiring:
//   "+" / VCC  ->  3.3V
//   "-" / GND  ->  GND
//   "S"        ->  GPIO BUZZER_PIN (14)
//
// Uses tone() — passive buzzer needs a frequency signal, not just HIGH/LOW.
// Pattern (mirrors the isolated unit test): beep-beep ... beep-beep ...
// Non-blocking: call updateBuzzer() every loop().
// ============================================================================

// Beep pattern timing (ms) — matches the unit-test sketch
#define BUZZ_FREQ        2000   // Hz
#define BUZZ_BEEP_MS      150   // each beep duration
#define BUZZ_GAP_MS       250   // gap between the two beeps in a pair
#define BUZZ_PAUSE_MS    1500   // silence between pairs

static unsigned long buzzerEndAt  = 0;   // when the whole alert ends (0 = idle)
static unsigned long buzzerNextAt = 0;   // when to fire the next action
static uint8_t       buzzerStep   = 0;   // which step in the beep pattern

// Steps: 0=beep1, 1=gap, 2=beep2, 3=pause, then repeat
void updateBuzzer() {
  if (buzzerEndAt == 0) return;

  unsigned long now = millis();

  if (now >= buzzerEndAt) {
    noTone(BUZZER_PIN);
    buzzerEndAt = 0;
    buzzerStep  = 0;
    Serial.println("[BUZZER] alert done");
    return;
  }

  if (now < buzzerNextAt) return;   // not yet time

  switch (buzzerStep) {
    case 0:                                    // first beep ON
      tone(BUZZER_PIN, BUZZ_FREQ, BUZZ_BEEP_MS);
      buzzerNextAt = now + BUZZ_GAP_MS;
      buzzerStep   = 1;
      break;
    case 1:                                    // gap between beeps
      // tone() already stopped (BUZZ_BEEP_MS < BUZZ_GAP_MS), nothing to do
      buzzerNextAt = now + BUZZ_GAP_MS;
      buzzerStep   = 2;
      break;
    case 2:                                    // second beep ON
      tone(BUZZER_PIN, BUZZ_FREQ, BUZZ_BEEP_MS);
      buzzerNextAt = now + BUZZ_GAP_MS;
      buzzerStep   = 3;
      break;
    case 3:                                    // pause before next pair
      noTone(BUZZER_PIN);
      buzzerNextAt = now + BUZZ_PAUSE_MS;
      buzzerStep   = 0;
      break;
  }
}

void initBuzzer() {
  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN);
  Serial.printf("[BUZZER] init — GPIO%d\n", BUZZER_PIN);
}

// Start the beep-beep alert for durationMs total. Safe to call repeatedly.
void buzzFor(unsigned long durationMs) {
  buzzerEndAt  = millis() + durationMs;
  buzzerNextAt = millis();
  buzzerStep   = 0;
  Serial.printf("[BUZZER] alert start — %lu ms\n", durationMs);
}
