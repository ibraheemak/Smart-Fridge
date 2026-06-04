#pragma once

// ============================================================================
// DHT11 — Temperature & Humidity
// User Stories: #8 (Temperature Monitoring), #9 (Temperature Alert)
//
// Wiring (ESP32 NodeMCU):
//   DHT11 S (left)   -> GPIO 33  +  10kΩ pull-up to 3.3V
//   DHT11 + (middle) -> 3.3V
//   DHT11 - (right)  -> GND
// ============================================================================

#include <DHT.h>

// ----------------------------------------------------------------------------
// PARAMETERS
// ----------------------------------------------------------------------------
#define DHT_PIN              33
#define DHT_TYPE             DHT11
#define DHT_READ_INTERVAL_MS  2000   // read every 2 seconds
#define DHT_UPLOAD_INTERVAL_MS 30000 // upload to Firestore every 30 seconds
#define TEMP_MIN_C            1      // alert: too cold (risk of freezing)
#define TEMP_MAX_C            8      // alert: too warm (food spoilage)

// ----------------------------------------------------------------------------
// GLOBALS
// ----------------------------------------------------------------------------
static DHT           g_dht(DHT_PIN, DHT_TYPE);
static float         g_temp_c    = NAN;
static float         g_humidity  = NAN;
static unsigned long g_dht_last_read_ms   = 0;
static unsigned long g_dht_last_upload_ms = 0;

// ----------------------------------------------------------------------------
// FIRESTORE UPLOAD
// Writes to: fridges/{FRIDGE_ID}/sensors/temperature
// ----------------------------------------------------------------------------
static bool dhtUploadToFirestore() {
  if (WiFi.status() != WL_CONNECTED || isnan(g_temp_c)) return false;

  StaticJsonDocument<512> doc;
  JsonObject fields = doc.createNestedObject("fields");
  fields["temperatureC"]["doubleValue"] = g_temp_c;
  fields["humidity"]["doubleValue"]     = g_humidity;
  fields["source"]["stringValue"]       = "DHT11";

  time_t now = time(nullptr);
  char ts[20];
  strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&now));
  fields["updatedAt"]["stringValue"] = ts;

  String payload;
  serializeJson(doc, payload);

  String url = "https://firestore.googleapis.com/v1/projects/" +
               String(FIREBASE_PROJECT_ID) +
               "/databases/(default)/documents/fridges/" +
               String(FRIDGE_ID) +
               "/sensors/temperature?key=" + String(FIREBASE_API_KEY);

  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  int code = http.PATCH(payload);
  http.end();

  Serial.printf("[DHT11] Firestore upload %s\n",
                (code == 200 || code == 201) ? "OK" : "FAILED");
  return (code == 200 || code == 201);
}

// ----------------------------------------------------------------------------
// INIT — call once in setup()
// ----------------------------------------------------------------------------
void initDHT11() {
  g_dht.begin();
  Serial.printf("[DHT11] Initialized on GPIO %d\n", DHT_PIN);
}

// ----------------------------------------------------------------------------
// TICK — call every loop()
// ----------------------------------------------------------------------------
void tickDHT11() {
  unsigned long now = millis();
  if (now - g_dht_last_read_ms < DHT_READ_INTERVAL_MS) return;
  g_dht_last_read_ms = now;

  float h = g_dht.readHumidity();
  float t = g_dht.readTemperature();

  if (isnan(h) || isnan(t)) {
    Serial.println("[DHT11] Read failed — check wiring");
    return;
  }

  g_temp_c  = t;
  g_humidity = h;
  Serial.printf("[DHT11] Temp: %.1f C   Humidity: %.1f%%\n", t, h);

  // Temperature alerts
  if (t > TEMP_MAX_C)
    Serial.printf("[DHT11] ALERT: Too warm! %.1f C (max %d)\n", t, TEMP_MAX_C);
  else if (t < TEMP_MIN_C)
    Serial.printf("[DHT11] ALERT: Too cold! %.1f C (min %d)\n", t, TEMP_MIN_C);

  // Upload on interval
  if (now - g_dht_last_upload_ms >= DHT_UPLOAD_INTERVAL_MS) {
    g_dht_last_upload_ms = now;
    dhtUploadToFirestore();
  }
}

// ----------------------------------------------------------------------------
// GETTERS — for display or future alert features
// ----------------------------------------------------------------------------
float dhtGetTemp()     { return g_temp_c;   }
float dhtGetHumidity() { return g_humidity; }
bool  dhtIsTempOK()    { return !isnan(g_temp_c) && g_temp_c >= TEMP_MIN_C && g_temp_c <= TEMP_MAX_C; }
