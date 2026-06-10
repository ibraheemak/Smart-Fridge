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
static bool dhtReadRaw(uint8_t data[5]) {
  memset(data, 0, 5);

  // 1. Start signal: pull low >=18ms, then release.
  pinMode(DHT_PIN, OUTPUT);
  digitalWrite(DHT_PIN, LOW);
  delay(20);

  noInterrupts();
  pinMode(DHT_PIN, INPUT_PULLUP);

  // 2. Wait for sensor's ACK: low ~80us, then high ~80us.
  uint32_t timeout = micros();
  while (digitalRead(DHT_PIN) == HIGH) {
    if (micros() - timeout > 100) { interrupts(); return false; }
  }
  timeout = micros();
  while (digitalRead(DHT_PIN) == LOW) {
    if (micros() - timeout > 100) { interrupts(); return false; }
  }
  timeout = micros();
  while (digitalRead(DHT_PIN) == HIGH) {
    if (micros() - timeout > 100) { interrupts(); return false; }
  }

  // 3. Read 40 bits. Each bit = ~50us low, then high for ~26-28us (0)
  //    or ~70us (1).
  for (int i = 0; i < 40; i++) {
    timeout = micros();
    while (digitalRead(DHT_PIN) == LOW) {
      if (micros() - timeout > 100) { interrupts(); return false; }
    }

    uint32_t highStart = micros();
    while (digitalRead(DHT_PIN) == HIGH) {
      if (micros() - highStart > 150) { interrupts(); return false; }
    }
    uint32_t highDur = micros() - highStart;

    data[i / 8] <<= 1;
    if (highDur > 40) data[i / 8] |= 1;
  }
  interrupts();

  uint8_t checksum = data[0] + data[1] + data[2] + data[3];
  return (checksum == data[4]);
}

// Reads the DHT11. Returns true and fills outputs on success.
bool readTemperature(float &tempC, float &humidity) {
  uint8_t data[5];
  if (!dhtReadRaw(data)) return false;

  humidity = data[0] + data[1] / 10.0f;
  tempC    = data[2] + data[3] / 10.0f;
  return true;
}
