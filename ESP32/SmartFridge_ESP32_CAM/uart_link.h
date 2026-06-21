#pragma once

#include "parameters.h"

// ============================================================================
// UART link to the CH board — one-way, CH sends commands, CAM doesn't reply.
// Replaces the on-board hall sensor (US #10), which now lives on the CH
// board; the CAM receives a SCAN_TRIGGER command instead of polling a pin.
//
// Wiring (2 wires):
//   CAM GPIO 13 (RX) <------- CH GPIO 17 (TX2)
//   CAM GND          <------- CH GND
// ============================================================================

void initUartLink() {
  Serial2.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, -1);
  Serial.printf("[UART] link ready — RX on GPIO%d\n", UART_RX_PIN);
}

// Returns true exactly once when a SCAN_TRIGGER command arrives.
bool uartScanTriggerReceived() {
  bool triggered = false;
  while (Serial2.available()) {
    String msg = Serial2.readStringUntil('\n');
    msg.trim();
    if (msg == "SCAN_TRIGGER") triggered = true;
  }
  return triggered;
}
