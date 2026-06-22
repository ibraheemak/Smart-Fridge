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
static unsigned long doorOpenSince = 0;      // millis() when door last opened (0 = closed)

void initDoorSensor() {
  pinMode(HALL_PIN, INPUT_PULLUP);
  doorState   = digitalRead(HALL_PIN);
  doorReading = doorState;
  // If open at boot, start the open timer immediately.
  doorOpenSince = (doorState != DOOR_CLOSED_LEVEL) ? millis() : 0;
  Serial.printf("[DOOR] init — GPIO%d, %s at boot\n",
                HALL_PIN,
                doorState == DOOR_CLOSED_LEVEL ? "CLOSED" : "OPEN");
}

bool doorIsClosed() { return doorState == DOOR_CLOSED_LEVEL; }

// Returns true once per alert cycle: door has been open >= DOOR_OPEN_ALERT_MS.
// Resets the timer after firing so a second alert fires if door stays open longer.
bool doorOpenTooLong() {
  if (doorOpenSince == 0) return false;   // door is closed
  if (millis() - doorOpenSince >= DOOR_OPEN_ALERT_MS) {
    doorOpenSince = millis();             // reset — next alert in another 30 s
    return true;
  }
  return false;
}

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
    if (doorState == DOOR_CLOSED_LEVEL) {
      doorOpenSince = 0;               // door closed — stop open timer
      if (prev != DOOR_CLOSED_LEVEL) return true;
    } else {
      doorOpenSince = millis();        // door opened — start open timer
    }
  }
  return false;
}
