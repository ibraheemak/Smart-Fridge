/*
 * SmartFridge — XPT2046 touch-only test (NO TFT)
 *
 * Talks directly to the XPT2046 touch controller over SPI, WITHOUT ever
 * initializing the ILI9488 display. This isolates whether the display
 * (TFT_CS tied to GND, always selected) is interfering with touch reads.
 *
 * Wiring used here (matches SmartFridge_ESP32_CH):
 *   T_CLK -> GPIO 18
 *   T_DIN -> GPIO 23
 *   T_DO  -> GPIO 19
 *   T_CS  -> GPIO 5
 *   T_IRQ -> not connected
 *
 * Open the serial monitor at 115200 baud. With nothing touching the panel,
 * x/y/z should look like noise / fixed rail values (e.g. near 0 or 4095)
 * and NOT change much. When you press the panel, 'z' should rise sharply
 * and x/y should track the touch position and change as you move your
 * finger.
 *
 * If this ALSO shows no response to touch, the problem is the touch
 * chip's own wiring (T_CLK/T_DIN/T_DO/T_CS), not the TFT.
 * If THIS sketch responds correctly but the display test sketch did not,
 * the TFT (TFT_CS tied to GND) is interfering with touch reads.
 */

#include <SPI.h>

#define T_CLK 18
#define T_DIN 23
#define T_DO  19
#define T_CS   5

// XPT2046 control bytes (12-bit, single-ended, power-down between conversions)
#define CMD_READ_X  0xD0
#define CMD_READ_Y  0x90
#define CMD_READ_Z1 0xB0
#define CMD_READ_Z2 0xC0

SPIClass touchSPI(VSPI);

uint16_t xptRead(uint8_t cmd) {
  digitalWrite(T_CS, LOW);
  touchSPI.transfer(cmd);
  uint16_t raw = touchSPI.transfer16(0x0000);
  digitalWrite(T_CS, HIGH);
  return raw >> 3; // top 12 bits of the 16-bit response
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[BOOT] XPT2046 touch-only test (no TFT init)");

  pinMode(T_CS, OUTPUT);
  digitalWrite(T_CS, HIGH);

  touchSPI.begin(T_CLK, T_DO, T_DIN, T_CS);
  touchSPI.setFrequency(1000000);
  touchSPI.setDataMode(SPI_MODE0);

  Serial.println("[READY] Printing raw x/y/z every 200ms. Press the panel and watch for changes.");
}

void loop() {
  uint16_t x  = xptRead(CMD_READ_X);
  uint16_t y  = xptRead(CMD_READ_Y);
  uint16_t z1 = xptRead(CMD_READ_Z1);
  uint16_t z2 = xptRead(CMD_READ_Z2);

  // Standard pressure formula: lower value = more pressure
  int z = (int)z1 + 4095 - (int)z2;

  Serial.printf("[TOUCH] x=%u y=%u z1=%u z2=%u z=%d\n", x, y, z1, z2, z);

  delay(200);
}
