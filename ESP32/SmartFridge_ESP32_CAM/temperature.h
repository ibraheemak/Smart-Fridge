#pragma once

#include "parameters.h"

// ----------------------------------------------------------------------------
// DHT11 temperature/humidity sensor (US #8, #9)
//
// Bit-banged manually with interrupts disabled during the read — the
// Adafruit/DHTesp libraries' digitalRead-based polling loses the timing
// race against the WiFi/RTOS scheduler on ESP32 and reports TIMEOUT even
// when the sensor itself is fine.
// ----------------------------------------------------------------------------

void initTempSensor() {
  pinMode(DHT_PIN, INPUT_PULLUP);
  Serial.printf("[TEMP] init — DHT11 on GPIO%d\n", DHT_PIN);
}

// Read one 40-bit DHT11 frame into data[5] (humidity int, humidity dec,
// temp int, temp dec, checksum). Returns true on success.
//
// Uses a FreeRTOS critical section (not just noInterrupts()) because on
// ESP32 with WiFi active, plain interrupt-disable doesn't stop scheduler/
// flash-cache stalls from the WiFi stack on the other core, which blow
// the ~100us timing windows below.
static portMUX_TYPE dhtMux = portMUX_INITIALIZER_UNLOCKED;

// Returns 0 on success, or a stage code identifying where the read failed
// (for diagnosing whether the sensor is responding at all vs. a checksum
// error vs. a single-bit timing glitch):
//   1 = pin never went low before start (line stuck high)
//   2/3/4 = no ACK from sensor (low/high/low sequence) — sensor not responding
//   5 = bit start (low) timeout while reading data
//   6 = bit high-pulse timeout while reading data
//   7 = checksum mismatch
static int dhtReadRaw(uint8_t data[5]) {
  memset(data, 0, 5);

  // 1. Start signal: pull low >=18ms, then release.
  pinMode(DHT_PIN, OUTPUT);
  digitalWrite(DHT_PIN, LOW);
  delay(20);

  taskENTER_CRITICAL(&dhtMux);
  pinMode(DHT_PIN, INPUT_PULLUP);

  // 2. Wait for sensor's ACK: low ~80us, then high ~80us.
  uint32_t timeout = micros();
  while (digitalRead(DHT_PIN) == HIGH) {
    if (micros() - timeout > 200) { taskEXIT_CRITICAL(&dhtMux); return 2; }
  }
  timeout = micros();
  while (digitalRead(DHT_PIN) == LOW) {
    if (micros() - timeout > 200) { taskEXIT_CRITICAL(&dhtMux); return 3; }
  }
  timeout = micros();
  while (digitalRead(DHT_PIN) == HIGH) {
    if (micros() - timeout > 200) { taskEXIT_CRITICAL(&dhtMux); return 4; }
  }

  // 3. Read 40 bits. Each bit = ~50us low, then high for ~26-28us (0)
  //    or ~70us (1).
  for (int i = 0; i < 40; i++) {
    timeout = micros();
    while (digitalRead(DHT_PIN) == LOW) {
      if (micros() - timeout > 200) { taskEXIT_CRITICAL(&dhtMux); return 5; }
    }

    uint32_t highStart = micros();
    while (digitalRead(DHT_PIN) == HIGH) {
      if (micros() - highStart > 250) { taskEXIT_CRITICAL(&dhtMux); return 6; }
    }
    uint32_t highDur = micros() - highStart;

    data[i / 8] <<= 1;
    if (highDur > 40) data[i / 8] |= 1;
  }
  taskEXIT_CRITICAL(&dhtMux);

  uint8_t checksum = data[0] + data[1] + data[2] + data[3];
  if (checksum != data[4]) {
    Serial.printf("[TEMP] checksum mismatch: got %02X %02X %02X %02X %02X\n",
                  data[0], data[1], data[2], data[3], data[4]);
    return 7;
  }
  return 0;
}

// Reads the DHT11. Returns true and fills outputs on success.
// DHT11's bit-banged timing occasionally glitches, so retry a few times
// (the sensor needs >=1s between reads) before reporting failure.
bool readTemperature(float &tempC, float &humidity) {
  uint8_t data[5];
  for (int attempt = 0; attempt < 3; attempt++) {
    if (attempt > 0) delay(1500);
    int stage = dhtReadRaw(data);
    if (stage == 0) {
      humidity = data[0] + data[1] / 10.0f;
      tempC    = data[2] + data[3] / 10.0f;
      return true;
    }
    Serial.printf("[TEMP] attempt %d failed at stage %d\n", attempt + 1, stage);
  }
  return false;
}
