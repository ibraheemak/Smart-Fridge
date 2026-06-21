#pragma once

#include "parameters.h"

// ============================================================================
// Hall effect door sensor (US #10) — moved here from the CAM board so the
// CAM's UART RX pin (GPIO 13) could be freed for the SCAN_TRIGGER link.
//
// Wiring:
//   Hall VCC -> 3.3V
//   Hall GND -> GND
//   Hall DO  -> GPIO 25
//
// Polled (not interrupt-driven) — debounce needs millis() timing, which is
// simpler to reason about outside an ISR.
// ============================================================================

static int           doorState     = HIGH;   // last confirmed (debounced) state
static int           doorReading   = HIGH;   // last raw reading
static unsigned long doorChangeMs  = 0;      // when the raw reading last changed

void initDoorSensor() {
  pinMode(HALL_PIN, INPUT_PULLUP);
  doorState   = digitalRead(HALL_PIN);
  doorReading = doorState;
  Serial.printf("[DOOR] init — GPIO%d, %s at boot\n",
                HALL_PIN,
                doorState == DOOR_CLOSED_LEVEL ? "CLOSED" : "OPEN");
}

bool doorIsClosed() { return doorState == DOOR_CLOSED_LEVEL; }

// Debounced edge detector. Returns true exactly once per OPEN -> CLOSED
// transition. Non-blocking; call every loop().
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
