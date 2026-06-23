#pragma once

#include <HardwareSerial.h>
#include "parameters.h"

// ============================================================================
// UART link to the CAM board — one-way, CH sends commands, CAM doesn't reply.
//
// Wiring (2 wires):
//   CH GPIO 17 (TX) --------> CAM GPIO 13 (RX)
//   CH GND          --------> CAM GND
//
// ⚠️ Uses hardware UART1 — NOT Serial2/UART2. The GM65 barcode scanner
// (gm65.h) already owns UART2 via `HardwareSerial GM65Serial(2)`. The ESP32
// only has three UART peripherals (UART0 = USB debug, UART2 = GM65), so this
// link must live on the one that's left, UART1, or the two would fight over
// the same peripheral and the last .begin() would silently steal the pins —
// breaking the door-close SCAN_TRIGGER. The ESP32 GPIO matrix lets UART1
// route to any pins, so GPIO 17 works fine even though it's not UART1's
// default TX pad.
// ============================================================================

HardwareSerial UartLinkSerial(1);   // UART1 — dedicated to the scan-trigger link

void initUartLink() {
  // RX pin unused (CAM never replies) — pass -1 so no GPIO is needlessly tied up.
  UartLinkSerial.begin(UART_BAUD, SERIAL_8N1, -1, UART_TX_PIN);
  Serial.printf("[UART] link ready (UART1) — TX on GPIO%d\n", UART_TX_PIN);
}

void uartSendScanTrigger() {
  UartLinkSerial.println("SCAN_TRIGGER");
  Serial.println("[UART] >> SCAN_TRIGGER sent");
}
