#pragma once

#include "parameters.h"

// ----------------------------------------------------------------------------
// Hall effect door sensor (US #10)
//
// 3-pin digital module on DOOR_SENSOR_PIN. Magnet near = door closed.
// Polled (not interrupt-driven) because a close triggers captureAndProcess(),
// which does blocking network I/O — never safe inside an ISR.
// ----------------------------------------------------------------------------

static int           doorState     = HIGH;   // last confirmed (debounced) state
static int           doorReading   = HIGH;   // last raw reading
static unsigned long doorChangeMs  = 0;      // when the raw reading last changed

void initDoorSensor() {
  pinMode(DOOR_SENSOR_PIN, INPUT_PULLUP);
  doorState   = digitalRead(DOOR_SENSOR_PIN);
  doorReading = doorState;
  Serial.printf("[DOOR] init — GPIO%d, %s at boot\n",
                DOOR_SENSOR_PIN,
                doorState == DOOR_CLOSED_LEVEL ? "CLOSED" : "OPEN");
}

// True for the reading: is the door currently closed (debounced)?
bool doorIsClosed() { return doorState == DOOR_CLOSED_LEVEL; }

// Debounced edge detector. Returns true exactly once per OPEN -> CLOSED
// transition. Non-blocking; call every loop().
bool doorJustClosed() {
  int reading = digitalRead(DOOR_SENSOR_PIN);

  // Reset the debounce timer whenever the raw reading changes.
  if (reading != doorReading) {
    doorReading  = reading;
    doorChangeMs = millis();
  }

  // Accept the new state only once it has been stable long enough.
  if ((millis() - doorChangeMs) >= DOOR_DEBOUNCE_MS && reading != doorState) {
    int prev  = doorState;
    doorState = reading;
    // Fire only on the OPEN -> CLOSED edge.
    if (prev != DOOR_CLOSED_LEVEL && doorState == DOOR_CLOSED_LEVEL)
      return true;
  }
  return false;
}
