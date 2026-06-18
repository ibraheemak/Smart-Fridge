#pragma once

#include "parameters.h"

// ============================================================================
// UART link to the CAM board — one-way, CH sends commands, CAM doesn't reply.
//
// Wiring (2 wires):
//   CH GPIO 17 (TX2) -------> CAM GPIO 13 (RX)
//   CH GND           -------> CAM GND
// ============================================================================

void initUartLink() {
  Serial2.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
  Serial.printf("[UART] link ready — TX on GPIO%d\n", UART_TX_PIN);
}

void uartSendScanTrigger() {
  Serial2.println("SCAN_TRIGGER");
  Serial.println("[UART] >> SCAN_TRIGGER sent");
}
