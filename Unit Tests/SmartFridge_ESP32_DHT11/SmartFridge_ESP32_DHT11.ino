/*
 * SmartFridge — DHT11 Temperature & Humidity Sensor (isolated test)
 *
 * US #8, #9 — Temperature monitoring + alert. Standalone sketch: NO camera,
 * NO WiFi, NO display. Purpose: confirm wiring and readings on GPIO 33
 * before integrating into SmartFridge_ESP32_CAM.
 *
 * Wiring (3-pin KY-015-style module — onboard pull-up, no external
 * resistor needed; follow the labels printed on the board, not generic
 * online diagrams):
 *   DHT11 "+" → 3.3V
 *   DHT11 "-" → GND
 *   DHT11 "S" → GPIO 14   (free pin on CAM board)
 *
 * No external library: this sketch bit-bangs the DHT11 one-wire protocol
 * directly with interrupts disabled during the timing-critical read, which
 * is more reliable on ESP32 than the Adafruit/DHTesp libraries (their
 * digitalRead-based polling loses the race against the WiFi/RTOS
 * scheduler).
 *
 * Runs on any ESP32 (devkit or CAM). Watch the serial log (115200 baud)
 * for temperature (°C) and humidity (%) readings every ~2.5 seconds.
 */

#define DHT_PIN   14     // free pin on CAM board

// Read one 40-bit DHT11 frame into data[5] (humidity int, humidity dec,
// temp int, temp dec, checksum). Returns true on success.
static bool dhtRead(uint8_t data[5]) {
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

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[BOOT] DHT11 temperature/humidity sensor test");
  Serial.printf("[BOOT] DATA (S) on GPIO%d\n", DHT_PIN);
  delay(1500);  // DHT11 needs to settle before its first read
}

void loop() {
  delay(2000);  // DHT11 needs at least 1-2s between reads

  uint8_t data[5];
  if (!dhtRead(data)) {
    Serial.println("[DHT11] Read failed (no response or checksum mismatch)");
    return;
  }

  float humidity    = data[0] + data[1] / 10.0f;
  float tempCelsius = data[2] + data[3] / 10.0f;

  Serial.printf("[DHT11] Temperature = %.1f C   Humidity = %.1f %%\n",
                tempCelsius, humidity);
}
