#include <WiFi.h>

// Flash this on the CAM board to read its MAC address, then paste it into
// CAM_MAC_ADDR in ESP32/SmartFridge_ESP32_CH/parameters.h (espnow_link.h
// unicasts SCAN_TRIGGER to that address — see CLAUDE.md "ESP-NOW link").
void setup() {
  Serial.begin(115200);
  delay(500);
  WiFi.mode(WIFI_STA);
  WiFi.begin();  // no SSID — just brings up the STA netif so macAddress() is valid
  delay(500);
  Serial.print("MAC address: ");
  Serial.println(WiFi.macAddress());
}

void loop() {}
